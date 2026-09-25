#!/usr/bin/env python3
"""M11 Experiment I: cross-system agreement arbitration.

Research tooling only. Reuses Experiment D's compact semantic index and
Experiment G's conservative rank-derived semantic support. The production
SearchEngine is not changed again.

Experiment H showed that Acclorite's existing ambiguity/confidence states are
not calibrated correctness probabilities: frozen M10 answers can be correct
while marked competitive or low-confidence. This probe therefore asks a
narrower question: can disagreement between the deterministic Top-1 and the
semantic retriever decide when the conservative-supported ranking should be
shown?

The policies are declared before results and use semantic rank only, never raw
cosine thresholds:

  anchor-top20
      Keep deterministic Top-1 whenever it appears anywhere in semantic@20.
      Otherwise show the conservative-supported ranking.

  anchor-top10
      Same, but the deterministic Top-1 only needs semantic@10 protection.

  rank-gap-5
      Show the supported ranking only when its Top-1 is present in semantic@20
      and outranks the deterministic Top-1 by at least five semantic positions.

  rank-gap-10
      Same, but require a ten-position semantic-rank advantage.

These are arbitration rules only. Semantic retrieval still cannot establish
command existence, provenance, syntax, invocation completeness, or safety.
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

import m11_embedding_probe as embedprobe
import m11_hybrid_fusion_probe as fusion
import m11_ranked_injection_probe as ranked
import m11_confidence_gate_probe as hprobe

DEFAULT_BUDGET = 4
DEFAULT_SEMANTIC_DEPTH = 20
POLICIES = ("anchor-top20", "anchor-top10", "rank-gap-5", "rank-gap-10")


def semantic_rank(ranking: list[str], command: str, depth: int) -> int:
    """Return one-based semantic rank, or depth+1 when absent."""
    if not command:
        return depth + 1
    try:
        return ranking[:depth].index(command) + 1
    except ValueError:
        return depth + 1


def policy_activates(
    baseline: list[str],
    supported: list[str],
    semantic: list[str],
    policy: str,
    depth: int,
) -> bool:
    if not baseline or not supported:
        return False
    base = baseline[0]
    replacement = supported[0]
    if replacement == base:
        return False

    base_rank = semantic_rank(semantic, base, depth)
    replacement_rank = semantic_rank(semantic, replacement, depth)

    if policy == "anchor-top20":
        return base_rank > min(20, depth)
    if policy == "anchor-top10":
        return base_rank > min(10, depth)
    if policy == "rank-gap-5":
        return replacement_rank <= depth and (base_rank - replacement_rank) >= 5
    if policy == "rank-gap-10":
        return replacement_rank <= depth and (base_rank - replacement_rank) >= 10
    raise ValueError(f"unknown policy: {policy}")


def choose_rankings(
    baseline: list[list[str]],
    supported: list[list[str]],
    semantic: list[list[str]],
    policy: str,
    depth: int,
) -> tuple[list[list[str]], list[bool]]:
    rows: list[list[str]] = []
    activated: list[bool] = []
    for base, support, sem in zip(baseline, supported, semantic):
        use_supported = policy_activates(base, support, sem, policy, depth)
        activated.append(use_supported)
        rows.append(support if use_supported else base)
    return rows, activated


def switch_rows(
    cases: list[dict],
    baseline: list[list[str]],
    supported: list[list[str]],
    semantic: list[list[str]],
    activated: list[bool],
    depth: int,
) -> list[dict]:
    rows: list[dict] = []
    for case, base, support, sem, active in zip(cases, baseline, supported, semantic, activated):
        if not active:
            continue
        base_name = base[0] if base else ""
        support_name = support[0] if support else ""
        rows.append({
            "id": str(case.get("id", "")),
            "baseline_top1": base_name,
            "supported_top1": support_name,
            "baseline_semantic_rank": semantic_rank(sem, base_name, depth),
            "supported_semantic_rank": semantic_rank(sem, support_name, depth),
            "accepted": sorted(fusion.accepted(case)),
        })
    return rows


def print_metric(label: str, metric: dict) -> None:
    n = metric["cases"]
    if not n:
        print(f"    {label:<23} no evaluable cases")
        return
    print(
        f"    {label:<23} "
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

    if args.budget < 1 or args.semantic_depth < 20 or args.semantic_depth > 32 or args.timeout <= 0:
        print("error: invalid budget/depth/timeout parameter", file=sys.stderr)
        return 2
    if not args.runner.exists():
        print(f"error: Experiment I runner not found: {args.runner}", file=sys.stderr)
        return 2

    try:
        import numpy as np
    except ImportError as exc:  # pragma: no cover
        print("error: Experiment I requires NumPy through the research embedding stack", file=sys.stderr)
        raise SystemExit(3) from exc

    corpus_paths = args.corpus or [
        Path("benchmarks/corpus-m11-research-v1.json"),
        Path("benchmarks/corpus-m11-heldout-v1.json"),
        Path("benchmarks/corpus-m11-transfer-v1.json"),
        Path("benchmarks/corpus-v1.json"),
        Path("benchmarks/corpus-human-v1.json"),
        Path("benchmarks/corpus-actionable-v5.json"),
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
        ranking, _meta, error = hprobe.run_diagnostic(args.runner, query, [], args.timeout)
        baseline.append(ranking)
        baseline_errors.append(error)
    baseline_done = time.perf_counter()

    supported: list[list[str]] = []
    supported_errors: list[str] = []
    for query, sem in zip(queries, semantic):
        hints = ranked.ranked_hints(sem, "conservative-ranked", args.semantic_depth)
        ranking, _meta, error = hprobe.run_diagnostic(args.runner, query, hints, args.timeout)
        supported.append(ranking)
        supported_errors.append(error)
    supported_done = time.perf_counter()

    corpus_reports: list[dict] = []
    for corpus, (begin, end) in zip(corpora, corpus_ranges):
        cases = cases_flat[begin:end]
        sem = semantic[begin:end]
        base = baseline[begin:end]
        support = supported[begin:end]
        policies: dict[str, dict] = {}
        for policy in POLICIES:
            rows, activated = choose_rankings(base, support, sem, policy, args.semantic_depth)
            policies[policy] = {
                "metric": fusion.summarize(rows, cases),
                "activated": sum(activated),
                "recoveries": {
                    "top1": hprobe.recovery_ids(base, rows, cases, 1),
                    "top3": hprobe.recovery_ids(base, rows, cases, 3),
                    "top10": hprobe.recovery_ids(base, rows, cases, 10),
                },
                "regressions": {
                    "top1": hprobe.regression_ids(base, rows, cases, 1),
                    "top3": hprobe.regression_ids(base, rows, cases, 3),
                    "top10": hprobe.regression_ids(base, rows, cases, 10),
                },
                "switches": switch_rows(cases, base, support, sem, activated, args.semantic_depth),
            }
        corpus_reports.append({
            "corpus": str(corpus.get("name", "")),
            "semantic": fusion.summarize(sem, cases),
            "baseline": fusion.summarize(base, cases),
            "conservative_always": fusion.summarize(support, cases),
            "policies": policies,
        })
    evaluated = time.perf_counter()

    report = {
        "experiment": "m11-cross-system-agreement-arbitration-v1",
        "index": str(args.index),
        "model": model_name,
        "device": args.device,
        "passage_budget": args.budget,
        "semantic_depth": args.semantic_depth,
        "semantic_policy": "conservative-ranked",
        "baseline_errors": sum(bool(error) for error in baseline_errors),
        "supported_errors": sum(bool(error) for error in supported_errors),
        "timing_ms": {
            "index_load": round((index_loaded - started) * 1000.0, 1),
            "model_load": round((model_loaded - index_loaded) * 1000.0, 1),
            "query_embedding": round((queries_embedded - model_loaded) * 1000.0, 1),
            "baseline": round((baseline_done - queries_embedded) * 1000.0, 1),
            "supported": round((supported_done - baseline_done) * 1000.0, 1),
            "evaluate": round((evaluated - supported_done) * 1000.0, 1),
            "total": round((evaluated - started) * 1000.0, 1),
        },
        "corpora": corpus_reports,
    }

    print("Acclorite M11 cross-system agreement arbitration probe")
    print("─" * 76)
    print(f"Index:              {args.index}")
    print(f"Model:              {model_name}")
    print(f"Passage budget:     {args.budget}/command")
    print(f"Semantic depth:     {args.semantic_depth}")
    print("Semantic policy:    conservative-ranked")
    print(f"Baseline errors:    {report['baseline_errors']}")
    print(f"Supported errors:   {report['supported_errors']}")
    tm = report["timing_ms"]
    print(
        "Probe timing:       "
        f"index {tm['index_load']:.1f} ms · model {tm['model_load']:.1f} ms · "
        f"queries {tm['query_embedding']:.1f} ms · baseline {tm['baseline']:.1f} ms · "
        f"supported {tm['supported']:.1f} ms · evaluate {tm['evaluate']:.1f} ms · total {tm['total']:.1f} ms"
    )

    for corpus in corpus_reports:
        print(f"\n{corpus['corpus']}")
        print_metric("semantic", corpus["semantic"])
        print_metric("baseline", corpus["baseline"])
        print_metric("conservative-always", corpus["conservative_always"])
        for policy in POLICIES:
            item = corpus["policies"][policy]
            print_metric(policy, item["metric"])
            rec = item["recoveries"]
            reg = item["regressions"]
            print(
                f"      activated {item['activated']}/{item['metric']['cases']} · "
                f"Δ new 1/3/10={len(rec['top1'])}/{len(rec['top3'])}/{len(rec['top10'])} · "
                f"regress 1/3/10={len(reg['top1'])}/{len(reg['top3'])}/{len(reg['top10'])}"
            )
            if reg["top1"]:
                print(f"      Top-1 regressions: {', '.join(reg['top1'])}")

    if args.json_out:
        args.json_out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    data.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
