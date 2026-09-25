#!/usr/bin/env python3
"""M11 Experiment H: confidence-gated semantic fallback.

Research tooling only. Reuses Experiment D's compact semantic index and
Experiment G's conservative rank-derived relevance schedule, but does not alter
SearchEngine ranking again. Each query first runs through ordinary deterministic
Acclorite and exposes its existing confidence assessment. The conservative
semantic-supported ranking is computed separately, then fixed predeclared gates
choose which ranking would be shown.

This tests whether pretrained semantics can behave as a fallback for uncertain
queries while leaving already-confident deterministic answers untouched.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from collections import Counter
from pathlib import Path

import m11_embedding_probe as embedprobe
import m11_hybrid_fusion_probe as fusion
import m11_ranked_injection_probe as ranked

DEFAULT_BUDGET = 4
DEFAULT_SEMANTIC_DEPTH = 20
HIGH_CONFIDENCE = 0.75  # Existing confidence_level_name() threshold.

# Declared before Experiment H results. Do not tune these against frozen M10.
GATES = (
    "uncertain-state",
    "non-high-confidence",
    "uncertain-or-non-high",
)


def run_diagnostic(
    runner: Path,
    query: str,
    hints: list[tuple[str, float]],
    timeout: float,
) -> tuple[list[str], dict, str]:
    command = [str(runner), "--diagnostic"]
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
        return [], {}, f"timeout after {timeout:.1f}s"

    if completed.returncode not in (0, 1):
        detail = completed.stderr.strip() or completed.stdout.strip()
        return [], {}, f"exit {completed.returncode}: {detail[:180]}"

    ranking: list[str] = []
    meta: dict[str, object] = {}
    for line in completed.stdout.splitlines():
        if line.startswith("@result\t"):
            value = line.split("\t", 1)[1].strip()
            if value:
                ranking.append(value)
        elif line.startswith("@ambiguity\t"):
            meta["ambiguity"] = line.split("\t", 1)[1].strip()
        elif line.startswith("@confidence\t"):
            try:
                meta["confidence"] = float(line.split("\t", 1)[1])
            except ValueError:
                return [], {}, "invalid diagnostic confidence"
        elif line.startswith("@level\t"):
            meta["level"] = line.split("\t", 1)[1].strip()

    if "ambiguity" not in meta or "confidence" not in meta or "level" not in meta:
        return [], {}, "missing diagnostic metadata"
    return ranking[:10], meta, ""


def gate_activates(meta: dict, gate: str) -> bool:
    state = str(meta.get("ambiguity", "no-result"))
    confidence = float(meta.get("confidence", 0.0))
    uncertain = state != "clear"
    non_high = confidence < HIGH_CONFIDENCE
    if gate == "uncertain-state":
        return uncertain
    if gate == "non-high-confidence":
        return non_high
    if gate == "uncertain-or-non-high":
        return uncertain or non_high
    raise ValueError(f"unknown gate: {gate}")


def choose_rankings(
    baseline: list[list[str]],
    supported: list[list[str]],
    metadata: list[dict],
    gate: str,
) -> tuple[list[list[str]], list[bool]]:
    out: list[list[str]] = []
    activated: list[bool] = []
    for base, sem, meta in zip(baseline, supported, metadata):
        use_semantic = gate_activates(meta, gate)
        activated.append(use_semantic)
        out.append(sem if use_semantic else base)
    return out, activated


def regression_ids(
    baseline: list[list[str]],
    ranking: list[list[str]],
    cases: list[dict],
    depth: int,
) -> list[str]:
    result = []
    for base, new, case in zip(baseline, ranking, cases):
        want = fusion.accepted(case)
        if not want:
            continue
        if fusion.metric_hit(base, want, depth) and not fusion.metric_hit(new, want, depth):
            result.append(str(case.get("id", "")))
    return result


def recovery_ids(
    baseline: list[list[str]],
    ranking: list[list[str]],
    cases: list[dict],
    depth: int,
) -> list[str]:
    result = []
    for base, new, case in zip(baseline, ranking, cases):
        want = fusion.accepted(case)
        if not want:
            continue
        if not fusion.metric_hit(base, want, depth) and fusion.metric_hit(new, want, depth):
            result.append(str(case.get("id", "")))
    return result


def calibration(metadata: list[dict], baseline: list[list[str]], cases: list[dict]) -> dict:
    buckets: dict[str, dict[str, int]] = {}
    for meta, ranking, case in zip(metadata, baseline, cases):
        want = fusion.accepted(case)
        if not want:
            continue
        state = str(meta.get("ambiguity", "no-result"))
        bucket = buckets.setdefault(state, {"cases": 0, "top1": 0, "top3": 0, "top10": 0})
        bucket["cases"] += 1
        bucket["top1"] += int(fusion.metric_hit(ranking, want, 1))
        bucket["top3"] += int(fusion.metric_hit(ranking, want, 3))
        bucket["top10"] += int(fusion.metric_hit(ranking, want, 10))
    return buckets


def print_metric(label: str, metric: dict) -> None:
    n = metric["cases"]
    if not n:
        print(f"    {label:<25} no evaluable cases")
        return
    print(
        f"    {label:<25} "
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
        print(f"error: Experiment H runner not found: {args.runner}", file=sys.stderr)
        return 2

    try:
        import numpy as np
    except ImportError as exc:  # pragma: no cover
        print("error: Experiment H requires NumPy through the research embedding stack", file=sys.stderr)
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
    baseline_meta: list[dict] = []
    baseline_errors: list[str] = []
    for query in queries:
        ranking, meta, error = run_diagnostic(args.runner, query, [], args.timeout)
        baseline.append(ranking)
        baseline_meta.append(meta)
        baseline_errors.append(error)
    baseline_done = time.perf_counter()

    supported: list[list[str]] = []
    supported_errors: list[str] = []
    for query, sem in zip(queries, semantic):
        hints = ranked.ranked_hints(sem, "conservative-ranked", args.semantic_depth)
        ranking, _meta, error = run_diagnostic(args.runner, query, hints, args.timeout)
        supported.append(ranking)
        supported_errors.append(error)
    supported_done = time.perf_counter()

    corpus_reports = []
    for corpus, (begin, end) in zip(corpora, corpus_ranges):
        cases = cases_flat[begin:end]
        sem = semantic[begin:end]
        base = baseline[begin:end]
        meta = baseline_meta[begin:end]
        support = supported[begin:end]
        gates = {}
        for gate in GATES:
            rows, activated = choose_rankings(base, support, meta, gate)
            gates[gate] = {
                "metrics": fusion.summarize(rows, cases),
                "delta": ranked.summarize_delta(base, rows, cases),
                "activated": sum(activated),
                "recoveries": {
                    "top1": recovery_ids(base, rows, cases, 1),
                    "top3": recovery_ids(base, rows, cases, 3),
                    "top10": recovery_ids(base, rows, cases, 10),
                },
                "regressions": {
                    "top1": regression_ids(base, rows, cases, 1),
                    "top3": regression_ids(base, rows, cases, 3),
                    "top10": regression_ids(base, rows, cases, 10),
                },
            }
        corpus_reports.append({
            "corpus": str(corpus.get("name", "")),
            "semantic": fusion.summarize(sem, cases),
            "baseline": fusion.summarize(base, cases),
            "conservative_always": {
                "metrics": fusion.summarize(support, cases),
                "delta": ranked.summarize_delta(base, support, cases),
                "regressions": {
                    "top1": regression_ids(base, support, cases, 1),
                    "top3": regression_ids(base, support, cases, 3),
                    "top10": regression_ids(base, support, cases, 10),
                },
            },
            "confidence_calibration": calibration(meta, base, cases),
            "confidence_levels": dict(Counter(str(item.get("level", "unknown")) for item in meta)),
            "gates": gates,
            "cases": [
                {
                    "id": str(case.get("id", "")),
                    "accepted": sorted(fusion.accepted(case)),
                    "baseline": base_row,
                    "baseline_confidence": meta_row,
                    "semantic_hints": sem_row[:args.semantic_depth],
                    "conservative_ranked": support_row,
                }
                for case, base_row, meta_row, sem_row, support_row in zip(cases, base, meta, sem, support)
            ],
        })

    evaluated = time.perf_counter()
    report = {
        "experiment": "m11-confidence-gated-semantic-fallback-v1",
        "index": str(args.index),
        "model": model_name,
        "device": args.device,
        "passage_budget": args.budget,
        "semantic_depth": args.semantic_depth,
        "semantic_policy": "conservative-ranked",
        "high_confidence_threshold": HIGH_CONFIDENCE,
        "gates": list(GATES),
        "baseline_errors": sum(bool(x) for x in baseline_errors),
        "supported_errors": sum(bool(x) for x in supported_errors),
        "timing_ms": {
            "index_load": round((index_loaded - started) * 1000.0, 1),
            "model_load": round((model_loaded - index_loaded) * 1000.0, 1),
            "embed_queries": round((queries_embedded - model_loaded) * 1000.0, 1),
            "baseline_queries": round((baseline_done - queries_embedded) * 1000.0, 1),
            "supported_queries": round((supported_done - baseline_done) * 1000.0, 1),
            "evaluate": round((evaluated - supported_done) * 1000.0, 1),
            "total": round((evaluated - started) * 1000.0, 1),
        },
        "corpora": corpus_reports,
    }

    print("Acclorite M11 confidence-gated semantic fallback probe")
    print("─" * 76)
    print(f"Index:              {args.index}")
    print(f"Model:              {model_name}")
    print(f"Passage budget:     {args.budget}/command")
    print(f"Semantic depth:     {args.semantic_depth}")
    print("Semantic policy:    conservative-ranked")
    print(f"High confidence:    >= {HIGH_CONFIDENCE:.2f}")
    print(f"Baseline errors:    {report['baseline_errors']}")
    print(f"Supported errors:   {report['supported_errors']}")
    t = report["timing_ms"]
    print(
        "Probe timing:       "
        f"index {t['index_load']:.1f} ms · model {t['model_load']:.1f} ms · "
        f"queries {t['embed_queries']:.1f} ms · baseline {t['baseline_queries']:.1f} ms · "
        f"supported {t['supported_queries']:.1f} ms · evaluate {t['evaluate']:.1f} ms · total {t['total']:.1f} ms"
    )
    print()

    for corpus_report in corpus_reports:
        print(corpus_report["corpus"])
        print_metric("semantic", corpus_report["semantic"])
        print_metric("baseline", corpus_report["baseline"])
        print_metric("conservative-always", corpus_report["conservative_always"]["metrics"])
        always_reg = corpus_report["conservative_always"]["regressions"]
        if always_reg["top1"]:
            print("      always Top-1 regressions: " + ", ".join(always_reg["top1"]))
        for gate in GATES:
            payload = corpus_report["gates"][gate]
            print_metric(gate, payload["metrics"])
            d = payload["delta"]
            print(
                f"      activated {payload['activated']}/{d['cases']} · "
                f"Δ new 1/3/10={d['new_top1']}/{d['new_top3']}/{d['new_top10']} · "
                f"regress 1/3/10={d['top1_regressions']}/{d['top3_regressions']}/{d['top10_regressions']}"
            )
            if payload["regressions"]["top1"]:
                print("      Top-1 regressions: " + ", ".join(payload["regressions"]["top1"]))
        cal = corpus_report["confidence_calibration"]
        print("    baseline confidence calibration:")
        for state in ("clear", "competitive", "ambiguous", "low-confidence", "no-result"):
            if state not in cal:
                continue
            row = cal[state]
            n = row["cases"]
            print(
                f"      {state:<14} {n:2d} cases · "
                f"Top-1 {row['top1']}/{n} · Top-3 {row['top3']}/{n} · Top-10 {row['top10']}/{n}"
            )
        print()

    if args.json_out:
        args.json_out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    data.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
