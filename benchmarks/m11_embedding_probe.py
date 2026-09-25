#!/usr/bin/env python3
"""M11 Experiment C: pretrained local semantic retrieval over static man text.

Research tooling only. This probe chunks cleaned local section 1/8 man-page text,
embeds those chunks with a compact pretrained sentence-embedding model, and
measures command recovery on the M11 research + held-out corpora.

The probe never executes candidate commands and never alters Acclorite's runtime
index. Network access is disabled by default: pass --allow-download explicitly
for the one-time research-model fetch if it is not already cached locally.
"""
from __future__ import annotations

import argparse
import collections
import json
import os
import re
import resource
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

import m11_mantext_probe as manprobe

DEFAULT_MODEL = "sentence-transformers/all-MiniLM-L6-v2"
ROFF_FONT_RE = re.compile(r"\\f(?:\[[^]]+\]|\([^)]{2}|.)")
ROFF_SIZE_RE = re.compile(r"\\s(?:[-+]?\d+|\[[^]]+\])")
ROFF_ESCAPE_REPLACEMENTS = {
    r"\-": "-",
    r"\e": "\\",
    r"\&": "",
    r"\~": " ",
    r"\ ": " ",
    r"\|": " ",
    r"\^": " ",
}
PARAGRAPH_MACROS = {"SH", "SS", "PP", "P", "LP", "TP", "TQ", "RS", "RE", "br", "sp", "nf", "fi"}
TEXT_MACROS = {"B", "I", "BR", "BI", "IB", "IR", "RB", "RI", "SM", "SB", "IP"}


@dataclass
class PlainPage:
    command: str
    path: str
    text: str


@dataclass
class Chunk:
    command: str
    path: str
    text: str


def _rss_mib() -> float:
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024.0


def _accepted(case: dict) -> set[str]:
    expected = case.get("expect", {})
    return set(expected.get("top1_any", [])) | set(expected.get("top3_any", []))


def _load_corpora(paths: list[Path]) -> list[dict]:
    corpora: list[dict] = []
    for path in paths:
        try:
            corpus = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            print(f"error: cannot read corpus {path}: {exc}", file=sys.stderr)
            raise SystemExit(2) from exc
        corpus["_path"] = str(path)
        corpora.append(corpus)
    return corpora


def _unquote_macro_arg(text: str) -> str:
    text = text.strip()
    if len(text) >= 2 and text[0] == text[-1] == '"':
        text = text[1:-1]
    return text


def roff_to_plain(text: str) -> str:
    """Conservatively remove common man-page roff markup while preserving prose."""
    output: list[str] = []
    for raw_line in text.splitlines():
        line = raw_line.rstrip()
        stripped = line.lstrip()
        if not stripped:
            output.append("")
            continue
        if stripped.startswith('.\\"') or stripped.startswith(".\""):
            continue
        if stripped.startswith(".") or stripped.startswith("'"):
            body = stripped[1:].lstrip()
            if not body:
                continue
            macro, _, rest = body.partition(" ")
            if macro in {"TH", "Dd", "Dt", "Os"}:
                continue
            if macro in {"SH", "SS", "Sh", "Ss"}:
                if rest.strip():
                    output.extend(["", _unquote_macro_arg(rest), ""])
                else:
                    output.append("")
                continue
            if macro in PARAGRAPH_MACROS:
                output.append("")
                if macro in {"TP", "TQ"} and rest.strip():
                    output.append(_unquote_macro_arg(rest))
                continue
            if macro in TEXT_MACROS:
                if rest.strip():
                    output.append(_unquote_macro_arg(rest))
                continue
            # Unknown request/macros are not interpreted. Keeping their arguments
            # is safer for research recall than keeping the roff control token.
            if rest.strip() and not macro.startswith("\\"):
                output.append(_unquote_macro_arg(rest))
            continue
        output.append(line)

    plain = "\n".join(output)
    plain = ROFF_FONT_RE.sub("", plain)
    plain = ROFF_SIZE_RE.sub("", plain)
    for source, replacement in ROFF_ESCAPE_REPLACEMENTS.items():
        plain = plain.replace(source, replacement)
    # Common named escapes. Do not attempt to become a general roff interpreter.
    plain = re.sub(r"\\\((?:aq|cq|dq)", "'", plain)
    plain = re.sub(r"\\\((?:em|en)", "-", plain)
    plain = re.sub(r"\\\[[^]]+\]", " ", plain)
    plain = re.sub(r"[ \t]+", " ", plain)
    plain = re.sub(r"\n[ \t]+", "\n", plain)
    plain = re.sub(r"\n{3,}", "\n\n", plain)
    return plain.strip()


