#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("assist", ROOT / "m11_semantic_assist_acceptance.py")
assert SPEC and SPEC.loader
assist = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(assist)


def main() -> int:
    assert assist.semantic_alternatives(
        ["grep", "rg", "ag"], ["rg", "ripgrep-all", "grep", "fd"], baseline_depth=3, limit=5
    ) == ["ripgrep-all", "fd"]
    assert assist.semantic_alternatives(
        ["wrong", "other"], ["right", "wrong", "third"], baseline_depth=10, limit=2
    ) == ["right", "third"]
    assert assist.semantic_alternatives([], ["right", "right", "other"], limit=5) == ["right", "other"]

    cases = [{"id": "x", "accepted": ["right"]}, {"id": "y", "accepted": ["known"]}]
    baseline = [["wrong", "noise"], ["known", "noise"]]
    alternatives = [["right", "third"], ["other"]]
    metric = assist.summarize_baseline(baseline, cases)
    assert metric == {"cases": 2, "top1": 1, "top3": 1, "top10": 1}
    rescue = assist.rescue_summary(baseline, alternatives, cases, 1)
    assert rescue["misses"] == 1
    assert rescue["rescued_at_1"] == 1
    assert rescue["rescued_ids_at_1"] == ["x"]
    assert assist.combined_discovery(metric, rescue, 1) == 2

    # Separate-channel presentation must never mutate deterministic ranking.
    before = list(baseline[0])
    _ = assist.semantic_alternatives(baseline[0], alternatives[0])
    assert baseline[0] == before
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
