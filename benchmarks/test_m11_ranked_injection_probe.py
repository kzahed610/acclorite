#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import m11_ranked_injection_probe as probe


def main() -> int:
    commands = [f"tool-{i}" for i in range(1, 22)]

    uniform = probe.ranked_hints(commands, "plausible-uniform", 20)
    assert len(uniform) == 20
    assert all(floor == 0.70 for _, floor in uniform)

    conservative = probe.ranked_hints(commands, "conservative-ranked", 20)
    assert conservative[0][1] == 0.84
    assert conservative[1][1] == 0.80 and conservative[2][1] == 0.80
    assert conservative[3][1] == 0.74 and conservative[9][1] == 0.74
    assert conservative[10][1] == 0.70 and conservative[19][1] == 0.70

    tiered = probe.ranked_hints(commands, "tiered-ranked", 20)
    assert tiered[0][1] == 0.88
    assert tiered[1][1] == 0.84 and tiered[2][1] == 0.84
    assert tiered[3][1] == 0.78 and tiered[9][1] == 0.78
    assert tiered[10][1] == 0.70
    assert max(floor for _, floor in tiered) <= 0.88

    cases = [{"expect": {"top1_any": ["right"]}}]
    delta = probe.summarize_delta([["wrong"]], [["right"]], cases)
    assert delta["new_top1"] == 1 and delta["top1_regressions"] == 0
    reverse = probe.summarize_delta([["right"]], [["wrong"]], cases)
    assert reverse["new_top1"] == 0 and reverse["top1_regressions"] == 1

    print("m11 ranked injection probe tests: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
