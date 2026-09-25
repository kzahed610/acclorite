#!/usr/bin/env python3
"""M11 research probe: rank commands using full local man-page text.

Evaluation tooling only. It never executes candidate commands and never fetches
network data. It reads local section 1/8 manual sources, tokenizes their static
text, and measures whether deterministic lexical retrieval over richer
documentation can recover commands missed by the M10 one-line catalog.
"""
from __future__ import annotations

import argparse
import bz2
import collections
import gzip
import json
import lzma
import math
import os
import re
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

TOKEN_RE = re.compile(r"[a-z0-9][a-z0-9+._-]*")
SECTION_RE = re.compile(r"^(?:1|8)[a-z0-9]*$")
SO_ALIAS_RE = re.compile(r"^\s*\.so\s+(\S+)\s*$", re.MULTILINE)
COMPRESSED_SUFFIXES = {".gz", ".bz2", ".xz", ".lzma", ".zst"}
STOPWORDS = {
    "a", "an", "and", "are", "as", "at", "be", "been", "being", "but", "by",
    "can", "do", "does", "for", "from", "had", "has", "have", "how", "i", "if",
    "in", "into", "is", "it", "its", "me", "my", "of", "on", "or", "so", "that",
    "the", "their", "them", "then", "there", "these", "they", "this", "those", "to",
    "was", "were", "what", "when", "where", "which", "who", "why", "will", "with",
    "without", "you", "your",
}


@dataclass
class Document:
    command: str
    counts: collections.Counter[str]
    length: int
    paths: list[str]


@dataclass
class SearchIndex:
    documents: dict[str, Document]
    document_frequency: collections.Counter[str]
    average_length: float
    token_count: int


def _stem(token: str) -> str:
    """Tiny deterministic morphology for retrieval only; not semantic inference."""
    if len(token) > 5 and token.endswith("ies"):
        return token[:-3] + "y"
    if len(token) > 6 and token.endswith("ing"):
        return token[:-3]
    if len(token) > 5 and token.endswith("ed"):
        return token[:-2]
    if len(token) > 4 and token.endswith("es"):
        return token[:-2]
    if len(token) > 3 and token.endswith("s") and not token.endswith("ss"):
        return token[:-1]
    return token


def tokenize(text: str) -> list[str]:
    tokens: list[str] = []
    for raw in TOKEN_RE.findall(text.lower()):
        token = raw.strip("._-")
        if len(token) < 2 or token in STOPWORDS or token.isdigit():
            continue
        tokens.append(_stem(token))
    return tokens


def man_roots() -> list[Path]:
    configured = os.environ.get("MANPATH", "")
    if configured:
        roots = [Path(item) for item in configured.split(":") if item]
        if roots:
            return roots
    roots = [
        Path("/usr/local/share/man"), Path("/usr/share/man"),
        Path("/usr/local/man"), Path("/usr/man"),
    ]
    if home := os.environ.get("HOME"):
        roots.append(Path(home) / ".local/share/man")
    return roots


def _section_dir(path: Path) -> bool:
    return path.name.startswith("man") and SECTION_RE.fullmatch(path.name[3:]) is not None


def _command_from_filename(path: Path) -> str | None:
    name = path.name
    suffix = path.suffix.lower()
    if suffix in COMPRESSED_SUFFIXES:
        name = name[: -len(suffix)]
    if "." not in name:
        return None
    command, section = name.rsplit(".", 1)
    return command if command and SECTION_RE.fullmatch(section) else None


def _read_text(path: Path) -> tuple[str | None, str | None]:
    suffix = path.suffix.lower()
    try:
        if suffix == ".gz":
            with gzip.open(path, "rt", encoding="utf-8", errors="replace") as handle:
                return handle.read(), None
        if suffix == ".bz2":
            with bz2.open(path, "rt", encoding="utf-8", errors="replace") as handle:
                return handle.read(), None
        if suffix in {".xz", ".lzma"}:
            with lzma.open(path, "rt", encoding="utf-8", errors="replace") as handle:
                return handle.read(), None
        if suffix == ".zst":
            return None, "zstd-unsupported"
        return path.read_text(encoding="utf-8", errors="replace"), None
    except (OSError, EOFError, lzma.LZMAError) as exc:
        return None, f"read-error:{type(exc).__name__}"


