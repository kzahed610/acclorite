#!/usr/bin/env python3
"""M11 Experiment B: local latent-semantic retrieval over static man text.

Research tooling only. The probe reuses the exact local man-page token corpus from
Experiment A, but replaces BM25 lexical ranking with Latent Semantic Analysis
(TF-IDF -> TruncatedSVD -> cosine similarity). This isolates whether a semantic
representation learned entirely from local documentation materially improves
retrieval without a downloaded/pretrained model.

It does not execute candidate commands, access the network, or alter Acclorite's
runtime index. scikit-learn is a research-only dependency for this experiment.
"""
from __future__ import annotations

import argparse
import collections
import json
import resource
import sys
import time
from pathlib import Path

import m11_mantext_probe as manprobe


def _load_sklearn():
    try:
        import numpy as np
        from sklearn.decomposition import TruncatedSVD
        from sklearn.feature_extraction import DictVectorizer
        from sklearn.feature_extraction.text import TfidfTransformer
        from sklearn.preprocessing import normalize
    except ImportError as exc:  # pragma: no cover - depends on research host
        print(
            "error: Experiment B requires the research-only Python package "
            "scikit-learn (and its NumPy/SciPy dependencies). Acclorite runtime "
            "does not require it. On Arch/CachyOS the distro package is "
            "python-scikit-learn.",
            file=sys.stderr,
        )
        raise SystemExit(3) from exc
    return np, TruncatedSVD, DictVectorizer, TfidfTransformer, normalize


def _rss_mib() -> float:
    # Linux reports ru_maxrss in KiB. This probe targets Linux only.
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024.0


def _accepted(case: dict) -> set[str]:
    expected = case.get("expect", {})
    return set(expected.get("top1_any", [])) | set(expected.get("top3_any", []))


def _load_corpora(paths: list[Path]) -> list[dict]:
    corpora: list[dict] = []
    for path in paths:
        try:
            corpus = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            print(f"error: cannot read corpus {path}: {exc}", file=sys.stderr)
            raise SystemExit(2) from exc
        corpus["_path"] = str(path)
        corpora.append(corpus)
    return corpora


def build_lsa(
    index: manprobe.SearchIndex, dimensions: int, min_df: int, max_features: int
):
    np, TruncatedSVD, DictVectorizer, TfidfTransformer, normalize = _load_sklearn()

    commands = sorted(index.documents)

    # LSA cannot learn a useful latent relation from a token that occurs in only
    # one document, and keeping every roff/man-specific singleton makes randomized
    # SVD needlessly expensive. Feature selection is corpus-global and query-blind:
    # retain terms meeting min_df, then cap by document frequency followed by total
    # corpus frequency. No benchmark/query wording participates in this choice.
    total_frequency: collections.Counter[str] = collections.Counter()
    for document in index.documents.values():
        total_frequency.update(document.counts)
    eligible = [
        token for token, df in index.document_frequency.items()
        if df >= min_df
    ]
    eligible.sort(
        key=lambda token: (-index.document_frequency[token], -total_frequency[token], token)
    )
    if max_features > 0:
        eligible = eligible[:max_features]
    selected = set(eligible)
    counters = [
        {token: count for token, count in index.documents[command].counts.items() if token in selected}
        for command in commands
    ]

    vectorizer = DictVectorizer(sparse=True, sort=True)
    counts = vectorizer.fit_transform(counters)
    tfidf_transformer = TfidfTransformer(norm=None, use_idf=True, smooth_idf=True, sublinear_tf=True)
    tfidf = tfidf_transformer.fit_transform(counts)

    max_components = min(tfidf.shape[0] - 1, tfidf.shape[1] - 1)
    if max_components < 2:
        raise ValueError("not enough documents/features for latent semantic analysis")
    actual_dimensions = min(dimensions, max_components)

    svd = TruncatedSVD(
        n_components=actual_dimensions,
        algorithm="randomized",
        n_iter=7,
        random_state=0,
    )
    latent = svd.fit_transform(tfidf)
    latent = normalize(latent, norm="l2", copy=False)

    return {
        "np": np,
        "normalize": normalize,
        "commands": commands,
        "vectorizer": vectorizer,
        "tfidf_transformer": tfidf_transformer,
        "svd": svd,
        "latent": latent,
        "features": int(tfidf.shape[1]),
        "min_df": int(min_df),
        "max_features": int(max_features),
        "nnz": int(tfidf.nnz),
        "dimensions": int(actual_dimensions),
        "explained_variance": float(svd.explained_variance_ratio_.sum()),
    }


