#!/usr/bin/env python3
"""M11 Experiment G: bounded ranked semantic support inside deterministic ranking.

Research tooling only. Reuses Experiment D's persisted compact semantic index,
embeds benchmark queries, and passes semantic Top-N command identities to the
Experiment F runner together with a predeclared rank-derived relevance floor.
SearchEngine still requires ordinary local source substantiation before a hint
can enter ranking. The relevance floor is not provenance, syntax, or safety
authority, and is capped below the exact-name semantic tier.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

import m11_embedding_probe as embedprobe
import m11_hybrid_fusion_probe as fusion

DEFAULT_BUDGET = 4
DEFAULT_SEMANTIC_DEPTH = 20

# Predeclared before Experiment G results. Do not tune these against transfer-v1.
POLICIES = {
    "plausible-uniform": lambda rank: 0.70,
    "conservative-ranked": lambda rank: 0.84 if rank == 1 else (0.80 if rank <= 3 else (0.74 if rank <= 10 else 0.70)),
    "tiered-ranked": lambda rank: 0.88 if rank == 1 else (0.84 if rank <= 3 else (0.78 if rank <= 10 else 0.70)),
}


def ranked_hints(commands: list[str], policy: str, depth: int) -> list[tuple[str, float]]:
    schedule = POLICIES[policy]
    return [(command, schedule(rank)) for rank, command in enumerate(commands[:depth], start=1)]


def run_runner(
    runner: Path,
    query: str,
    hints: list[tuple[str, float]],
    timeout: float,
) -> tuple[list[str], str]:
    command = [str(runner)]
    for name, floor in hints:
        command.extend(["--ranked-hint", name, f"{floor:.6f}"])
    command.extend(["--", query])
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
    return [line.strip() for line in completed.stdout.splitlines() if line.strip()][:10], ""


def summarize_delta(baseline: list[list[str]], ranked: list[list[str]], cases: list[dict]) -> dict:
    out = {
        "cases": 0,
        "new_top1": 0,
        "new_top3": 0,
        "new_top10": 0,
        "top1_regressions": 0,
        "top3_regressions": 0,
        "top10_regressions": 0,
    }
    for base, new, case in zip(baseline, ranked, cases):
        accepted = fusion.accepted(case)
        if not accepted:
            continue
        out["cases"] += 1
        for depth, suffix in ((1, "top1"), (3, "top3"), (10, "top10")):
            base_hit = fusion.metric_hit(base, accepted, depth)
            new_hit = fusion.metric_hit(new, accepted, depth)
            out[f"new_{suffix}"] += int((not base_hit) and new_hit)
            out[f"{suffix}_regressions"] += int(base_hit and (not new_hit))
    return out


def print_metric(label: str, metric: dict) -> None:
    n = metric["cases"]
    if not n:
        print(f"    {label:<22} no evaluable cases")
        return
    print(
        f"    {label:<22} "
        f"Top-1 {metric['top1']}/{n} {100.0 * metric['top1'] / n:5.1f}% · "
        f"Top-3 {metric['top3']}/{n} {100.0 * metric['top3'] / n:5.1f}% · "
        f"Top-10 {metric['top10']}/{n} {100.0 * metric['top10'] / n:5.1f}%"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, default=Path("./build/acclorite_m11_injection_runner"))
    parser.add_argument("--index", type=Path, default=Path("benchmark-m11-compact-index.npz"))
    parser.add_argument("--corpus", action="append", type=Path, default=[])
    parser.add_argument("--budget", type=int, default=DEFAULT_BUDGET)
    parser.add_argument("--semantic-depth", type=int, default=DEFAULT_SEMANTIC_DEPTH)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--cache-dir", type=Path)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    if args.budget < 1 or args.semantic_depth < 10 or args.semantic_depth > 32 or args.timeout <= 0:
        print("error: invalid budget/depth/timeout parameter", file=sys.stderr)
        return 2
    if not args.runner.exists():
        print(f"error: Experiment G runner not found: {args.runner}", file=sys.stderr)
        return 2

    try:
        import numpy as np
    except ImportError as exc:  # pragma: no cover
        print("error: Experiment G requires NumPy through the research embedding stack", file=sys.stderr)
        raise SystemExit(3) from exc

    corpus_paths = args.corpus or [
        Path("benchmarks/corpus-m11-research-v1.json"),
        Path("benchmarks/corpus-m11-heldout-v1.json"),
        Path("benchmarks/corpus-m11-transfer-v1.json"),
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
    data, embeddings, commands, selection_rank, model_name = fusion.load_index(args.index, np)
    index_loaded = time.perf_counter()
    if args.budget > int(selection_rank.max(initial=0)):
        print("error: requested passage budget exceeds passages stored in compact index", file=sys.stderr)
        data.close()
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

    semantic = fusion.semantic_rankings(
        np, embeddings, commands, selection_rank, query_embeddings, args.budget, args.semantic_depth
    )

    baseline: list[list[str]] = []
    baseline_errors: list[str] = []
    for query in queries:
        ranking, error = run_runner(args.runner, query, [], args.timeout)
        baseline.append(ranking)
        baseline_errors.append(error)
    baseline_done = time.perf_counter()

    policy_rankings: dict[str, list[list[str]]] = {}
    policy_errors: dict[str, list[str]] = {}
    for policy in POLICIES:
        rows: list[list[str]] = []
        errors: list[str] = []
        for query, sem in zip(queries, semantic):
            ranking, error = run_runner(
                args.runner,
                query,
                ranked_hints(sem, policy, args.semantic_depth),
                args.timeout,
            )
            rows.append(ranking)
            errors.append(error)
        policy_rankings[policy] = rows
        policy_errors[policy] = errors
    evaluated = time.perf_counter()

    corpus_reports = []
    for corpus, (begin, end) in zip(corpora, corpus_ranges):
        cases = cases_flat[begin:end]
        sem = semantic[begin:end]
        base = baseline[begin:end]
        policies = {}
        for policy, all_rows in policy_rankings.items():
            rows = all_rows[begin:end]
            policies[policy] = {
                "metrics": fusion.summarize(rows, cases),
                "delta": summarize_delta(base, rows, cases),
            }
        corpus_reports.append({
            "corpus": str(corpus.get("name", "")),
            "semantic": fusion.summarize(sem, cases),
            "baseline": fusion.summarize(base, cases),
            "policies": policies,
            "cases": [
                {
                    "id": str(case.get("id", "")),
                    "accepted": sorted(fusion.accepted(case)),
                    "semantic_hints": sem_row[:args.semantic_depth],
                    "baseline": base_row,
                    "policies": {policy: policy_rankings[policy][begin + offset] for policy in POLICIES},
                }
                for offset, (case, sem_row, base_row) in enumerate(zip(cases, sem, base))
            ],
        })

    report = {
        "experiment": "m11-bounded-ranked-semantic-support-v1",
        "index": str(args.index),
        "model": model_name,
        "device": args.device,
        "passage_budget": args.budget,
        "semantic_depth": args.semantic_depth,
        "policies": {
            "plausible-uniform": {"ranks_1_20": 0.70},
            "conservative-ranked": {"rank_1": 0.84, "ranks_2_3": 0.80, "ranks_4_10": 0.74, "ranks_11_20": 0.70},
            "tiered-ranked": {"rank_1": 0.88, "ranks_2_3": 0.84, "ranks_4_10": 0.78, "ranks_11_20": 0.70},
        },
        "baseline_errors": sum(bool(x) for x in baseline_errors),
        "policy_errors": {k: sum(bool(x) for x in v) for k, v in policy_errors.items()},
        "timing_ms": {
            "index_load": round((index_loaded - started) * 1000.0, 1),
            "model_load": round((model_loaded - index_loaded) * 1000.0, 1),
            "embed_queries": round((queries_embedded - model_loaded) * 1000.0, 1),
            "baseline_queries": round((baseline_done - queries_embedded) * 1000.0, 1),
            "ranked_injection_and_evaluate": round((evaluated - baseline_done) * 1000.0, 1),
            "total": round((evaluated - started) * 1000.0, 1),
        },
        "corpora": corpus_reports,
    }

    print("Acclorite M11 bounded ranked-semantic-support probe")
    print("─" * 76)
    print(f"Index:              {args.index}")
    print(f"Model:              {model_name}")
    print(f"Passage budget:     {args.budget}/command")
    print(f"Semantic depth:     {args.semantic_depth}")
    print(f"Baseline errors:    {report['baseline_errors']}")
    print("Policy errors:      " + " · ".join(f"{k}={v}" for k, v in report["policy_errors"].items()))
    timing = report["timing_ms"]
    print(
        "Probe timing:       "
        f"index {timing['index_load']:.1f} ms · model {timing['model_load']:.1f} ms · "
        f"queries {timing['embed_queries']:.1f} ms · baseline {timing['baseline_queries']:.1f} ms · "
        f"ranked/eval {timing['ranked_injection_and_evaluate']:.1f} ms · total {timing['total']:.1f} ms"
    )
    print()

    for corpus_report in corpus_reports:
        print(corpus_report["corpus"])
        print_metric("semantic", corpus_report["semantic"])
        print_metric("baseline", corpus_report["baseline"])
        for policy in POLICIES:
            payload = corpus_report["policies"][policy]
            print_metric(policy, payload["metrics"])
            d = payload["delta"]
            print(
                f"      Δ new 1/3/10={d['new_top1']}/{d['new_top3']}/{d['new_top10']} · "
                f"regress 1/3/10={d['top1_regressions']}/{d['top3_regressions']}/{d['top10_regressions']}"
            )
        print()

    if args.json_out:
        args.json_out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    data.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
