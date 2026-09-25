#!/usr/bin/env python3
"""M11 Experiment D: compact, query-blind pretrained semantic index.

Research tooling only. This probe keeps Experiment C's pretrained semantic model,
but selects a small query-blind subset of source-backed man-page passages per
command before embedding them. It evaluates several per-command passage budgets
from one maximum-budget embedding pass.

The probe never executes candidate commands and never alters Acclorite's runtime
index. Network access is disabled by default; the model should normally already
be cached by Experiment C.
"""
from __future__ import annotations

import argparse
import collections
import json
import math
import re
import resource
import sys
import time
from dataclasses import dataclass
from pathlib import Path

import m11_mantext_probe as manprobe
import m11_embedding_probe as embedprobe

WORD_RE = re.compile(r"[a-z0-9][a-z0-9_+.-]{1,}")
GENERIC_TERMS = {
    "command", "commands", "option", "options", "file", "files", "name", "names",
    "value", "values", "argument", "arguments", "default", "use", "used", "using",
    "the", "and", "for", "from", "with", "this", "that", "into", "when", "where",
    "which", "will", "may", "can", "not", "are", "is", "to", "of", "in", "on",
}
DEFAULT_BUDGETS = (1, 2, 4)


@dataclass
class PassageCandidate:
    chunk: embedprobe.Chunk
    ordinal: int
    token_set: frozenset[str]
    salience: float = 0.0


@dataclass
class SelectedPassage:
    chunk: embedprobe.Chunk
    selection_rank: int
    original_ordinal: int
    salience: float


def _rss_mib() -> float:
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024.0


def lexical_terms(text: str, command: str = "") -> frozenset[str]:
    command_l = command.lower()
    terms = {
        token for token in WORD_RE.findall(text.lower())
        if token not in GENERIC_TERMS
        and token != command_l
        and token.rstrip(".") != command_l
        and not token.isdigit()
    }
    return frozenset(terms)


def command_document_frequencies(grouped: dict[str, list[PassageCandidate]]) -> collections.Counter[str]:
    df: collections.Counter[str] = collections.Counter()
    for candidates in grouped.values():
        seen: set[str] = set()
        for candidate in candidates:
            seen.update(candidate.token_set)
        df.update(seen)
    return df


def assign_salience(grouped: dict[str, list[PassageCandidate]]) -> None:
    df = command_document_frequencies(grouped)
    n_commands = max(1, len(grouped))
    for candidates in grouped.values():
        for candidate in candidates:
            weights = [math.log((n_commands + 1.0) / (df[token] + 1.0)) + 1.0 for token in candidate.token_set]
            weights.sort(reverse=True)
            # A few highly discriminative terms are more useful than rewarding sheer passage length.
            candidate.salience = sum(weights[:12]) / max(1, min(12, len(weights)))


def _jaccard(a: frozenset[str], b: frozenset[str]) -> float:
    if not a or not b:
        return 0.0
    union = len(a | b)
    return len(a & b) / union if union else 0.0


def select_for_command(candidates: list[PassageCandidate], max_passages: int) -> list[SelectedPassage]:
    if not candidates or max_passages <= 0:
        return []
    if len(candidates) <= max_passages:
        return [
            SelectedPassage(c.chunk, i + 1, c.ordinal, c.salience)
            for i, c in enumerate(candidates)
        ]

    # Identity/intro evidence is always retained. Everything else is selected without query knowledge.
    selected: list[PassageCandidate] = [candidates[0]]
    remaining = candidates[1:].copy()
    saliences = [c.salience for c in remaining]
    sal_min = min(saliences) if saliences else 0.0
    sal_max = max(saliences) if saliences else 0.0
    last_ordinal = max(1, candidates[-1].ordinal)

    while remaining and len(selected) < max_passages:
        best: PassageCandidate | None = None
        best_score = -1.0
        for candidate in remaining:
            if sal_max > sal_min:
                salience_norm = (candidate.salience - sal_min) / (sal_max - sal_min)
            else:
                salience_norm = 0.5
            novelty = 1.0 - max(_jaccard(candidate.token_set, item.token_set) for item in selected)
            coverage = min(abs(candidate.ordinal - item.ordinal) for item in selected) / last_ordinal
            # Salience finds command-specific capability language; novelty/coverage stop all slots
            # collapsing onto one verbose part of a long manual.
            score = 0.55 * salience_norm + 0.30 * novelty + 0.15 * coverage
            if score > best_score or (math.isclose(score, best_score) and candidate.ordinal < (best.ordinal if best else 10**9)):
                best = candidate
                best_score = score
        assert best is not None
        selected.append(best)
        remaining.remove(best)

    return [
        SelectedPassage(c.chunk, i + 1, c.ordinal, c.salience)
        for i, c in enumerate(selected)
    ]