def rank_query(query: str, model: dict, top_k: int) -> tuple[list[tuple[str, float]], bool]:
    tokens = manprobe.tokenize(query)
    if not tokens:
        return [], True
    counter = collections.Counter(tokens)
    counts = model["vectorizer"].transform([dict(counter)])
    tfidf = model["tfidf_transformer"].transform(counts)
    if tfidf.nnz == 0:
        return [], True
    latent = model["svd"].transform(tfidf)
    latent = model["normalize"](latent, norm="l2", copy=False)
    scores = model["latent"] @ latent[0]
    order = model["np"].argsort(-scores, kind="stable")[:top_k]
    ranking = [(model["commands"][int(i)], float(scores[int(i)])) for i in order]
    return ranking, False


def evaluate(corpus: dict, index: manprobe.SearchIndex, model: dict, top_k: int) -> dict:
    cases: list[dict] = []
    available_cases = top1 = top3 = top10 = zero_queries = 0
    for case in corpus.get("cases", []):
        accepted = _accepted(case)
        available = sorted(accepted & index.documents.keys())
        ranking, zero_query = rank_query(str(case.get("query", "")), model, top_k)
        rank = next((i for i, (command, _) in enumerate(ranking, 1) if command in accepted), None)
        if available:
            available_cases += 1
            top1 += rank == 1
            top3 += rank is not None and rank <= 3
            top10 += rank is not None and rank <= 10
        zero_queries += int(zero_query)
        cases.append({
            "id": str(case.get("id", "")),
            "query": str(case.get("query", "")),
            "accepted": sorted(accepted),
            "available_expected": available,
            "expected_rank": rank,
            "zero_query_vector": zero_query,
            "top": [
                {"command": command, "score": round(score, 6)}
                for command, score in ranking[:5]
            ],
        })
    return {
        "corpus": str(corpus.get("name", "")),
        "corpus_path": str(corpus.get("_path", "")),
        "available_cases": available_cases,
        "top1": top1,
        "top3": top3,
        "top10": top10,
        "zero_query_vectors": zero_queries,
        "cases": cases,
    }


