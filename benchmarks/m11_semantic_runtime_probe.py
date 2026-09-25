#!/usr/bin/env python3
"""M11 Semantic Assist Runtime Qualification 1: quantized ONNX query encoder.

This probe does not rebuild the semantic passage index. It reuses Experiment D's
persisted PyTorch-built compact index, encodes only benchmark queries with an ONNX
variant of the same MiniLM model, retrieves semantic Top-N command identities, and
passes those identities through the already-established source-substantiated
`conservative-ranked` research seam.

The product contract under test is the accepted separate semantic-assist channel:
Acclorite's deterministic ranking is never modified. This probe asks whether a
small ONNX query encoder preserves the *rescue value* measured with the PyTorch
research runtime closely enough to become the native-runtime candidate.

Network access is disabled by default. Use --allow-download once to fetch the
selected ONNX artifact if it is not already cached.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path

import m11_embedding_probe as embedprobe
import m11_hybrid_fusion_probe as fusion
import m11_ranked_injection_probe as ranked
import m11_semantic_assist_acceptance as assist

DEFAULT_ONNX_FILE = "onnx/model_quint8_avx2.onnx"
DEFAULT_PROVIDER = "CPUExecutionProvider"
DEFAULT_BUDGET = 4
DEFAULT_SEMANTIC_DEPTH = 20


def _load_onnx_model(
    model_name: str,
    onnx_file: str,
    *,
    allow_download: bool,
    cache_dir: Path | None,
    provider: str,
):
    try:
        from sentence_transformers import SentenceTransformer
    except ImportError as exc:  # pragma: no cover - research-host dependent
        print(
            'error: Runtime Qualification 1 requires the research-only ONNX stack. '
            'Install it in the M11 venv with: pip install -U "sentence-transformers[onnx]"',
            file=sys.stderr,
        )
        raise SystemExit(3) from exc

    if not allow_download:
        os.environ.setdefault("HF_HUB_OFFLINE", "1")
        os.environ.setdefault("TRANSFORMERS_OFFLINE", "1")

    kwargs = {
        "backend": "onnx",
        "device": "cpu",
        "local_files_only": not allow_download,
        "model_kwargs": {
            "file_name": onnx_file,
            "provider": provider,
            # Never silently export another model when the requested artifact is absent.
            "export": False,
        },
    }
    if cache_dir is not None:
        kwargs["cache_folder"] = str(cache_dir)

    try:
        return SentenceTransformer(model_name, **kwargs)
    except Exception as exc:  # pragma: no cover - runtime/cache dependent
        message = str(exc).lower()
        if "onnxruntime" in message or "optimum" in message or "onnx" in message and "install" in message:
            print(
                'error: ONNX backend dependencies are missing. In the M11 venv run: '
                'pip install -U "sentence-transformers[onnx]"',
                file=sys.stderr,
            )
            raise SystemExit(3) from exc
        if not allow_download:
            print(
                f"error: ONNX artifact '{onnx_file}' for '{model_name}' is not available locally. "
                "Network access is disabled by default. Rerun once with --allow-download, then future "
                "qualification runs can remain offline.",
                file=sys.stderr,
            )
            raise SystemExit(3) from exc
        raise


def _corpus_cases(corpus_paths: list[Path]) -> tuple[list[dict], list[dict], list[tuple[int, int]], list[str]]:
    corpora = embedprobe._load_corpora(corpus_paths)
    cases_flat: list[dict] = []
    ranges: list[tuple[int, int]] = []
    queries: list[str] = []
    for corpus in corpora:
        begin = len(cases_flat)
        for case in corpus.get("cases", []):
            cases_flat.append(case)
            queries.append(str(case.get("query", "")))
        ranges.append((begin, len(cases_flat)))
    return corpora, cases_flat, ranges, queries


def _reference_map(report: dict) -> dict[str, dict]:
    mapped: dict[str, dict] = {}
    for corpus in report.get("corpora", []):
        name = str(corpus.get("corpus", ""))
        if name:
            mapped[name] = corpus
    return mapped


def _case_by_id(corpus: dict) -> dict[str, dict]:
    return {str(case.get("id", "")): case for case in corpus.get("cases", []) if str(case.get("id", ""))}


def _assist_cases(cases: list[dict]) -> list[dict]:
    """Normalize benchmark cases to the closure analyzer's accepted-answer schema.

    Raw corpus files store answers under ``expect.top1_any`` / ``top3_any`` while
    Experiment G's per-case JSON stores a normalized ``accepted`` list. Runtime
    qualification consumes the raw corpora, so normalize explicitly before using
    the separate-channel acceptance helpers.
    """
    normalized: list[dict] = []
    for case in cases:
        answers = sorted(fusion.accepted(case))
        if not answers:
            answers = sorted({str(x) for x in case.get("accepted", []) if str(x)})
        normalized.append({"id": str(case.get("id", "")), "accepted": answers})
    return normalized


def _rescue_counts(baseline: list[list[str]], supported: list[list[str]], cases: list[dict], limit: int = 5) -> dict:
    normalized_cases = _assist_cases(cases)
    alternatives = [
        assist.semantic_alternatives(base, support, baseline_depth=10, limit=limit)
        for base, support in zip(baseline, supported)
    ]
    return {
        "primary": assist.rescue_summary(baseline, alternatives, normalized_cases, 1),
        "top3": assist.rescue_summary(baseline, alternatives, normalized_cases, 3),
        "top10": assist.rescue_summary(baseline, alternatives, normalized_cases, 10),
    }


def _validate_reference_miss_counts(reference_corpus: dict, rescue: dict, cases: list[dict]) -> None:
    """Fail closed if the rescue analyzer disagrees with Experiment G's baseline metrics.

    This invariant catches answer-schema bugs before a zero-vs-zero comparison can
    accidentally satisfy the ONNX quality gate.
    """
    baseline = reference_corpus.get("baseline", {})
    total = int(baseline.get("cases", len(cases)))
    expected = {
        "primary": total - int(baseline.get("top1", 0)),
        "top3": total - int(baseline.get("top3", 0)),
        "top10": total - int(baseline.get("top10", 0)),
    }
    got = {key: int(rescue[key]["misses"]) for key in expected}
    if got != expected:
        raise ValueError(
            "reference rescue miss counts do not match Experiment G baseline metrics: "
            f"expected {expected}, got {got}"
        )


def _rescue_vector(summary: dict) -> tuple[int, int, int]:
    return (
        int(summary["rescued_at_1"]),
        int(summary["rescued_at_3"]),
        int(summary["rescued_at_5"]),
    )


def _reference_rescue(reference_corpus: dict, cases: list[dict], limit: int = 5) -> dict:
    rows = list(reference_corpus.get("cases", []))
    by_id = {str(row.get("id", "")): row for row in rows}
    baseline: list[list[str]] = []
    supported: list[list[str]] = []
    for case in cases:
        case_id = str(case.get("id", ""))
        row = by_id.get(case_id)
        if row is None:
            raise ValueError(f"reference report missing case '{case_id}'")
        baseline.append(list(row.get("baseline", [])))
        supported.append(list(row.get("policies", {}).get("conservative-ranked", [])))
    return _rescue_counts(baseline, supported, cases, limit)


def _quality_delta(reference: dict, candidate: dict) -> dict:
    result: dict[str, dict] = {}
    for key in ("primary", "top3", "top10"):
        ref = _rescue_vector(reference[key])
        got = _rescue_vector(candidate[key])
        result[key] = {
            "reference": {"assist1": ref[0], "assist3": ref[1], "assist5": ref[2]},
            "onnx": {"assist1": got[0], "assist3": got[1], "assist5": got[2]},
            "delta": {"assist1": got[0] - ref[0], "assist3": got[1] - ref[1], "assist5": got[2] - ref[2]},
        }
    return result


def _passes_quality_gate(delta: dict) -> bool:
    """Accept at most one lost rescue at assist@3/@5 for each miss family.

    assist@1 is intentionally not a gate: the accepted product surface is an
    alternatives channel, and the ordering inside that channel is secondary to
    preserving useful recall within the first few alternatives.
    """
    for key in ("primary", "top3", "top10"):
        if int(delta[key]["delta"]["assist3"]) < -1:
            return False
        if int(delta[key]["delta"]["assist5"]) < -1:
            return False
    return True


def _print_rescue(label: str, summary: dict) -> None:
    misses = int(summary["misses"])
    def pct(value: int) -> float:
        return 100.0 * value / misses if misses else 0.0
    print(
        f"      {label:<8} misses {misses:2d} · "
        f"assist@1 {summary['rescued_at_1']:2d}/{misses:<2d} {pct(summary['rescued_at_1']):5.1f}% · "
        f"assist@3 {summary['rescued_at_3']:2d}/{misses:<2d} {pct(summary['rescued_at_3']):5.1f}% · "
        f"assist@5 {summary['rescued_at_5']:2d}/{misses:<2d} {pct(summary['rescued_at_5']):5.1f}%"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, default=Path("./build/acclorite_m11_injection_runner"))
    parser.add_argument("--index", type=Path, default=Path("benchmark-m11-compact-index.npz"))
    parser.add_argument("--reference-report", type=Path, default=Path("benchmark-m11-ranked-injection.json"))
    parser.add_argument("--corpus", action="append", type=Path, default=[])
    parser.add_argument("--onnx-file", default=DEFAULT_ONNX_FILE)
    parser.add_argument("--provider", default=DEFAULT_PROVIDER)
    parser.add_argument("--allow-download", action="store_true")
    parser.add_argument("--cache-dir", type=Path)
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--budget", type=int, default=DEFAULT_BUDGET)
    parser.add_argument("--semantic-depth", type=int, default=DEFAULT_SEMANTIC_DEPTH)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    if args.batch_size < 1 or args.budget < 1 or not 10 <= args.semantic_depth <= 32 or args.timeout <= 0:
        print("error: invalid batch-size/budget/semantic-depth/timeout", file=sys.stderr)
        return 2
    if not args.runner.exists():
        print(f"error: M11 injection runner not found: {args.runner}", file=sys.stderr)
        return 2
    if not args.reference_report.exists():
        print(f"error: reference Experiment G report not found: {args.reference_report}", file=sys.stderr)
        return 2

    try:
        import numpy as np
    except ImportError as exc:  # pragma: no cover
        print("error: Runtime Qualification 1 requires NumPy in the research venv", file=sys.stderr)
        raise SystemExit(3) from exc

    corpus_paths = args.corpus or [
        Path("benchmarks/corpus-m11-research-v1.json"),
        Path("benchmarks/corpus-m11-heldout-v1.json"),
        Path("benchmarks/corpus-m11-transfer-v1.json"),
    ]
    corpora, cases_flat, ranges, queries = _corpus_cases(corpus_paths)

    try:
        reference = json.loads(args.reference_report.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"error: unable to read reference report: {exc}", file=sys.stderr)
        return 2
    if reference.get("experiment") != "m11-bounded-ranked-semantic-support-v1":
        print("error: reference report is not Experiment G output", file=sys.stderr)
        return 2
    reference_by_name = _reference_map(reference)

    started = time.perf_counter()
    data, embeddings, commands, selection_rank, model_name = fusion.load_index(args.index, np)
    index_loaded = time.perf_counter()
    if args.budget > int(selection_rank.max(initial=0)):
        print("error: requested passage budget exceeds compact index", file=sys.stderr)
        data.close()
        return 2

    model = _load_onnx_model(
        model_name,
        args.onnx_file,
        allow_download=args.allow_download,
        cache_dir=args.cache_dir,
        provider=args.provider,
    )
    model_loaded = time.perf_counter()
    query_embeddings = model.encode(
        queries,
        batch_size=args.batch_size,
        show_progress_bar=False,
        convert_to_numpy=True,
        normalize_embeddings=True,
    ).astype(np.float32, copy=False)
    queries_embedded = time.perf_counter()

    semantic = fusion.semantic_rankings(
        np, embeddings, commands, selection_rank, query_embeddings, args.budget, args.semantic_depth
    )

    supported: list[list[str]] = []
    errors: list[str] = []
    for query, sem in zip(queries, semantic):
        ranking, error = ranked.run_runner(
            args.runner,
            query,
            ranked.ranked_hints(sem, "conservative-ranked", args.semantic_depth),
            args.timeout,
        )
        supported.append(ranking)
        errors.append(error)
    supported_done = time.perf_counter()

    corpus_reports: list[dict] = []
    quality_ok = True
    for corpus, (begin, end) in zip(corpora, ranges):
        name = str(corpus.get("name", ""))
        reference_corpus = reference_by_name.get(name)
        if reference_corpus is None:
            print(f"error: reference report has no corpus named '{name}'", file=sys.stderr)
            data.close()
            return 2
        cases = cases_flat[begin:end]
        ref_cases = _case_by_id(reference_corpus)
        baseline: list[list[str]] = []
        for case in cases:
            case_id = str(case.get("id", ""))
            row = ref_cases.get(case_id)
            if row is None:
                print(f"error: reference report missing case '{case_id}'", file=sys.stderr)
                data.close()
                return 2
            baseline.append(list(row.get("baseline", [])))

        onnx_supported = supported[begin:end]
        onnx_semantic = semantic[begin:end]
        onnx_rescue = _rescue_counts(baseline, onnx_supported, cases)
        ref_rescue = _reference_rescue(reference_corpus, cases)
        try:
            _validate_reference_miss_counts(reference_corpus, ref_rescue, cases)
        except ValueError as exc:
            print(f"error: invalid Runtime Qualification reference analysis for '{name}': {exc}", file=sys.stderr)
            data.close()
            return 2
        delta = _quality_delta(ref_rescue, onnx_rescue)
        gate = _passes_quality_gate(delta)
        quality_ok = quality_ok and gate
        corpus_reports.append({
            "corpus": name,
            "semantic": fusion.summarize(onnx_semantic, cases),
            "reference_semantic": reference_corpus.get("semantic", {}),
            "onnx_rescue": onnx_rescue,
            "reference_rescue": ref_rescue,
            "rescue_delta": delta,
            "quality_gate_pass": gate,
        })

    finished = time.perf_counter()
    timing = {
        "index_load": round((index_loaded - started) * 1000.0, 1),
        "model_load": round((model_loaded - index_loaded) * 1000.0, 1),
        "embed_queries": round((queries_embedded - model_loaded) * 1000.0, 1),
        "source_substantiation_and_ranking": round((supported_done - queries_embedded) * 1000.0, 1),
        "analysis": round((finished - supported_done) * 1000.0, 1),
        "total": round((finished - started) * 1000.0, 1),
    }
    report = {
        "qualification": "m11-semantic-runtime-onnx-q1",
        "model": model_name,
        "onnx_file": args.onnx_file,
        "provider": args.provider,
        "index": str(args.index),
        "passage_budget": args.budget,
        "semantic_depth": args.semantic_depth,
        "reference_report": str(args.reference_report),
        "supported_errors": sum(bool(error) for error in errors),
        "quality_gate": {
            "pass": quality_ok,
            "rule": "for every corpus/miss family, ONNX may lose at most one PyTorch rescue at assist@3 and assist@5",
        },
        "timing_ms": timing,
        "corpora": corpus_reports,
    }

    print("Acclorite M11 Semantic Assist Runtime Qualification 1")
    print("─" * 76)
    print(f"Model:              {model_name}")
    print(f"ONNX artifact:      {args.onnx_file}")
    print(f"Provider:           {args.provider}")
    print(f"Stored passages:    {len(commands)}")
    print(f"Semantic depth:     {args.semantic_depth}")
    print(f"Supported errors:   {report['supported_errors']}")
    print(
        "Probe timing:       "
        f"index {timing['index_load']:.1f} ms · model {timing['model_load']:.1f} ms · "
        f"queries {timing['embed_queries']:.1f} ms · substantiate/rank {timing['source_substantiation_and_ranking']:.1f} ms · "
        f"total {timing['total']:.1f} ms"
    )
    for corpus in corpus_reports:
        metric = corpus["semantic"]
        n = metric["cases"]
        print(f"\n{corpus['corpus']}")
        print(
            f"    ONNX semantic      Top-1 {metric['top1']}/{n} {100.0 * metric['top1']/n:5.1f}% · "
            f"Top-3 {metric['top3']}/{n} {100.0 * metric['top3']/n:5.1f}% · "
            f"Top-10 {metric['top10']}/{n} {100.0 * metric['top10']/n:5.1f}%"
        )
        _print_rescue("primary", corpus["onnx_rescue"]["primary"])
        _print_rescue("Top-3", corpus["onnx_rescue"]["top3"])
        _print_rescue("Top-10", corpus["onnx_rescue"]["top10"])
        print(f"      quality gate: {'PASS' if corpus['quality_gate_pass'] else 'FAIL'}")

    print(f"\nOverall quality gate: {'PASS' if quality_ok else 'FAIL'}")
    if args.json_out:
        args.json_out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    data.close()
    return 0 if quality_ok and not any(errors) else 1


if __name__ == "__main__":
    raise SystemExit(main())