def select_passages(chunks: list[embedprobe.Chunk], max_passages: int) -> list[SelectedPassage]:
    grouped: dict[str, list[PassageCandidate]] = collections.defaultdict(list)
    ordinals: collections.Counter[str] = collections.Counter()
    for chunk in chunks:
        ordinal = ordinals[chunk.command]
        ordinals[chunk.command] += 1
        grouped[chunk.command].append(PassageCandidate(chunk, ordinal, lexical_terms(chunk.text, chunk.command)))
    assign_salience(grouped)

    selected: list[SelectedPassage] = []
    for command in sorted(grouped):
        selected.extend(select_for_command(grouped[command], max_passages))
    return selected


def _save_cache(path: Path, np, embeddings, selected: list[SelectedPassage], model_name: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    np.savez(
        path,
        embeddings=embeddings.astype(np.float32, copy=False),
        commands=np.asarray([item.chunk.command for item in selected]),
        sources=np.asarray([item.chunk.path for item in selected]),
        texts=np.asarray([item.chunk.text for item in selected]),
        selection_rank=np.asarray([item.selection_rank for item in selected], dtype=np.int16),
        original_ordinal=np.asarray([item.original_ordinal for item in selected], dtype=np.int32),
        model=np.asarray([model_name]),
    )


def _budget_report(corpora, selected, embeddings, query_embeddings, query_offsets, np, budget: int, top_k: int):
    indices = np.asarray([i for i, item in enumerate(selected) if item.selection_rank <= budget], dtype=np.int64)
    budget_selected = [selected[int(i)] for i in indices]
    budget_chunks = [item.chunk for item in budget_selected]
    budget_embeddings = embeddings[indices]
    commands = sorted({chunk.command for chunk in budget_chunks})
    command_ids = {command: i for i, command in enumerate(commands)}
    chunk_command_ids = np.asarray([command_ids[chunk.command] for chunk in budget_chunks], dtype=np.int32)
    reports = embedprobe.evaluate(
        corpora, budget_chunks, commands, chunk_command_ids,
        budget_embeddings, query_embeddings, query_offsets, np, top_k,
    )
    return {
        "budget": budget,
        "passages": len(budget_chunks),
        "commands": len(commands),
        "corpora": reports,
    }


def _print_budget(report: dict) -> None:
    print(f"Passage budget: {report['budget']}/command · {report['passages']} passages")
    for corpus in report["corpora"]:
        available = corpus["available_cases"]
        if not available:
            continue
        print(
            f"  {corpus['corpus']}: "
            f"Top-1 {corpus['top1']}/{available} {100.0 * corpus['top1'] / available:.1f}% · "
            f"Top-3 {corpus['top3']}/{available} {100.0 * corpus['top3'] / available:.1f}% · "
            f"Top-10 {corpus['top10']}/{available} {100.0 * corpus['top10'] / available:.1f}%"
        )
    print()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", action="append", type=Path, default=[])
    parser.add_argument("--model", default=embedprobe.DEFAULT_MODEL)
    parser.add_argument("--allow-download", action="store_true")
    parser.add_argument("--cache-dir", type=Path)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--chunk-tokens", type=int, default=192)
    parser.add_argument("--overlap-tokens", type=int, default=32)
    parser.add_argument("--max-passages", type=int, default=4)
    parser.add_argument("--budget", action="append", type=int, default=[])
    parser.add_argument("--top-k", type=int, default=100)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--index-out", type=Path, help="Optional reusable NPZ of selected passage embeddings.")
    parser.add_argument("--man-root", action="append", type=Path, default=[])
    args = parser.parse_args()

    budgets = sorted(set(args.budget or DEFAULT_BUDGETS))
    if args.max_passages < 1 or any(b < 1 or b > args.max_passages for b in budgets):
        print("error: budgets must be positive and <= --max-passages", file=sys.stderr)
        return 2
    if args.batch_size < 1 or args.top_k < 10:
        print("error: invalid batch size or top-k", file=sys.stderr)
        return 2

    corpus_paths = args.corpus or [
        Path("benchmarks/corpus-m11-research-v1.json"),
        Path("benchmarks/corpus-m11-heldout-v1.json"),
    ]
    corpora = embedprobe._load_corpora(corpus_paths)

    started = time.perf_counter()
    roots = args.man_root or manprobe.man_roots()
    source_pages = manprobe.discover_pages(roots)
    discovered = time.perf_counter()
    plain_pages, skipped = embedprobe.build_plain_pages(source_pages)
    prepared = time.perf_counter()
    if not plain_pages:
        print("error: no readable section 1/8 local man pages found", file=sys.stderr)
        return 3

    model = embedprobe._load_sentence_transformer(args.model, args.allow_download, args.cache_dir, args.device)
    model_loaded = time.perf_counter()
    tokenizer = model.tokenizer
    model_limit = int(getattr(model, "max_seq_length", 256) or 256)
    all_chunks = embedprobe.build_chunks(plain_pages, tokenizer, args.chunk_tokens, args.overlap_tokens, model_limit)
    chunked = time.perf_counter()
    selected = select_passages(all_chunks, args.max_passages)
    selected_done = time.perf_counter()
    if not selected:
        print("error: compact selector produced no passages", file=sys.stderr)
        return 3

    try:
        import numpy as np
    except ImportError as exc:  # pragma: no cover
        print("error: Experiment D requires NumPy through its research embedding stack", file=sys.stderr)
        raise SystemExit(3) from exc

    embedded_started = time.perf_counter()
    selected_embeddings = model.encode(
        [item.chunk.text for item in selected],
        batch_size=args.batch_size,
        show_progress_bar=True,
        convert_to_numpy=True,
        normalize_embeddings=True,
    ).astype(np.float32, copy=False)
    embedded = time.perf_counter()

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
    queries_done = time.perf_counter()

    budget_reports = [
        _budget_report(corpora, selected, selected_embeddings, query_embeddings, query_offsets, np, budget, args.top_k)
        for budget in budgets
    ]
    evaluated = time.perf_counter()

    if args.index_out:
        _save_cache(args.index_out, np, selected_embeddings, selected, args.model)
    saved = time.perf_counter()

    report = {
        "experiment": "m11-compact-pretrained-embedding-probe-v1",
        "model": args.model,
        "device": args.device,
        "model_max_seq_length": model_limit,
        "embedding_dimensions": int(selected_embeddings.shape[1]),
        "pages_discovered": len(source_pages),
        "plain_pages": len(plain_pages),
        "all_chunks": len(all_chunks),
        "selected_passages": len(selected),
        "indexed_commands": len({item.chunk.command for item in selected}),
        "max_passages": args.max_passages,
        "budgets": budgets,
        "skipped": dict(skipped),
        "peak_rss_mib": round(_rss_mib(), 1),
        "timing_ms": {
            "discover": round((discovered - started) * 1000.0, 1),
            "plain_prepare": round((prepared - discovered) * 1000.0, 1),
            "model_load": round((model_loaded - prepared) * 1000.0, 1),
            "tokenize_chunk": round((chunked - model_loaded) * 1000.0, 1),
            "select": round((selected_done - chunked) * 1000.0, 1),
            "embed_selected": round((embedded - embedded_started) * 1000.0, 1),
            "embed_queries": round((queries_done - embedded) * 1000.0, 1),
            "evaluate": round((evaluated - queries_done) * 1000.0, 1),
            "save": round((saved - evaluated) * 1000.0, 1),
            "total": round((saved - started) * 1000.0, 1),
        },
        "budget_reports": budget_reports,
    }

    print("Acclorite M11 compact pretrained-index probe")
    print("─" * 76)
    print(f"Model:              {args.model}")
    print(f"Device:             {args.device}")
    print(f"Pages discovered:   {len(source_pages)}")
    print(f"Indexed commands:   {report['indexed_commands']}")
    print(f"Full chunks:        {len(all_chunks)}")
    print(f"Selected passages:  {len(selected)} ({100.0 * len(selected) / max(1, len(all_chunks)):.1f}% of full C index)")
    print(f"Embedding dims:     {selected_embeddings.shape[1]}")
    print(f"Peak RSS:           {report['peak_rss_mib']:.1f} MiB")
    timing = report["timing_ms"]
    print(
        "Probe timing:       "
        f"discover {timing['discover']:.1f} ms · plain {timing['plain_prepare']:.1f} ms · "
        f"model {timing['model_load']:.1f} ms · chunk {timing['tokenize_chunk']:.1f} ms · "
        f"select {timing['select']:.1f} ms · embed {timing['embed_selected']:.1f} ms · "
        f"queries {timing['embed_queries']:.1f} ms · evaluate {timing['evaluate']:.1f} ms · "
        f"total {timing['total']:.1f} ms"
    )
    print()
    for budget_report in budget_reports:
        _print_budget(budget_report)

    if args.index_out:
        print(f"Reusable research index: {args.index_out}")
    if args.json_out:
        args.json_out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