def _print_corpus(report: dict) -> None:
    available = report["available_cases"]
    print(f"Corpus: {report['corpus']}")
    if available:
        print(f"  Top-1 recovery:  {report['top1']}/{available}  {100.0 * report['top1'] / available:.1f}%")
        print(f"  Top-3 recovery:  {report['top3']}/{available}  {100.0 * report['top3'] / available:.1f}%")
        print(f"  Top-10 recovery: {report['top10']}/{available}  {100.0 * report['top10'] / available:.1f}%")
    print(f"  Zero query vectors: {report['zero_query_vectors']}")
    print()
    for case in report["cases"]:
        if not case["available_expected"]:
            state = "NO-EXPECTED-MAN"
        elif case["zero_query_vector"]:
            state = "ZERO-QUERY"
        elif case["expected_rank"] is None:
            state = "MISS"
        else:
            state = f"RANK {case['expected_rank']}"
        top = ", ".join(item["command"] for item in case["top"]) or "<none>"
        print(f"  {case['id']:<38} {state:<16} top={top}")
    print()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--corpus",
        action="append",
        type=Path,
        default=[],
        help="Corpus to evaluate; may be repeated. Defaults to research-v1.",
    )
    parser.add_argument("--dimensions", type=int, default=128)
    parser.add_argument("--min-df", type=int, default=2)
    parser.add_argument("--max-features", type=int, default=50000)
    parser.add_argument("--top-k", type=int, default=100)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--man-root", action="append", type=Path, default=[])
    args = parser.parse_args()

    if args.dimensions < 2:
        print("error: --dimensions must be at least 2", file=sys.stderr)
        return 2
    if args.min_df < 1:
        print("error: --min-df must be at least 1", file=sys.stderr)
        return 2
    if args.max_features < 100:
        print("error: --max-features must be at least 100", file=sys.stderr)
        return 2
    if args.top_k < 10:
        print("error: --top-k must be at least 10", file=sys.stderr)
        return 2

    corpus_paths = args.corpus or [Path("benchmarks/corpus-m11-research-v1.json")]
    corpora = _load_corpora(corpus_paths)

    started = time.perf_counter()
    roots = args.man_root or manprobe.man_roots()
    discover_started = time.perf_counter()
    pages = manprobe.discover_pages(roots)
    discover_done = time.perf_counter()
    index, skipped = manprobe.build_documents(pages)
    tokenize_done = time.perf_counter()
    if not index.documents:
        print("error: no readable section 1/8 local man pages found", file=sys.stderr)
        return 3

    try:
        model = build_lsa(index, args.dimensions, args.min_df, args.max_features)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 3
    model_done = time.perf_counter()

    reports = [evaluate(corpus, index, model, args.top_k) for corpus in corpora]
    evaluation_done = time.perf_counter()

    output = {
        "experiment": "m11-local-man-lsa-v1",
        "method": "local full-man TF-IDF + TruncatedSVD + cosine",
        "man_roots": [str(root) for root in roots],
        "pages_discovered": len(pages),
        "indexed_commands": len(index.documents),
        "indexed_tokens": index.token_count,
        "features": model["features"],
        "min_df": model["min_df"],
        "max_features": model["max_features"],
        "matrix_nnz": model["nnz"],
        "dimensions": model["dimensions"],
        "explained_variance_ratio_sum": round(model["explained_variance"], 6),
        "skipped_pages": dict(sorted(skipped.items())),
        "peak_rss_mib": round(_rss_mib(), 3),
        "timings_ms": {
            "discover": round((discover_done - discover_started) * 1000.0, 3),
            "tokenize_index": round((tokenize_done - discover_done) * 1000.0, 3),
            "lsa_fit": round((model_done - tokenize_done) * 1000.0, 3),
            "evaluate": round((evaluation_done - model_done) * 1000.0, 3),
            "total": round((evaluation_done - started) * 1000.0, 3),
        },
        "corpora": reports,
    }

    print("Acclorite M11 local-man latent-semantic probe")
    print("─" * 76)
    print(f"Pages discovered:   {output['pages_discovered']}")
    print(f"Indexed commands:   {output['indexed_commands']}")
    print(f"Indexed tokens:     {output['indexed_tokens']}")
    print(f"Sparse features:    {output['features']} (min_df={output['min_df']}, cap={output['max_features']})")
    print(f"Latent dimensions:  {output['dimensions']}")
    print(f"Explained variance: {100.0 * output['explained_variance_ratio_sum']:.1f}%")
    print(f"Peak RSS:           {output['peak_rss_mib']:.1f} MiB")
    timing = output["timings_ms"]
    print(
        "Probe timing:       "
        f"discover {timing['discover']:.1f} ms · "
        f"tokenize/index {timing['tokenize_index']:.1f} ms · "
        f"LSA fit {timing['lsa_fit']:.1f} ms · "
        f"evaluate {timing['evaluate']:.1f} ms · "
        f"total {timing['total']:.1f} ms"
    )
    print()
    for report in reports:
        _print_corpus(report)

    if args.json_out:
        try:
            args.json_out.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
        except OSError as exc:
            print(f"error: cannot write JSON report: {exc}", file=sys.stderr)
            return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