def build_plain_pages(pages: Iterable[Path]) -> tuple[list[PlainPage], collections.Counter[str]]:
    page_list = list(pages)
    page_by_name: dict[str, Path] = {}
    for page in page_list:
        page_by_name.setdefault(page.name, page)
        if page.suffix.lower() in manprobe.COMPRESSED_SUFFIXES:
            page_by_name.setdefault(page.name[: -len(page.suffix)], page)

    result: list[PlainPage] = []
    skipped: collections.Counter[str] = collections.Counter()
    for page in page_list:
        command = manprobe._command_from_filename(page)
        if not command:
            continue
        raw, reason = manprobe._read_resolving_alias(page, page_by_name)
        if raw is None:
            skipped[reason or "unknown"] += 1
            continue
        plain = roff_to_plain(raw)
        if len(plain.split()) < 3:
            skipped["empty"] += 1
            continue
        result.append(PlainPage(command=command, path=str(page), text=plain))
    return result, skipped


def _load_sentence_transformer(model_name: str, allow_download: bool, cache_dir: Path | None, device: str):
    try:
        from sentence_transformers import SentenceTransformer
    except ImportError as exc:  # pragma: no cover - depends on research host
        print(
            "error: Experiment C requires the research-only Python package "
            "sentence-transformers. Acclorite runtime does not require it. "
            "Install it in an isolated research environment or via your distro/AUR package, "
            "then rerun the probe.",
            file=sys.stderr,
        )
        raise SystemExit(3) from exc

    if not allow_download:
        os.environ.setdefault("HF_HUB_OFFLINE", "1")
        os.environ.setdefault("TRANSFORMERS_OFFLINE", "1")

    kwargs = {
        "device": device,
        "local_files_only": not allow_download,
    }
    if cache_dir is not None:
        kwargs["cache_folder"] = str(cache_dir)
    try:
        return SentenceTransformer(model_name, **kwargs)
    except Exception as exc:  # pragma: no cover - depends on model/cache/runtime
        if not allow_download:
            print(
                f"error: pretrained model '{model_name}' is not available in the local cache. "
                "Experiment C disables network access by default. Rerun once with --allow-download "
                "to fetch the research model explicitly, then future runs can stay offline.",
                file=sys.stderr,
            )
            raise SystemExit(3) from exc
        raise


def chunk_page(page: PlainPage, tokenizer, chunk_tokens: int, overlap_tokens: int, model_limit: int) -> list[Chunk]:
    special = 0
    try:
        special = int(tokenizer.num_special_tokens_to_add(pair=False))
    except Exception:
        special = 2
    safe_limit = max(16, model_limit - max(special, 2) - 8)
    width = min(chunk_tokens, safe_limit)
    if overlap_tokens >= width:
        raise ValueError("overlap must be smaller than effective chunk width")
    token_ids = tokenizer.encode(page.text, add_special_tokens=False)
    if not token_ids:
        return []

    chunks: list[Chunk] = []
    step = width - overlap_tokens
    prefix = f"Command {page.command}. "
    for start in range(0, len(token_ids), step):
        current = token_ids[start : start + width]
        if not current:
            break
        passage = tokenizer.decode(current, skip_special_tokens=True, clean_up_tokenization_spaces=True).strip()
        if passage:
            chunks.append(Chunk(page.command, page.path, prefix + passage))
        if start + width >= len(token_ids):
            break
    return chunks


def build_chunks(
    pages: list[PlainPage], tokenizer, chunk_tokens: int, overlap_tokens: int, model_limit: int
) -> list[Chunk]:
    chunks: list[Chunk] = []
    for page in pages:
        chunks.extend(chunk_page(page, tokenizer, chunk_tokens, overlap_tokens, model_limit))
    return chunks


def _rank_commands(np, scores, chunk_command_ids, commands: list[str], top_k: int):
    best = np.full(len(commands), -np.inf, dtype=np.float32)
    np.maximum.at(best, chunk_command_ids, scores)
    order = np.argsort(-best, kind="stable")[:top_k]
    return order, best