def discover_pages(roots: Iterable[Path]) -> list[Path]:
    pages: list[Path] = []
    seen: set[Path] = set()
    for root in roots:
        if not root.is_dir():
            continue
        try:
            section_dirs = [p for p in root.iterdir() if p.is_dir() and _section_dir(p)]
        except OSError:
            continue
        for directory in sorted(section_dirs):
            try:
                entries = list(directory.iterdir())
            except OSError:
                continue
            for entry in entries:
                if not (entry.is_file() or entry.is_symlink()) or _command_from_filename(entry) is None:
                    continue
                key = entry.absolute()
                if key not in seen:
                    seen.add(key)
                    pages.append(entry)
    return pages


def _alias_target(text: str) -> str | None:
    match = SO_ALIAS_RE.search(text)
    if not match:
        return None
    non_comment = [
        line.strip() for line in text.splitlines()
        if line.strip() and not line.lstrip().startswith('.\\"')
    ]
    if len(non_comment) != 1 or not non_comment[0].startswith(".so "):
        return None
    return Path(match.group(1)).name


def _read_resolving_alias(
    page: Path,
    page_by_name: dict[str, Path],
    *,
    max_hops: int = 4,
) -> tuple[str | None, str | None]:
    current = page
    visited: set[Path] = set()
    for _ in range(max_hops + 1):
        if current in visited:
            return None, "alias-cycle"
        visited.add(current)
        text, reason = _read_text(current)
        if text is None:
            return None, reason
        target = _alias_target(text)
        if target is None:
            return text, None
        next_page = page_by_name.get(target)
        if next_page is None:
            return None, "alias-target-missing"
        current = next_page
    return None, "alias-depth"


def build_documents(pages: Iterable[Path]) -> tuple[SearchIndex, collections.Counter[str]]:
    page_list = list(pages)
    page_by_name: dict[str, Path] = {}
    for page in page_list:
        page_by_name.setdefault(page.name, page)
        # Alias references usually omit compression suffixes.
        if page.suffix.lower() in COMPRESSED_SUFFIXES:
            page_by_name.setdefault(page.name[: -len(page.suffix)], page)

    documents: dict[str, Document] = {}
    skipped: collections.Counter[str] = collections.Counter()
    for page in page_list:
        command = _command_from_filename(page)
        if not command:
            continue
        text, reason = _read_resolving_alias(page, page_by_name)
        if text is None:
            skipped[reason or "unknown"] += 1
            continue
        tokens = tokenize(text)
        if not tokens:
            skipped["empty"] += 1
            continue
        counts = collections.Counter(tokens)
        if command not in documents:
            documents[command] = Document(command, counts, sum(counts.values()), [str(page)])
        else:
            documents[command].counts.update(counts)
            documents[command].length += sum(counts.values())
            documents[command].paths.append(str(page))

    document_frequency: collections.Counter[str] = collections.Counter()
    token_count = 0
    for document in documents.values():
        document_frequency.update(document.counts.keys())
        token_count += document.length
    average_length = token_count / len(documents) if documents else 0.0
    return SearchIndex(documents, document_frequency, average_length, token_count), skipped


def bm25_rank(query: str, index: SearchIndex, top_k: int) -> list[tuple[str, float]]:
    if not index.documents:
        return []
    query_terms = list(dict.fromkeys(tokenize(query)))
    if not query_terms:
        return []

    n_docs = len(index.documents)
    k1, b = 1.2, 0.75
    scored: list[tuple[str, float]] = []
    for document in index.documents.values():
        score = 0.0
        for term in query_terms:
            tf = document.counts.get(term, 0)
            if not tf:
                continue
            df = index.document_frequency.get(term, 0)
            idf = math.log(1.0 + ((n_docs - df + 0.5) / (df + 0.5)))
            denom = tf + k1 * (1.0 - b + b * (document.length / index.average_length))
            score += idf * ((tf * (k1 + 1.0)) / denom)
        if score > 0.0:
            scored.append((document.command, score))
    scored.sort(key=lambda item: (-item[1], item[0]))
    return scored[:top_k]


