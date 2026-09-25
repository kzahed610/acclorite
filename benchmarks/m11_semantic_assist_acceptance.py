#!/usr/bin/env python3
"""M11 semantic-assist acceptance analysis.

This is the closure analysis for the semantic-retrieval research track. It
consumes the already-generated Experiment G JSON and evaluates the product
boundary that remains after automatic arbitration and merged-list presentation
were both rejected:

  * the deterministic ranking is preserved byte-for-byte;
  * source-substantiated semantic-supported candidates are exposed in a
    separate alternatives channel;
  * candidates already present in deterministic Top-10 are omitted from that
    channel so it measures genuine recall expansion rather than duplication;
  * semantic alternatives never acquire deterministic rank positions.

Because the channels are separate, deterministic Top-1/Top-3/Top-10 cannot
regress. The useful metric is rescue coverage: when deterministic discovery
misses, how often does the semantic-assist channel contain an accepted command?
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def accepted(case: dict) -> set[str]:
    return {str(x) for x in case.get("accepted", []) if str(x)}


def dedupe(items: list[str]) -> list[str]:
    seen: set[str] = set()
    out: list[str] = []
    for item in items:
        if not item or item in seen:
            continue
        seen.add(item)
        out.append(item)
    return out


def semantic_alternatives(
    baseline: list[str], supported: list[str], *, baseline_depth: int = 10, limit: int = 5
) -> list[str]:
    """Return a separate semantic-assist channel containing genuinely new candidates.

    Any command already visible in deterministic Top-N is omitted. The baseline
    ordering itself is never modified.
    """
    visible = set(dedupe(baseline)[:baseline_depth])
    return [item for item in dedupe(supported) if item not in visible][:limit]


def hit(ranking: list[str], answers: set[str], depth: int) -> bool:
    return any(item in answers for item in ranking[:depth])


def summarize_baseline(rows: list[list[str]], cases: list[dict]) -> dict:
    return {
        "cases": len(cases),
        "top1": sum(hit(row, accepted(case), 1) for row, case in zip(rows, cases)),
        "top3": sum(hit(row, accepted(case), 3) for row, case in zip(rows, cases)),
        "top10": sum(hit(row, accepted(case), 10) for row, case in zip(rows, cases)),
    }


def rescue_summary(
    baseline: list[list[str]], alternatives: list[list[str]], cases: list[dict], baseline_depth: int
) -> dict:
    missed: list[str] = []
    rescued: dict[int, list[str]] = {1: [], 3: [], 5: []}
    for base, assist, case in zip(baseline, alternatives, cases):
        answers = accepted(case)
        if hit(base, answers, baseline_depth):
            continue
        case_id = str(case.get("id", ""))
        missed.append(case_id)
        for depth in rescued:
            if hit(assist, answers, depth):
                rescued[depth].append(case_id)
    return {
        "baseline_depth": baseline_depth,
        "misses": len(missed),
        "miss_ids": missed,
        "rescued_at_1": len(rescued[1]),
        "rescued_at_3": len(rescued[3]),
        "rescued_at_5": len(rescued[5]),
        "rescued_ids_at_1": rescued[1],
        "rescued_ids_at_3": rescued[3],
        "rescued_ids_at_5": rescued[5],
    }


def combined_discovery(baseline_metric: dict, rescue: dict, depth: int) -> int:
    key = {1: "top1", 3: "top3", 10: "top10"}[rescue["baseline_depth"]]
    return int(baseline_metric[key]) + int(rescue[f"rescued_at_{depth}"])


def print_baseline(metric: dict) -> None:
    n = metric["cases"]
    print(
        f"    deterministic       "
        f"Top-1 {metric['top1']}/{n} {100.0 * metric['top1'] / n:5.1f}% · "
        f"Top-3 {metric['top3']}/{n} {100.0 * metric['top3'] / n:5.1f}% · "
        f"Top-10 {metric['top10']}/{n} {100.0 * metric['top10'] / n:5.1f}%"
    )


def print_rescue(label: str, rescue: dict) -> None:
    misses = rescue["misses"]
    def pct(value: int) -> float:
        return 100.0 * value / misses if misses else 0.0
    print(
        f"    {label:<19} "
        f"misses {misses} · assist@1 {rescue['rescued_at_1']}/{misses} {pct(rescue['rescued_at_1']):5.1f}% · "
        f"assist@3 {rescue['rescued_at_3']}/{misses} {pct(rescue['rescued_at_3']):5.1f}% · "
        f"assist@5 {rescue['rescued_at_5']}/{misses} {pct(rescue['rescued_at_5']):5.1f}%"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--ranked-report",
        type=Path,
        default=Path("benchmark-m11-ranked-injection.json"),
        help="Experiment G JSON report containing per-case baseline and conservative-ranked lists",
    )
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--assist-limit", type=int, default=5)
    args = parser.parse_args()

    if args.assist_limit < 1:
        print("error: --assist-limit must be positive", file=sys.stderr)
        return 2
    if not args.ranked_report.exists():
        print(f"error: ranked report not found: {args.ranked_report}", file=sys.stderr)
        return 2

    try:
        source = json.loads(args.ranked_report.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"error: unable to read ranked report: {exc}", file=sys.stderr)
        return 2

    if source.get("experiment") != "m11-bounded-ranked-semantic-support-v1":
        print("error: input is not the Experiment G ranked-semantic-support report", file=sys.stderr)
        return 2

    reports: list[dict] = []
    for corpus in source.get("corpora", []):
        cases = list(corpus.get("cases", []))
        baseline = [list(case.get("baseline", [])) for case in cases]
        supported = [list(case.get("policies", {}).get("conservative-ranked", [])) for case in cases]
        alternatives = [
            semantic_alternatives(base, support, baseline_depth=10, limit=args.assist_limit)
            for base, support in zip(baseline, supported)
        ]
        baseline_metric = summarize_baseline(baseline, cases)
        primary_rescue = rescue_summary(baseline, alternatives, cases, 1)
        top3_rescue = rescue_summary(baseline, alternatives, cases, 3)
        top10_rescue = rescue_summary(baseline, alternatives, cases, 10)
        reports.append({
            "corpus": str(corpus.get("corpus", "")),
            "deterministic": baseline_metric,
            "semantic_alternative_limit": args.assist_limit,
            "primary_miss_rescue": primary_rescue,
            "top3_miss_rescue": top3_rescue,
            "top10_miss_rescue": top10_rescue,
            "combined_discovery": {
                "deterministic_top1_plus_assist1": combined_discovery(baseline_metric, primary_rescue, 1),
                "deterministic_top1_plus_assist3": combined_discovery(baseline_metric, primary_rescue, 3),
                "deterministic_top1_plus_assist5": combined_discovery(baseline_metric, primary_rescue, 5),
                "deterministic_top10_plus_assist1": combined_discovery(baseline_metric, top10_rescue, 1),
                "deterministic_top10_plus_assist3": combined_discovery(baseline_metric, top10_rescue, 3),
                "deterministic_top10_plus_assist5": combined_discovery(baseline_metric, top10_rescue, 5),
            },
        })

    report = {
        "analysis": "m11-semantic-assist-separate-channel-v2",
        "source_report": str(args.ranked_report),
        "source_experiment": source.get("experiment"),
        "policy": (
            "preserve deterministic ranking byte-for-byte; expose source-substantiated semantic-supported "
            "candidates in a separate alternatives channel; omit candidates already in deterministic Top-10"
        ),
        "deterministic_safety": (
            "Semantic assist never occupies deterministic rank positions, so deterministic Top-1/Top-3/Top-10 "
            "cannot regress by construction."
        ),
        "corpora": reports,
    }

    print("Acclorite M11 semantic-assist separate-channel acceptance analysis")
    print("─" * 76)
    print(f"Source report:       {args.ranked_report}")
    print("Policy:              deterministic ranking unchanged; semantic alternatives are a separate channel")
    print("Deduplication:       omit candidates already visible in deterministic Top-10")
    print(f"Assist depth:         {args.assist_limit}")
    print("Safety:              deterministic ranking cannot regress by construction")
    for corpus in reports:
        print(f"\n{corpus['corpus']}")
        print_baseline(corpus["deterministic"])
        print_rescue("primary-miss rescue", corpus["primary_miss_rescue"])
        print_rescue("Top-3-miss rescue", corpus["top3_miss_rescue"])
        print_rescue("Top-10-miss rescue", corpus["top10_miss_rescue"])
        combo = corpus["combined_discovery"]
        n = corpus["deterministic"]["cases"]
        print(
            "      discovery with unchanged deterministic Top-10 + semantic assist: "
            f"assist@1 {combo['deterministic_top10_plus_assist1']}/{n} · "
            f"assist@3 {combo['deterministic_top10_plus_assist3']}/{n} · "
            f"assist@5 {combo['deterministic_top10_plus_assist5']}/{n}"
        )

    if args.json_out:
        args.json_out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
