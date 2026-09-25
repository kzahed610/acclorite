#!/usr/bin/env python3
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("probe", ROOT / "benchmarks" / "m11_agreement_gate_probe.py")
probe = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
SPEC.loader.exec_module(probe)


def test_semantic_rank():
    sem = ["a", "b", "c"]
    assert probe.semantic_rank(sem, "a", 20) == 1
    assert probe.semantic_rank(sem, "c", 20) == 3
    assert probe.semantic_rank(sem, "x", 20) == 21


def test_anchor_policies():
    sem = ["replacement"] + [f"x{i}" for i in range(1, 11)] + ["baseline"]
    base = ["baseline"]
    support = ["replacement"]
    assert probe.policy_activates(base, support, sem, "anchor-top10", 20)
    assert not probe.policy_activates(base, support, sem, "anchor-top20", 20)


def test_rank_gap_policies():
    sem = ["replacement", "x1", "x2", "x3", "x4", "x5", "baseline"]
    base = ["baseline"]
    support = ["replacement"]
    assert probe.policy_activates(base, support, sem, "rank-gap-5", 20)
    assert not probe.policy_activates(base, support, sem, "rank-gap-10", 20)
    assert not probe.policy_activates(base, base, sem, "rank-gap-5", 20)


def main():
    test_semantic_rank()
    test_anchor_policies()
    test_rank_gap_policies()
    print("m11 agreement-gate probe tests passed")


if __name__ == "__main__":
    main()
