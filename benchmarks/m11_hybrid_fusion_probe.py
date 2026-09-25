#!/usr/bin/env python3
"""M11 Experiment E: semantic candidate retrieval + deterministic rank fusion.

Research tooling only. Reuses Experiment D's persisted compact embedding index,
embeds only benchmark queries, asks the existing Acclorite CLI for its normal
Top-10 deterministic ranking, and evaluates fixed reciprocal-rank fusion rules.

No document/passages are re-embedded. The production SearchEngine and schemas are
not modified; this probe measures whether the two independent rankings contain
complementary signal before any candidate-injection architecture is attempted.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path
from typing import Iterable

import m11_embedding_probe as embedprobe

DEFAULT_BUDGETS = (1, 2, 4)
DEFAULT_SEMANTIC_DEPTH = 20
DEFAULT_RRF_K = 60.0


def accepted(case: dict) -> set[str]:
    expected = case.get("expect", {})
    if not isinstance(expected, dict):
        return set()
    return set(expected.get("top1_any", [])) | set(expected.get("top3_any", []))


def reciprocal_rank_fusion(
    semantic: list[str],
    deterministic: list[str],
    *,
    semantic_weight: float = 1.0,
    deterministic_weight: float = 1.0,
    k: float = DEFAULT_RRF_K,
    limit: int = 10,
) -> list[str]:
    """Fuse two ranked lists without assuming their raw score scales are comparable."""
    if k <= 0 or semantic_weight < 0 or deterministic_weight < 0:
        raise ValueError("invalid RRF parameters")
    sem_rank = {name: i for i, name in enumerate(semantic, 1)}
    det_rank = {name: i for i, name in enumerate(deterministic, 1)}
    names = set(sem_rank) | set(det_rank)

    def key(name: str):
        score = 0.0
        if name in sem_rank:
            score += semantic_weight / (k + sem_rank[name])
        if name in det_rank:
            score += deterministic_weight / (k + det_rank[name])
        # Stable, inspectable tie-breaks: stronger semantic rank, then deterministic rank, then name.
        return (-score, sem_rank.get(name, 10**9), det_rank.get(name, 10**9), name)

    return sorted(names, key=key)[:limit]


def metric_hit(ranking: list[str], expected: set[str], depth: int) -> bool:
    return any(name in expected for name in ranking[:depth])


def summarize(rankings: list[list[str]], cases: list[dict]) -> dict:
    available = 0
    top1 = top3 = top10 = 0
    for ranking, case in zip(rankings, cases):
        want = accepted(case)
        if not want:
            continue
        available += 1
        top1 += metric_hit(ranking, want, 1)
        top3 += metric_hit(ranking, want, 3)
        top10 += metric_hit(ranking, want, 10)
    return {"cases": available, "top1": top1, "top3": top3, "top10": top10}


def union_coverage(semantic: list[str], deterministic: list[str], expected: set[str], semantic_depth: int) -> bool:
    candidates = set(semantic[:semantic_depth]) | set(deterministic[:10])
    return bool(candidates & expected)


def run_acclorite(binary: Path, query: str, timeout: float) -> tuple[list[str], str]:
    command = [str(binary), "--json", query]
    try:
        completed = subprocess.run(
            command,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return [], f"timeout after {timeout:.1f}s"

    if completed.returncode not in (0, 1):
        detail = completed.stderr.strip() or completed.stdout.strip()
        return [], f"exit {completed.returncode}: {detail[:180]}"
    try:
        payload = json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        return [], f"invalid JSON: {exc}"
    raw = payload.get("results", [])
    if not isinstance(raw, list):
        return [], "JSON results is not a list"
    ranking = [
        str(item.get("command", ""))
        for item in raw
        if isinstance(item, dict) and isinstance(item.get("command"), str) and item.get("command")
    ]
    return ranking[:10], ""


def load_index(path: Path, np):
    try:
        data = np.load(path, allow_pickle=False)
    except Exception as exc:
        print(f"error: cannot load compact index {path}: {exc}", file=sys.stderr)
        raise SystemExit(2) from exc
    required = {"embeddings", "commands", "selection_rank", "model"}
    missing = required - set(data.files)
    if missing:
        print(f"error: compact index is missing fields: {', '.join(sorted(missing))}", file=sys.stderr)
        raise SystemExit(2)
    embeddings = data["embeddings"].astype(np.float32, copy=False)
    commands = [str(item) for item in data["commands"]]
    selection_rank = data["selection_rank"].astype(np.int32, copy=False)
    model_values = data["model"]
    model_name = str(model_values[0]) if len(model_values) else embedprobe.DEFAULT_MODEL
    if embeddings.ndim != 2 or len(commands) != embeddings.shape[0] or len(selection_rank) != embeddings.shape[0]:
        print("error: inconsistent compact-index array lengths", file=sys.stderr)
        raise SystemExit(2)
    return data, embeddings, commands, selection_rank, model_name


def semantic_rankings(np, embeddings, commands: list[str], selection_rank, query_embeddings, budget: int, depth: int):
    mask = selection_rank <= budget
    budget_embeddings = embeddings[mask]
    budget_commands = [command for command, keep in zip(commands, mask.tolist()) if keep]
    unique_commands = sorted(set(budget_commands))
    command_ids = {name: i for i, name in enumerate(unique_commands)}
    row_ids = np.asarray([command_ids[name] for name in budget_commands], dtype=np.int32)

    rankings: list[list[str]] = []
    for query in query_embeddings:
        passage_scores = budget_embeddings @ query
        best = np.full(len(unique_commands), -np.inf, dtype=np.float32)
        np.maximum.at(best, row_ids, passage_scores)
        order = np.argsort(-best, kind="stable")[:depth]
        rankings.append([unique_commands[int(i)] for i in order])
    return rankings


def print_metric(label: str, metric: dict) -> None:
    n = metric["cases"]
    if not n:
        print(f"    {label:<20} no evaluable cases")
        return
    print(
        f"    {label:<20} "
        f"Top-1 {metric['top1']}/{n} {100.0 * metric['top1'] / n:5.1f}% · "
        f"Top-3 {metric['top3']}/{n} {100.0 * metric['top3'] / n:5.1f}% · "
        f"Top-10 {metric['top10']}/{n} {100.0 * metric['top10'] / n:5.1f}%"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("./build/acclorite"))
    parser.add_argument("--index", type=Path, default=Path("benchmark-m11-compact-index.npz"))
    parser.add_argument("--corpus", action="append", type=Path, default=[])
    parser.add_argument("--budget", action="append", type=int, default=[])
    parser.add_argument("--semantic-depth", type=int, default=DEFAULT_SEMANTIC_DEPTH)
    parser.add_argument("--rrf-k", type=float, default=DEFAULT_RRF_K)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--cache-dir", type=Path)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    budgets = sorted(set(args.budget or DEFAULT_BUDGETS))
    if any(b < 1 for b in budgets) or args.semantic_depth < 10 or args.rrf_k <= 0 or args.timeout <= 0:
        print("error: invalid budget/depth/RRF/timeout parameter", file=sys.stderr)
        return 2
    if not args.binary.exists():
        print(f"error: Acclorite binary not found: {args.binary}", file=sys.stderr)
        return 2

    try:
        import numpy as np
    except ImportError as exc:  # pragma: no cover
        print("error: Experiment E requires NumPy through the research embedding stack", file=sys.stderr)
        raise SystemExit(3) from exc

    corpus_paths = args.corpus or [
        Path("benchmarks/corpus-m11-research-v1.json"),
        Path("benchmarks/corpus-m11-heldout-v1.json"),
    ]
    corpora = embedprobe._load_corpora(corpus_paths)
    cases_flat: list[dict] = []
    corpus_ranges: list[tuple[int, int]] = []
    queries: list[str] = []
    for corpus in corpora:
        begin = len(cases_flat)
        for case in corpus.get("cases", []):
            cases_flat.append(case)
            queries.append(str(case.get("query", "")))
        corpus_ranges.append((begin, len(cases_flat)))

    started = time.perf_counter()
    data, embeddings, commands, selection_rank, model_name = load_index(args.index, np)
    loaded_index = time.perf_counter()
    if max(budgets) > int(selection_rank.max(initial=0)):
        print("error: requested passage budget exceeds passages stored in compact index", file=sys.stderr)
        return 2

    model = embedprobe._load_sentence_transformer(model_name, False, args.cache_dir, args.device)
    model_loaded = time.perf_counter()
    query_embeddings = model.encode(
        queries,
        batch_size=32,
        show_progress_bar=False,
        convert_to_numpy=True,
        normalize_embeddings=True,
    ).astype(np.float32, copy=False)
    queries_embedded = time.perf_counter()

    deterministic: list[list[str]] = []
    deterministic_errors: list[str] = []
    for query in queries:
        ranking, error = run_acclorite(args.binary, query, args.timeout)
        deterministic.append(ranking)
        deterministic_errors.append(error)
    deterministic_done = time.perf_counter()

    report_budgets: list[dict] = []
    for budget in budgets:
        semantic = semantic_rankings(
            np, embeddings, commands, selection_rank, query_embeddings, budget, args.semantic_depth
        )
        corpus_reports: list[dict] = []
        for corpus, (begin, end) in zip(corpora, corpus_ranges):
            cases = cases_flat[begin:end]
            sem = semantic[begin:end]
            det = deterministic[begin:end]
            fused_equal = [
                reciprocal_rank_fusion(s, d, k=args.rrf_k, limit=10)
                for s, d in zip(sem, det)
            ]
            fused_sem2 = [
                reciprocal_rank_fusion(s, d, semantic_weight=2.0, deterministic_weight=1.0, k=args.rrf_k, limit=10)
                for s, d in zip(sem, det)
            ]
            union_hits = sum(
                union_coverage(s, d, accepted(case), args.semantic_depth)
                for s, d, case in zip(sem, det, cases)
                if accepted(case)
            )
            evaluable = sum(bool(accepted(case)) for case in cases)
            case_rows = []
            for case, s, d, eq, sem2 in zip(cases, sem, det, fused_equal, fused_sem2):
                want = accepted(case)
                case_rows.append({
                    "id": str(case.get("id", "")),
                    "accepted": sorted(want),
                    "semantic": s[:10],
                    "deterministic": d[:10],
                    "rrf_equal": eq,
                    "rrf_semantic_2x": sem2,
                })
            corpus_reports.append({
                "corpus": str(corpus.get("name", "")),
                "semantic": summarize(sem, cases),
                "deterministic": summarize(det, cases),
                "rrf_equal": summarize(fused_equal, cases),
                "rrf_semantic_2x": summarize(fused_sem2, cases),
                "candidate_union_coverage": {"hits": union_hits, "cases": evaluable},
                "cases": case_rows,
            })
        report_budgets.append({"budget": budget, "corpora": corpus_reports})
    evaluated = time.perf_counter()

    error_count = sum(bool(error) for error in deterministic_errors)
    report = {
        "experiment": "m11-hybrid-rank-fusion-v1",
        "index": str(args.index),
        "model": model_name,
        "device": args.device,
        "index_passages": int(embeddings.shape[0]),
        "embedding_dimensions": int(embeddings.shape[1]),
        "semantic_depth": args.semantic_depth,
        "rrf_k": args.rrf_k,
        "deterministic_errors": error_count,
        "timing_ms": {
            "index_load": round((loaded_index - started) * 1000.0, 1),
            "model_load": round((model_loaded - loaded_index) * 1000.0, 1),
            "embed_queries": round((queries_embedded - model_loaded) * 1000.0, 1),
            "deterministic_queries": round((deterministic_done - queries_embedded) * 1000.0, 1),
            "evaluate": round((evaluated - deterministic_done) * 1000.0, 1),
            "total": round((evaluated - started) * 1000.0, 1),
        },
        "budgets": report_budgets,
    }

    print("Acclorite M11 hybrid semantic/deterministic fusion probe")
    print("─" * 76)
    print(f"Index:              {args.index}")
    print(f"Model:              {model_name}")
    print(f"Stored passages:    {embeddings.shape[0]}")
    print(f"Semantic depth:     {args.semantic_depth}")
    print(f"Deterministic errors: {error_count}")
    timing = report["timing_ms"]
    print(
        "Probe timing:       "
        f"index {timing['index_load']:.1f} ms · model {timing['model_load']:.1f} ms · "
        f"queries {timing['embed_queries']:.1f} ms · deterministic {timing['deterministic_queries']:.1f} ms · "
        f"evaluate {timing['evaluate']:.1f} ms · total {timing['total']:.1f} ms"
    )
    print()
    for budget_report in report_budgets:
        print(f"Passage budget: {budget_report['budget']}/command")
        for corpus_report in budget_report["corpora"]:
            union = corpus_report["candidate_union_coverage"]
            print(f"  {corpus_report['corpus']}")
            print_metric("semantic", corpus_report["semantic"])
            print_metric("deterministic", corpus_report["deterministic"])
            print_metric("RRF equal", corpus_report["rrf_equal"])
            print_metric("RRF semantic 2x", corpus_report["rrf_semantic_2x"])
            if union["cases"]:
                print(
                    f"    candidate union      expected in semantic@{args.semantic_depth} ∪ deterministic@10: "
                    f"{union['hits']}/{union['cases']} {100.0 * union['hits'] / union['cases']:.1f}%"
                )
        print()

    if args.json_out:
        args.json_out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    data.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