def evaluate(
    corpora: list[dict], chunks: list[Chunk], command_names: list[str], chunk_command_ids,
    chunk_embeddings, query_embeddings, query_offsets: list[tuple[int, int]], np, top_k: int,
) -> list[dict]:
    reports: list[dict] = []
    for corpus, (begin, end) in zip(corpora, query_offsets):
        available_commands = set(command_names)
        cases: list[dict] = []
        available_cases = top1 = top3 = top10 = 0
        query_matrix = query_embeddings[begin:end]
        similarities = query_matrix @ chunk_embeddings.T

        for case_index, case in enumerate(corpus.get("cases", [])):
            accepted = _accepted(case)
            available = sorted(accepted & available_commands)
            order, best = _rank_commands(np, similarities[case_index], chunk_command_ids, command_names, top_k)
            ranking = [(command_names[int(i)], float(best[int(i)])) for i in order]
            rank = next((i for i, (command, _) in enumerate(ranking, 1) if command in accepted), None)
            if available:
                available_cases += 1
                top1 += rank == 1
                top3 += rank is not None and rank <= 3
                top10 += rank is not None and rank <= 10

            top_items = []
            for command, score in ranking[:5]:
                command_id = command_names.index(command)
                mask = chunk_command_ids == command_id
                candidate_indices = np.flatnonzero(mask)
                local_scores = similarities[case_index, candidate_indices]
                best_chunk_index = int(candidate_indices[int(np.argmax(local_scores))])
                snippet = re.sub(r"\s+", " ", chunks[best_chunk_index].text).strip()
                if len(snippet) > 180:
                    snippet = snippet[:177] + "..."
                top_items.append({
                    "command": command,
                    "score": round(score, 6),
                    "source": chunks[best_chunk_index].path,
                    "snippet": snippet,
                })

            cases.append({
                "id": str(case.get("id", "")),
                "query": str(case.get("query", "")),
                "accepted": sorted(accepted),
                "available_expected": available,
                "expected_rank": rank,
                "top": top_items,
            })

        reports.append({
            "corpus": str(corpus.get("name", "")),
            "corpus_path": str(corpus.get("_path", "")),
            "available_cases": available_cases,
            "top1": top1,
            "top3": top3,
            "top10": top10,
            "cases": cases,
        })
    return reports


