#!/usr/bin/env python3
"""M11 Semantic Assist Runtime Qualification 2: native C++ ONNX query encoder.

RQ2 keeps Python only as a qualification harness. The query encoder under test is
`acclorite_m11_native_onnx_encoder`, which performs BERT BasicTokenizer + WordPiece,
ONNX Runtime inference, SentenceTransformers mean pooling, and L2 normalization in
native C++.

The native tokenizer must exactly match the cached Hugging Face tokenizer for every
benchmark query plus a fixed Unicode/punctuation parity set. Native rescue coverage
must remain within RQ1's already-accepted ONNX quality tolerance.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import m11_hybrid_fusion_probe as fusion
import m11_ranked_injection_probe as ranked
import m11_semantic_runtime_probe as rq1

DEFAULT_MODEL_FILE = "onnx/model_quint8_avx2.onnx"
DEFAULT_VOCAB_FILE = "vocab.txt"
DEFAULT_BUDGET = 4
DEFAULT_SEMANTIC_DEPTH = 20
DEFAULT_MAX_LENGTH = 256

PARITY_TEXTS = [
    "Résumé CAFÉ naïve façade",
    "foo—bar “quoted” and ‘single’",
    "路径 文件 内容",
    "İstanbul Straße",
    "emoji 🙂 file-name.txt",
]


def _load_reference(path: Path) -> dict:
    try:
        report = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"unable to read RQ1 reference report: {exc}") from exc
    if report.get("qualification") != "m11-semantic-runtime-onnx-q1":
        raise ValueError("reference report is not corrected Runtime Qualification 1 output")
    if not report.get("quality_gate", {}).get("pass"):
        raise ValueError("reference RQ1 report did not pass its quality gate")
    return report


def _reference_by_name(report: dict) -> dict[str, dict]:
    return {str(row.get("corpus", "")): row for row in report.get("corpora", [])}


def _locate_assets(model_name: str, model_file: str, vocab_file: str, cache_dir: Path | None) -> tuple[Path, Path]:
    try:
        from huggingface_hub import hf_hub_download
    except ImportError as exc:  # pragma: no cover
        raise RuntimeError("RQ2 requires huggingface_hub in the research venv to locate already-cached assets") from exc

    kwargs = {"repo_id": model_name, "local_files_only": True}
    if cache_dir is not None:
        kwargs["cache_dir"] = str(cache_dir)
    try:
        model = Path(hf_hub_download(filename=model_file, **kwargs))
        vocab = Path(hf_hub_download(filename=vocab_file, **kwargs))
    except Exception as exc:  # noqa: BLE001 - surface backend cache errors verbatim
        raise RuntimeError(
            "required model/tokenizer asset is not cached; rerun RQ1 once with --allow-download before RQ2"
        ) from exc
    return model, vocab


def _write_input(lines: list[str]) -> Path:
    handle = tempfile.NamedTemporaryFile("w", encoding="utf-8", newline="\n", delete=False)
    try:
        for line in lines:
            if "\n" in line or "\r" in line:
                raise ValueError("RQ2 line protocol does not permit embedded newlines")
            handle.write(line + "\n")
        return Path(handle.name)
    finally:
        handle.close()


def _run_native(runner: Path, *, vocab: Path, input_path: Path, model: Path | None, tokenize_only: bool,
                max_length: int, threads: int, timeout: float) -> subprocess.CompletedProcess[str]:
    cmd = [
        str(runner),
        "--vocab", str(vocab),
        "--input", str(input_path),
        "--max-length", str(max_length),
        "--threads", str(threads),
    ]
    if tokenize_only:
        cmd.append("--tokenize-only")
    else:
        if model is None:
            raise ValueError("model path required for native embedding mode")
        cmd.extend(["--model", str(model)])
    return subprocess.run(cmd, text=True, capture_output=True, timeout=timeout, check=False)


def _parse_tokens(stdout: str, expected_rows: int) -> list[list[int]]:
    rows: dict[int, list[int]] = {}
    for line in stdout.splitlines():
        if not line.startswith("@tokens\t"):
            continue
        parts = line.split("\t", 2)
        if len(parts) != 3:
            raise ValueError("malformed native token row")
        index = int(parts[1])
        rows[index] = [] if not parts[2] else [int(piece) for piece in parts[2].split(",")]
    if set(rows) != set(range(expected_rows)):
        raise ValueError(f"native tokenizer returned {len(rows)} rows, expected {expected_rows}")
    return [rows[i] for i in range(expected_rows)]


def _parse_embeddings(stdout: str, expected_rows: int, np) -> object:
    rows: dict[int, list[float]] = {}
    dims = None
    for line in stdout.splitlines():
        if line.startswith("@meta\t"):
            parts = line.split("\t")
            try:
                rows_reported = int(parts[parts.index("rows") + 1])
                dims = int(parts[parts.index("dims") + 1])
            except (ValueError, IndexError) as exc:
                raise ValueError("malformed native embedding metadata") from exc
            if rows_reported != expected_rows:
                raise ValueError(f"native encoder reports {rows_reported} rows, expected {expected_rows}")
        elif line.startswith("@embedding\t"):
            parts = line.split("\t")
            if len(parts) < 3:
                raise ValueError("malformed native embedding row")
            rows[int(parts[1])] = [float(value) for value in parts[2:]]
    if dims is None:
        raise ValueError("native encoder omitted embedding metadata")
    if set(rows) != set(range(expected_rows)):
        raise ValueError(f"native encoder returned {len(rows)} rows, expected {expected_rows}")
    if any(len(row) != dims for row in rows.values()):
        raise ValueError("native encoder emitted inconsistent embedding dimensions")
    matrix = np.asarray([rows[i] for i in range(expected_rows)], dtype=np.float32)
    if not np.isfinite(matrix).all():
        raise ValueError("native encoder emitted non-finite embeddings")
    norms = np.linalg.norm(matrix, axis=1)
    if not np.allclose(norms, 1.0, atol=2e-4, rtol=2e-4):
        raise ValueError("native encoder embeddings are not L2-normalized")
    return matrix


def _tokenizer_reference(model_name: str, texts: list[str], max_length: int, cache_dir: Path | None) -> list[list[int]]:
    try:
        from transformers import AutoTokenizer
    except ImportError as exc:  # pragma: no cover
        raise RuntimeError("RQ2 requires transformers in the research venv for tokenizer parity only") from exc
    kwargs = {"local_files_only": True, "use_fast": True}
    if cache_dir is not None:
        kwargs["cache_dir"] = str(cache_dir)
    tokenizer = AutoTokenizer.from_pretrained(model_name, **kwargs)
    encoded = tokenizer(
        texts,
        add_special_tokens=True,
        truncation=True,
        max_length=max_length,
        padding=False,
    )["input_ids"]
    return [[int(value) for value in row] for row in encoded]


def _token_parity(native: list[list[int]], reference: list[list[int]], benchmark_count: int) -> dict:
    if len(native) != len(reference):
        raise ValueError("native/reference tokenizer row-count mismatch")
    mismatches = []
    for index, (got, expected) in enumerate(zip(native, reference)):
        if got != expected:
            mismatches.append({
                "index": index,
                "kind": "benchmark" if index < benchmark_count else "parity",
                "native": got,
                "reference": expected,
            })
    return {
        "pass": not mismatches,
        "rows": len(native),
        "benchmark_rows": benchmark_count,
        "extra_parity_rows": len(native) - benchmark_count,
        "mismatch_count": len(mismatches),
        "mismatches": mismatches,
    }


def _baseline_from_ranked(reference_corpus: dict, cases: list[dict]) -> list[list[str]]:
    by_id = {str(row.get("id", "")): row for row in reference_corpus.get("cases", [])}
    baseline = []
    for case in cases:
        case_id = str(case.get("id", ""))
        row = by_id.get(case_id)
        if row is None:
            raise ValueError(f"ranked reference is missing case '{case_id}'")
        baseline.append(list(row.get("baseline", [])))
    return baseline


def _quality_against_rq1(reference_corpus: dict, native_rescue: dict) -> tuple[dict, bool]:
    rq1_rescue = reference_corpus.get("onnx_rescue")
    if not isinstance(rq1_rescue, dict):
        raise ValueError("RQ1 reference corpus is missing onnx_rescue")
    delta = rq1._quality_delta(rq1_rescue, native_rescue)
    return delta, rq1._passes_quality_gate(delta)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-runner", type=Path, default=Path("./build/acclorite_m11_native_onnx_encoder"))
    parser.add_argument("--injection-runner", type=Path, default=Path("./build/acclorite_m11_injection_runner"))
    parser.add_argument("--index", type=Path, default=Path("benchmark-m11-compact-index.npz"))
    parser.add_argument("--rq1-report", type=Path, default=Path("benchmark-m11-semantic-runtime-onnx.json"))
    parser.add_argument("--ranked-report", type=Path, default=Path("benchmark-m11-ranked-injection.json"))
    parser.add_argument("--corpus", action="append", type=Path, default=[])
    parser.add_argument("--model-file", default=DEFAULT_MODEL_FILE)
    parser.add_argument("--vocab-file", default=DEFAULT_VOCAB_FILE)
    parser.add_argument("--cache-dir", type=Path)
    parser.add_argument("--budget", type=int, default=DEFAULT_BUDGET)
    parser.add_argument("--semantic-depth", type=int, default=DEFAULT_SEMANTIC_DEPTH)
    parser.add_argument("--max-length", type=int, default=DEFAULT_MAX_LENGTH)
    parser.add_argument("--threads", type=int, default=0)
    parser.add_argument("--native-timeout", type=float, default=30.0)
    parser.add_argument("--search-timeout", type=float, default=8.0)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    if not args.native_runner.exists():
        print(f"error: native RQ2 runner not found: {args.native_runner}", file=sys.stderr)
        print("hint: install onnxruntime-cpu and libutf8proc, then reconfigure/rebuild CMake", file=sys.stderr)
        return 2
    if not args.injection_runner.exists() or not args.rq1_report.exists() or not args.ranked_report.exists():
        print("error: missing injection runner or required RQ1/G reference report", file=sys.stderr)
        return 2
    if args.budget < 1 or not 10 <= args.semantic_depth <= 32 or not 2 <= args.max_length <= 512:
        print("error: invalid budget/semantic-depth/max-length", file=sys.stderr)
        return 2

    try:
        import numpy as np
    except ImportError as exc:  # pragma: no cover
        print("error: RQ2 requires NumPy in the research venv", file=sys.stderr)
        raise SystemExit(3) from exc

    try:
        rq1_report = _load_reference(args.rq1_report)
        ranked_report = json.loads(args.ranked_report.read_text(encoding="utf-8"))
    except (ValueError, OSError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    if ranked_report.get("experiment") != "m11-bounded-ranked-semantic-support-v1":
        print("error: ranked report is not Experiment G output", file=sys.stderr)
        return 2

    corpus_paths = args.corpus or [
        Path("benchmarks/corpus-m11-research-v1.json"),
        Path("benchmarks/corpus-m11-heldout-v1.json"),
        Path("benchmarks/corpus-m11-transfer-v1.json"),
    ]
    corpora, cases_flat, ranges, queries = rq1._corpus_cases(corpus_paths)
    rq1_by_name = _reference_by_name(rq1_report)
    ranked_by_name = rq1._reference_map(ranked_report)

    started = time.perf_counter()
    try:
        model_path, vocab_path = _locate_assets(
            str(rq1_report.get("model", "sentence-transformers/all-MiniLM-L6-v2")),
            args.model_file,
            args.vocab_file,
            args.cache_dir,
        )
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    assets_ready = time.perf_counter()

    parity_texts = queries + PARITY_TEXTS
    parity_input = _write_input(parity_texts)
    encode_input = _write_input(queries)
    try:
        token_proc = _run_native(
            args.native_runner,
            vocab=vocab_path,
            input_path=parity_input,
            model=None,
            tokenize_only=True,
            max_length=args.max_length,
            threads=args.threads,
            timeout=args.native_timeout,
        )
        if token_proc.returncode != 0:
            print(token_proc.stderr, file=sys.stderr, end="")
            return 1
        native_tokens = _parse_tokens(token_proc.stdout, len(parity_texts))
        reference_tokens = _tokenizer_reference(
            str(rq1_report.get("model", "sentence-transformers/all-MiniLM-L6-v2")),
            parity_texts,
            args.max_length,
            args.cache_dir,
        )
        token_parity = _token_parity(native_tokens, reference_tokens, len(queries))
        token_done = time.perf_counter()

        encode_proc = _run_native(
            args.native_runner,
            vocab=vocab_path,
            input_path=encode_input,
            model=model_path,
            tokenize_only=False,
            max_length=args.max_length,
            threads=args.threads,
            timeout=args.native_timeout,
        )
        if encode_proc.returncode != 0:
            print(encode_proc.stderr, file=sys.stderr, end="")
            return 1
        native_embeddings = _parse_embeddings(encode_proc.stdout, len(queries), np)
        encode_done = time.perf_counter()
    finally:
        parity_input.unlink(missing_ok=True)
        encode_input.unlink(missing_ok=True)

    data, passage_embeddings, commands, selection_rank, model_name = fusion.load_index(args.index, np)
    index_loaded = time.perf_counter()
    if args.budget > int(selection_rank.max(initial=0)):
        print("error: requested passage budget exceeds compact index", file=sys.stderr)
        data.close()
        return 2
    if model_name != rq1_report.get("model"):
        print("error: compact index model identity does not match RQ1 model", file=sys.stderr)
        data.close()
        return 2

    semantic = fusion.semantic_rankings(
        np,
        passage_embeddings,
        commands,
        selection_rank,
        native_embeddings,
        args.budget,
        args.semantic_depth,
    )
    semantic_done = time.perf_counter()

    supported: list[list[str]] = []
    errors: list[str] = []
    for query, sem in zip(queries, semantic):
        ranking, error = ranked.run_runner(
            args.injection_runner,
            query,
            ranked.ranked_hints(sem, "conservative-ranked", args.semantic_depth),
            args.search_timeout,
        )
        supported.append(ranking)
        errors.append(error)
    substantiated = time.perf_counter()

    corpus_reports = []
    quality_ok = token_parity["pass"] and not any(errors)
    for corpus, (begin, end) in zip(corpora, ranges):
        name = str(corpus.get("name", ""))
        rq1_corpus = rq1_by_name.get(name)
        ranked_corpus = ranked_by_name.get(name)
        if rq1_corpus is None or ranked_corpus is None:
            print(f"error: reference reports are missing corpus '{name}'", file=sys.stderr)
            data.close()
            return 2
        cases = cases_flat[begin:end]
        try:
            baseline = _baseline_from_ranked(ranked_corpus, cases)
        except ValueError as exc:
            print(f"error: {exc}", file=sys.stderr)
            data.close()
            return 2
        native_rescue = rq1._rescue_counts(baseline, supported[begin:end], cases)
        try:
            rq1._validate_reference_miss_counts(ranked_corpus, native_rescue, cases)
        except ValueError as exc:
            print(f"error: native rescue miss-count invariant failed for '{name}': {exc}", file=sys.stderr)
            data.close()
            return 2
        delta, gate = _quality_against_rq1(rq1_corpus, native_rescue)
        quality_ok = quality_ok and gate
        corpus_reports.append({
            "corpus": name,
            "semantic": fusion.summarize(semantic[begin:end], cases),
            "rq1_semantic": rq1_corpus.get("semantic", {}),
            "native_rescue": native_rescue,
            "rq1_rescue": rq1_corpus.get("onnx_rescue", {}),
            "rescue_delta": delta,
            "quality_gate_pass": gate,
        })

    finished = time.perf_counter()
    timing = {
        "locate_assets": round((assets_ready - started) * 1000.0, 1),
        "tokenizer_parity": round((token_done - assets_ready) * 1000.0, 1),
        "native_model_and_queries": round((encode_done - token_done) * 1000.0, 1),
        "index_load": round((index_loaded - encode_done) * 1000.0, 1),
        "semantic_rank": round((semantic_done - index_loaded) * 1000.0, 1),
        "source_substantiation_and_ranking": round((substantiated - semantic_done) * 1000.0, 1),
        "analysis": round((finished - substantiated) * 1000.0, 1),
        "total": round((finished - started) * 1000.0, 1),
    }
    report = {
        "qualification": "m11-semantic-native-onnx-q2",
        "model": model_name,
        "onnx_file": args.model_file,
        "vocab_file": args.vocab_file,
        "index": str(args.index),
        "passage_budget": args.budget,
        "semantic_depth": args.semantic_depth,
        "max_length": args.max_length,
        "tokenizer_parity": token_parity,
        "supported_errors": sum(bool(error) for error in errors),
        "quality_gate": {
            "pass": quality_ok,
            "rule": "exact tokenizer IDs for benchmark+fixed parity texts; per corpus/miss family native may lose at most one RQ1 rescue at assist@3/@5",
        },
        "timing_ms": timing,
        "corpora": corpus_reports,
    }

    print("Acclorite M11 Semantic Assist Runtime Qualification 2")
    print("─" * 76)
    print(f"Native runner:      {args.native_runner}")
    print(f"Model:              {model_name}")
    print(f"ONNX artifact:      {args.model_file}")
    print(f"Tokenizer vocab:    {args.vocab_file}")
    print(f"Tokenizer parity:   {'PASS' if token_parity['pass'] else 'FAIL'} · {token_parity['mismatch_count']} mismatches/{token_parity['rows']} rows")
    print(f"Stored passages:    {len(commands)}")
    print(f"Semantic depth:     {args.semantic_depth}")
    print(f"Supported errors:   {report['supported_errors']}")
    print(
        "Probe timing:       "
        f"tokenizer {timing['tokenizer_parity']:.1f} ms · native model+queries {timing['native_model_and_queries']:.1f} ms · "
        f"index {timing['index_load']:.1f} ms · semantic {timing['semantic_rank']:.1f} ms · "
        f"substantiate/rank {timing['source_substantiation_and_ranking']:.1f} ms · total {timing['total']:.1f} ms"
    )
    if not token_parity["pass"]:
        for mismatch in token_parity["mismatches"][:5]:
            print(f"  tokenizer mismatch row {mismatch['index']} ({mismatch['kind']})")
    for corpus in corpus_reports:
        metric = corpus["semantic"]
        n = metric["cases"]
        print(f"\n{corpus['corpus']}")
        print(
            f"    native semantic    Top-1 {metric['top1']}/{n} {100.0 * metric['top1']/n:5.1f}% · "
            f"Top-3 {metric['top3']}/{n} {100.0 * metric['top3']/n:5.1f}% · "
            f"Top-10 {metric['top10']}/{n} {100.0 * metric['top10']/n:5.1f}%"
        )
        rq1._print_rescue("primary", corpus["native_rescue"]["primary"])
        rq1._print_rescue("Top-3", corpus["native_rescue"]["top3"])
        rq1._print_rescue("Top-10", corpus["native_rescue"]["top10"])
        print(f"      quality gate: {'PASS' if corpus['quality_gate_pass'] else 'FAIL'}")

    print(f"\nOverall RQ2 gate: {'PASS' if quality_ok else 'FAIL'}")
    if args.json_out:
        args.json_out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    data.close()
    return 0 if quality_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
