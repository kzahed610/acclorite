#!/usr/bin/env python3
"""M11 Experiment F: semantic identity hints injected before deterministic ranking.

Research tooling only. Loads Experiment D's persisted compact semantic index,
embeds only benchmark queries, retrieves semantic Top-N command *names*, then
passes those names to a benchmark-only C++ runner. SearchEngine admits a hinted
identity only when an ordinary local KnowledgeSource independently substantiates
that exact command and scores its source-backed metadata against the original
query. Embedding scores never enter deterministic ranking or provenance.
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

DEFAULT_BUDGETS = (1, 2, 4)
DEFAULT_SEMANTIC_DEPTH = 20


def run_injection_runner(
    runner: Path,
    query: str,
    hints: list[str],
    timeout: float,
) -> tuple[list[str], str]:
    command = [str(runner)]
    for hint in hints:
        command.extend(["--hint", hint])
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
    ranking = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
    return ranking[:10], ""


def diagnostic_counts(
    semantic: list[list[str]],
    baseline: list[list[str]],
    injected: list[list[str]],
    cases: list[dict],
    semantic_depth: int,
) -> dict:
    hinted_cases = retained10 = promoted3 = rescued10 = regressions10 = 0
    evaluable = 0
    for sem, base, inj, case in zip(semantic, baseline, injected, cases):
        want = fusion.accepted(case)
        if not want:
            continue
        evaluable += 1
        sem_hit = fusion.metric_hit(sem, want, semantic_depth)
        base3 = fusion.metric_hit(base, want, 3)
        base10 = fusion.metric_hit(base, want, 10)
        inj3 = fusion.metric_hit(inj, want, 3)
        inj10 = fusion.metric_hit(inj, want, 10)
        if sem_hit:
            hinted_cases += 1
            retained10 += inj10
        promoted3 += (not base3) and inj3
        rescued10 += (not base10) and inj10
        regressions10 += base10 and (not inj10)
    return {
        "cases": evaluable,
        "semantic_candidate_hits": hinted_cases,
        "semantic_hits_retained_top10": retained10,
        "new_top3_recoveries": promoted3,
        "new_top10_recoveries": rescued10,
        "top10_regressions": regressions10,
    }


def print_metric(label: str, metric: dict) -> None:
    n = metric["cases"]
    if not n:
        print(f"    {label:<18} no evaluable cases")
        return
    print(
        f"    {label:<18} "
        f"Top-1 {metric['top1']}/{n} {100.0 * metric['top1'] / n:5.1f}% · "
        f"Top-3 {metric['top3']}/{n} {100.0 * metric['top3'] / n:5.1f}% · "
        f"Top-10 {metric['top10']}/{n} {100.0 * metric['top10'] / n:5.1f}%"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, default=Path("./build/acclorite_m11_injection_runner"))
    parser.add_argument("--index", type=Path, default=Path("benchmark-m11-compact-index.npz"))
    parser.add_argument("--corpus", action="append", type=Path, default=[])
    parser.add_argument("--budget", action="append", type=int, default=[])
    parser.add_argument("--semantic-depth", type=int, default=DEFAULT_SEMANTIC_DEPTH)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--cache-dir", type=Path)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    budgets = sorted(set(args.budget or DEFAULT_BUDGETS))
    if any(b < 1 for b in budgets) or args.semantic_depth < 10 or args.semantic_depth > 32 or args.timeout <= 0:
        print("error: invalid budget/depth/timeout parameter", file=sys.stderr)
        return 2
    if not args.runner.exists():
        print(f"error: Experiment F runner not found: {args.runner}", file=sys.stderr)
        return 2

    try:
        import numpy as np
    except ImportError as exc:  # pragma: no cover
        print("error: Experiment F requires NumPy through the research embedding stack", file=sys.stderr)
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
    data, embeddings, commands, selection_rank, model_name = fusion.load_index(args.index, np)
    index_loaded = time.perf_counter()
    if max(budgets) > int(selection_rank.max(initial=0)):
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

    baseline: list[list[str]] = []
    baseline_errors: list[str] = []
    for query in queries:
        ranking, error = run_injection_runner(args.runner, query, [], args.timeout)
        baseline.append(ranking)
        baseline_errors.append(error)
    baseline_done = time.perf_counter()

    budget_reports: list[dict] = []
    injected_error_count = 0
    for budget in budgets:
        semantic = fusion.semantic_rankings(
            np, embeddings, commands, selection_rank, query_embeddings, budget, args.semantic_depth
        )
        injected: list[list[str]] = []
        injection_errors: list[str] = []
        for query, hints in zip(queries, semantic):
            ranking, error = run_injection_runner(
                args.runner, query, hints[:args.semantic_depth], args.timeout
            )
            injected.append(ranking)
            injection_errors.append(error)
        injected_error_count += sum(bool(item) for item in injection_errors)

        corpus_reports: list[dict] = []
        for corpus, (begin, end) in zip(corpora, corpus_ranges):
            cases = cases_flat[begin:end]
            sem = semantic[begin:end]
            base = baseline[begin:end]
            inj = injected[begin:end]
            rows = []
            for case, sem_row, base_row, inj_row in zip(cases, sem, base, inj):
                rows.append({
                    "id": str(case.get("id", "")),
                    "accepted": sorted(fusion.accepted(case)),
                    "semantic_hints": sem_row[:args.semantic_depth],
                    "baseline": base_row,
                    "injected": inj_row,
                })
            corpus_reports.append({
                "corpus": str(corpus.get("name", "")),
                "semantic": fusion.summarize(sem, cases),
                "baseline": fusion.summarize(base, cases),
                "injected": fusion.summarize(inj, cases),
                "diagnostics": diagnostic_counts(sem, base, inj, cases, args.semantic_depth),
                "cases": rows,
            })
        budget_reports.append({"budget": budget, "corpora": corpus_reports})
    evaluated = time.perf_counter()

    report = {
        "experiment": "m11-pre-ranking-semantic-candidate-injection-v1",
        "index": str(args.index),
        "model": model_name,
        "device": args.device,
        "stored_passages": int(embeddings.shape[0]),
        "embedding_dimensions": int(embeddings.shape[1]),
        "semantic_depth": args.semantic_depth,
        "baseline_errors": sum(bool(item) for item in baseline_errors),
        "injection_errors": injected_error_count,
        "timing_ms": {
            "index_load": round((index_loaded - started) * 1000.0, 1),
            "model_load": round((model_loaded - index_loaded) * 1000.0, 1),
            "embed_queries": round((queries_embedded - model_loaded) * 1000.0, 1),
            "baseline_queries": round((baseline_done - queries_embedded) * 1000.0, 1),
            "injected_and_evaluate": round((evaluated - baseline_done) * 1000.0, 1),
            "total": round((evaluated - started) * 1000.0, 1),
        },
        "budgets": budget_reports,
    }

    print("Acclorite M11 pre-ranking semantic candidate-injection probe")
    print("─" * 76)
    print(f"Index:              {args.index}")
    print(f"Model:              {model_name}")
    print(f"Stored passages:    {embeddings.shape[0]}")
    print(f"Semantic depth:     {args.semantic_depth}")
    print(f"Baseline errors:    {report['baseline_errors']}")
    print(f"Injection errors:   {report['injection_errors']}")
    timing = report["timing_ms"]
    print(
        "Probe timing:       "
        f"index {timing['index_load']:.1f} ms · model {timing['model_load']:.1f} ms · "
        f"queries {timing['embed_queries']:.1f} ms · baseline {timing['baseline_queries']:.1f} ms · "
        f"inject/eval {timing['injected_and_evaluate']:.1f} ms · total {timing['total']:.1f} ms"
    )
    print()

    for budget_report in budget_reports:
        print(f"Passage budget: {budget_report['budget']}/command")
        for corpus_report in budget_report["corpora"]:
            print(f"  {corpus_report['corpus']}")
            print_metric("semantic", corpus_report["semantic"])
            print_metric("baseline", corpus_report["baseline"])
            print_metric("injected", corpus_report["injected"])
            diag = corpus_report["diagnostics"]
            hinted = diag["semantic_candidate_hits"]
            retained = diag["semantic_hits_retained_top10"]
            print(
                f"    hint diagnostics   semantic candidate hit {hinted}/{diag['cases']} · "
                f"retained in injected Top-10 {retained}/{hinted if hinted else 0} · "
                f"new Top-3 {diag['new_top3_recoveries']} · new Top-10 {diag['new_top10_recoveries']} · "
                f"Top-10 regressions {diag['top10_regressions']}"
            )
        print()

    if args.json_out:
        args.json_out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    data.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
