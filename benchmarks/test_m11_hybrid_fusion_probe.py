#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("m11_hybrid_fusion_probe", HERE / "m11_hybrid_fusion_probe.py")
assert SPEC and SPEC.loader
probe = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = probe
SPEC.loader.exec_module(probe)

# Equal RRF must reward agreement without depending on raw-score scales.
semantic = ["sem-only", "shared", "third"]
deterministic = ["det-only", "shared", "fourth"]
fused = probe.reciprocal_rank_fusion(semantic, deterministic, k=60.0, limit=5)
assert fused[0] == "shared", fused
assert set(fused) == {"sem-only", "shared", "third", "det-only", "fourth"}, fused

# Semantic-heavy RRF should retain a strong semantic-only candidate ahead of a
# deterministic-only candidate at the same list position.
fused_sem2 = probe.reciprocal_rank_fusion(
    ["semantic-winner", "shared"],
    ["deterministic-winner", "shared"],
    semantic_weight=2.0,
    deterministic_weight=1.0,
    k=60.0,
    limit=3,
)
assert fused_sem2[0] == "shared", fused_sem2
assert fused_sem2.index("semantic-winner") < fused_sem2.index("deterministic-winner"), fused_sem2

cases = [
    {"expect": {"top1_any": ["good"]}},
    {"expect": {"top3_any": ["yes"]}},
]
metric = probe.summarize([["good"], ["no", "yes"]], cases)
assert metric == {"cases": 2, "top1": 1, "top3": 2, "top10": 2}, metric
assert probe.union_coverage(["a", "good"], ["x"], {"good"}, 10)

print("m11 hybrid fusion probe tests: PASS")