def evaluate(corpus: dict, index: SearchIndex, rank_limit: int) -> dict:
    cases: list[dict] = []
    top1 = top3 = top10 = available_cases = 0
    for case in corpus.get("cases", []):
        expected = case.get("expect", {})
        accepted = set(expected.get("top1_any", [])) | set(expected.get("top3_any", []))
        available = sorted(accepted & index.documents.keys())
        ranking = bm25_rank(str(case.get("query", "")), index, rank_limit)
        rank = next((i for i, (cmd, _) in enumerate(ranking, 1) if cmd in accepted), None)
        if available:
            available_cases += 1
            top1 += rank == 1
            top3 += rank is not None and rank <= 3
            top10 += rank is not None and rank <= 10
        cases.append({
            "id": str(case.get("id", "")),
            "query": str(case.get("query", "")),
            "accepted": sorted(accepted),
            "available_expected": available,
            "expected_rank": rank,
            "top": [{"command": command, "score": round(score, 6)} for command, score in ranking[:5]],
        })
    return {
        "corpus": str(corpus.get("name", "")),
        "documents": len(index.documents),
        "document_tokens": index.token_count,
        "average_document_tokens": round(index.average_length, 3),
        "available_cases": available_cases,
        "top1": top1,
        "top3": top3,
        "top10": top10,
        "cases": cases,
    }


def print_report(report: dict, skipped: collections.Counter[str]) -> None:
    available = report["available_cases"]
    print("Acclorite M11 full-man lexical probe")
    print("─" * 76)
    print(f"Corpus:              {report['corpus']}")
    print(f"Pages discovered:    {report['pages_discovered']}")
    print(f"Indexed commands:    {report['documents']}")
    print(f"Indexed tokens:      {report['document_tokens']}")
    print(f"Cases with expected local man page: {available}")
    if available:
        print(f"Top-1 recovery:      {report['top1']}/{available}  {100.0 * report['top1'] / available:.1f}%")
        print(f"Top-3 recovery:      {report['top3']}/{available}  {100.0 * report['top3'] / available:.1f}%")
        print(f"Top-10 recovery:     {report['top10']}/{available}  {100.0 * report['top10'] / available:.1f}%")
    if skipped:
        detail = ", ".join(f"{key}={value}" for key, value in sorted(skipped.items()))
        print(f"Skipped pages:       {detail}")
    timings = report.get("timings_ms", {})
    if timings:
        print(
            "Probe timing:        "
            f"discover {timings.get('discover', 0.0):.1f} ms · "
            f"index {timings.get('index', 0.0):.1f} ms · "
            f"evaluate {timings.get('evaluate', 0.0):.1f} ms · "
            f"total {timings.get('total', 0.0):.1f} ms"
        )
    print()
    for case in report["cases"]:
        if not case["available_expected"]:
            state = "NO-EXPECTED-MAN"
        elif case["expected_rank"] is None:
            state = "MISS"
        else:
            state = f"RANK {case['expected_rank']}"
        top = ", ".join(item["command"] for item in case["top"]) or "<none>"
        print(f"{case['id']:<30} {state:<16} top={top}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", type=Path, default=Path("benchmarks/corpus-m11-research-v1.json"))
    parser.add_argument("--top-k", type=int, default=100)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--man-root", action="append", type=Path, default=[])
    args = parser.parse_args()

    if args.top_k < 10:
        print("error: --top-k must be at least 10", file=sys.stderr)
        return 2
    try:
        corpus = json.loads(args.corpus.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"error: cannot read corpus: {exc}", file=sys.stderr)
        return 2

    started = time.perf_counter()
    roots = args.man_root or man_roots()
    discovery_started = time.perf_counter()
    pages = discover_pages(roots)
    discovery_done = time.perf_counter()
    index, skipped = build_documents(pages)
    index_done = time.perf_counter()
    if not index.documents:
        print("error: no readable section 1/8 local man pages found", file=sys.stderr)
        return 3
    report = evaluate(corpus, index, args.top_k)
    evaluation_done = time.perf_counter()
    report["man_roots"] = [str(root) for root in roots]
    report["pages_discovered"] = len(pages)
    report["skipped_pages"] = dict(sorted(skipped.items()))
    report["timings_ms"] = {
        "discover": round((discovery_done - discovery_started) * 1000.0, 3),
        "index": round((index_done - discovery_done) * 1000.0, 3),
        "evaluate": round((evaluation_done - index_done) * 1000.0, 3),
        "total": round((evaluation_done - started) * 1000.0, 3),
    }
    print_report(report, skipped)
    if args.json_out:
        try:
            args.json_out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        except OSError as exc:
            print(f"error: cannot write JSON report: {exc}", file=sys.stderr)
            return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
