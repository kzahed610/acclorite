#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import m11_confidence_gate_probe as probe


def main() -> int:
    clear_high = {"ambiguity": "clear", "confidence": 0.81, "level": "high"}
    clear_medium = {"ambiguity": "clear", "confidence": 0.66, "level": "medium"}
    low = {"ambiguity": "low-confidence", "confidence": 0.52, "level": "low"}
    competitive_high = {"ambiguity": "competitive", "confidence": 0.78, "level": "high"}

    assert not probe.gate_activates(clear_high, "uncertain-state")
    assert not probe.gate_activates(clear_medium, "uncertain-state")
    assert probe.gate_activates(low, "uncertain-state")
    assert probe.gate_activates(competitive_high, "uncertain-state")

    assert not probe.gate_activates(clear_high, "non-high-confidence")
    assert probe.gate_activates(clear_medium, "non-high-confidence")
    assert probe.gate_activates(low, "non-high-confidence")
    assert not probe.gate_activates(competitive_high, "non-high-confidence")

    assert not probe.gate_activates(clear_high, "uncertain-or-non-high")
    assert probe.gate_activates(clear_medium, "uncertain-or-non-high")
    assert probe.gate_activates(low, "uncertain-or-non-high")
    assert probe.gate_activates(competitive_high, "uncertain-or-non-high")

    base = [["good"], ["wrong"]]
    supported = [["bad"], ["good"]]
    meta = [clear_high, low]
    chosen, activated = probe.choose_rankings(base, supported, meta, "uncertain-or-non-high")
    assert chosen == [["good"], ["good"]]
    assert activated == [False, True]

    cases = [
        {"id": "keep", "expect": {"top1_any": ["good"]}},
        {"id": "recover", "expect": {"top1_any": ["good"]}},
    ]
    assert probe.regression_ids(base, chosen, cases, 1) == []
    assert probe.recovery_ids(base, chosen, cases, 1) == ["recover"]

    cal = probe.calibration(meta, base, cases)
    assert cal["clear"]["cases"] == 1 and cal["clear"]["top1"] == 1
    assert cal["low-confidence"]["cases"] == 1 and cal["low-confidence"]["top1"] == 0

    print("m11 confidence gate probe tests: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