def _print_corpus(report: dict) -> None:
    available = report["available_cases"]
    print(f"Corpus: {report['corpus']}")
    if available:
        print(f"  Top-1 recovery:  {report['top1']}/{available}  {100.0 * report['top1'] / available:.1f}%")
        print(f"  Top-3 recovery:  {report['top3']}/{available}  {100.0 * report['top3'] / available:.1f}%")
        print(f"  Top-10 recovery: {report['top10']}/{available}  {100.0 * report['top10'] / available:.1f}%")
    print()
    for case in report["cases"]:
        if not case["available_expected"]:
            state = "NO-EXPECTED-MAN"
        elif case["expected_rank"] is None:
            state = "MISS"
        else:
            state = f"RANK {case['expected_rank']}"
        top = ", ".join(item["command"] for item in case["top"]) or "<none>"
        print(f"  {case['id']:<38} {state:<16} top={top}")
    print()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--corpus", action="append", type=Path, default=[],
        help="Corpus to evaluate; may be repeated. Defaults to research-v1 + heldout-v1.",
    )
    parser.add_argument("--model", default=DEFAULT_MODEL)
    parser.add_argument("--allow-download", action="store_true", help="Explicitly allow one-time model download.")
    parser.add_argument("--cache-dir", type=Path)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--chunk-tokens", type=int, default=192)
    parser.add_argument("--overlap-tokens", type=int, default=32)
    parser.add_argument("--top-k", type=int, default=100)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--man-root", action="append", type=Path, default=[])
    args = parser.parse_args()

    if args.batch_size < 1:
        print("error: --batch-size must be positive", file=sys.stderr)
        return 2
    if args.chunk_tokens < 32:
        print("error: --chunk-tokens must be at least 32", file=sys.stderr)
        return 2
    if args.overlap_tokens < 0 or args.overlap_tokens >= args.chunk_tokens:
        print("error: --overlap-tokens must be >= 0 and smaller than --chunk-tokens", file=sys.stderr)
        return 2
    if args.top_k < 10:
        print("error: --top-k must be at least 10", file=sys.stderr)
        return 2

    corpus_paths = args.corpus or [
        Path("benchmarks/corpus-m11-research-v1.json"),
        Path("benchmarks/corpus-m11-heldout-v1.json"),
    ]
    corpora = _load_corpora(corpus_paths)

    started = time.perf_counter()
    roots = args.man_root or manprobe.man_roots()
    discover_started = time.perf_counter()
    source_pages = manprobe.discover_pages(roots)
    discover_done = time.perf_counter()
    plain_pages, skipped = build_plain_pages(source_pages)
    prepare_done = time.perf_counter()
    if not plain_pages:
        print("error: no readable section 1/8 local man pages found", file=sys.stderr)
        return 3

    model_started = time.perf_counter()
    model = _load_sentence_transformer(args.model, args.allow_download, args.cache_dir, args.device)
    model_loaded = time.perf_counter()
    tokenizer = model.tokenizer
    model_limit = int(getattr(model, "max_seq_length", 256) or 256)
    chunks = build_chunks(plain_pages, tokenizer, args.chunk_tokens, args.overlap_tokens, model_limit)
    chunked_done = time.perf_counter()
    if not chunks:
        print("error: no semantic chunks produced from local manuals", file=sys.stderr)
        return 3

    try:
        import numpy as np
    except ImportError as exc:  # pragma: no cover
        print("error: Experiment C requires NumPy through its research embedding stack", file=sys.stderr)
        raise SystemExit(3) from exc

    command_names = sorted({chunk.command for chunk in chunks})
    command_ids = {command: i for i, command in enumerate(command_names)}
    chunk_command_ids = np.asarray([command_ids[chunk.command] for chunk in chunks], dtype=np.int32)

    embed_started = time.perf_counter()
    chunk_embeddings = model.encode(
        [chunk.text for chunk in chunks],
        batch_size=args.batch_size,
        show_progress_bar=True,
        convert_to_numpy=True,
        normalize_embeddings=True,
    ).astype(np.float32, copy=False)
    embed_done = time.perf_counter()

    all_queries: list[str] = []
    query_offsets: list[tuple[int, int]] = []
    for corpus in corpora:
        begin = len(all_queries)
        all_queries.extend(str(case.get("query", "")) for case in corpus.get("cases", []))
        query_offsets.append((begin, len(all_queries)))
    query_embeddings = model.encode(
        all_queries,
        batch_size=args.batch_size,
        show_progress_bar=False,
        convert_to_numpy=True,
        normalize_embeddings=True,
    ).astype(np.float32, copy=False)
    query_embed_done = time.perf_counter()

    reports = evaluate(
        corpora, chunks, command_names, chunk_command_ids,
        chunk_embeddings, query_embeddings, query_offsets, np, args.top_k,
    )
    evaluated = time.perf_counter()

    report = {
        "experiment": "m11-pretrained-embedding-probe-v1",
        "model": args.model,
        "device": args.device,
        "model_max_seq_length": model_limit,
        "embedding_dimensions": int(chunk_embeddings.shape[1]),
        "chunk_tokens_requested": args.chunk_tokens,
        "overlap_tokens": args.overlap_tokens,
        "pages_discovered": len(source_pages),
        "plain_pages": len(plain_pages),
        "indexed_commands": len(command_names),
        "chunks": len(chunks),
        "skipped": dict(skipped),
        "peak_rss_mib": round(_rss_mib(), 1),
        "timing_ms": {
            "discover": round((discover_done - discover_started) * 1000.0, 1),
            "plain_prepare": round((prepare_done - discover_done) * 1000.0, 1),
            "model_load": round((model_loaded - model_started) * 1000.0, 1),
            "tokenize_chunk": round((chunked_done - model_loaded) * 1000.0, 1),
            "embed_chunks": round((embed_done - embed_started) * 1000.0, 1),
            "embed_queries": round((query_embed_done - embed_done) * 1000.0, 1),
            "evaluate": round((evaluated - query_embed_done) * 1000.0, 1),
            "total": round((evaluated - started) * 1000.0, 1),
        },
        "corpora": reports,
    }

    print("Acclorite M11 pretrained local-embedding probe")
    print("─" * 76)
    print(f"Model:              {args.model}")
    print(f"Device:             {args.device}")
    print(f"Pages discovered:   {len(source_pages)}")
    print(f"Indexed commands:   {len(command_names)}")
    print(f"Semantic chunks:    {len(chunks)}")
    print(f"Embedding dims:     {chunk_embeddings.shape[1]}")
    print(f"Model seq limit:    {model_limit}")
    print(f"Peak RSS:           {report['peak_rss_mib']:.1f} MiB")
    timing = report["timing_ms"]
    print(
        "Probe timing:       "
        f"discover {timing['discover']:.1f} ms · plain {timing['plain_prepare']:.1f} ms · "
        f"model {timing['model_load']:.1f} ms · chunk {timing['tokenize_chunk']:.1f} ms · "
        f"embed {timing['embed_chunks']:.1f} ms · queries {timing['embed_queries']:.1f} ms · "
        f"evaluate {timing['evaluate']:.1f} ms · total {timing['total']:.1f} ms"
    )
    print()
    for corpus_report in reports:
        _print_corpus(corpus_report)

    if args.json_out:
        args.json_out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
