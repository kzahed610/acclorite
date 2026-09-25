#!/usr/bin/env python3
"""Dependency-free tests for M11 Semantic Assist Runtime Qualification 1."""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import m11_semantic_runtime_probe as probe


def fake_rescue(a: int, b: int, c: int, misses: int = 10) -> dict:
    return {
        "misses": misses,
        "rescued_at_1": a,
        "rescued_at_3": b,
        "rescued_at_5": c,
    }


def test_quality_delta_and_gate() -> None:
    reference = {
        "primary": fake_rescue(4, 6, 7),
        "top3": fake_rescue(5, 7, 8),
        "top10": fake_rescue(6, 8, 9),
    }
    close = {
        "primary": fake_rescue(3, 5, 6),
        "top3": fake_rescue(5, 7, 7),
        "top10": fake_rescue(5, 8, 9),
    }
    delta = probe._quality_delta(reference, close)
    assert delta["primary"]["delta"]["assist3"] == -1
    assert delta["primary"]["delta"]["assist5"] == -1
    assert probe._passes_quality_gate(delta)

    bad = {
        "primary": fake_rescue(4, 4, 5),
        "top3": fake_rescue(5, 7, 8),
        "top10": fake_rescue(6, 8, 9),
    }
    assert not probe._passes_quality_gate(probe._quality_delta(reference, bad))


def test_reference_map() -> None:
    source = {"corpora": [{"corpus": "one", "cases": []}, {"corpus": "two", "cases": []}]}
    mapped = probe._reference_map(source)
    assert set(mapped) == {"one", "two"}




def test_raw_corpus_expectations_are_normalized_for_rescue() -> None:
    cases = [
        {
            "id": "raw-schema",
            "expect": {"top1_any": ["right"], "top3_any": ["right", "alias"]},
        }
    ]
    baseline = [["wrong"]]
    supported = [["wrong", "right"]]
    rescue = probe._rescue_counts(baseline, supported, cases)
    assert rescue["primary"]["misses"] == 1
    assert rescue["primary"]["rescued_at_1"] == 1
    assert rescue["top10"]["misses"] == 1
    assert rescue["top10"]["rescued_at_1"] == 1


def test_reference_miss_count_invariant() -> None:
    cases = [{"id": "one", "expect": {"top1_any": ["right"]}}]
    reference = {"baseline": {"cases": 1, "top1": 1, "top3": 1, "top10": 1}}
    consistent = {
        "primary": fake_rescue(0, 0, 0, misses=0),
        "top3": fake_rescue(0, 0, 0, misses=0),
        "top10": fake_rescue(0, 0, 0, misses=0),
    }
    probe._validate_reference_miss_counts(reference, consistent, cases)

    broken = {
        "primary": fake_rescue(0, 0, 0, misses=1),
        "top3": fake_rescue(0, 0, 0, misses=1),
        "top10": fake_rescue(0, 0, 0, misses=1),
    }
    try:
        probe._validate_reference_miss_counts(reference, broken, cases)
    except ValueError:
        pass
    else:
        raise AssertionError("reference mismatch must fail closed")

def main() -> int:
    test_quality_delta_and_gate()
    test_reference_map()
    test_raw_corpus_expectations_are_normalized_for_rescue()
    test_reference_miss_count_invariant()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
