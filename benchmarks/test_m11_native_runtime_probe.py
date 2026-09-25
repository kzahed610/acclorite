#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
SPEC = importlib.util.spec_from_file_location("rq2", HERE / "m11_native_runtime_probe.py")
rq2 = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(rq2)


class NativeRuntimeProbeTests(unittest.TestCase):
    def test_parse_tokens_requires_complete_rows(self):
        self.assertEqual(rq2._parse_tokens("@tokens\t0\t101,102\n", 1), [[101, 102]])
        with self.assertRaises(ValueError):
            rq2._parse_tokens("@tokens\t1\t101,102\n", 1)

    def test_token_parity_is_fail_closed(self):
        same = rq2._token_parity([[101, 1, 102]], [[101, 1, 102]], 1)
        self.assertTrue(same["pass"])
        diff = rq2._token_parity([[101, 2, 102]], [[101, 1, 102]], 1)
        self.assertFalse(diff["pass"])
        self.assertEqual(diff["mismatch_count"], 1)

    def test_rq1_reference_requires_successful_q1(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "rq1.json"
            path.write_text(json.dumps({
                "qualification": "m11-semantic-runtime-onnx-q1",
                "quality_gate": {"pass": True},
            }), encoding="utf-8")
            loaded = rq2._load_reference(path)
            self.assertTrue(loaded["quality_gate"]["pass"])
            path.write_text(json.dumps({
                "qualification": "m11-semantic-runtime-onnx-q1",
                "quality_gate": {"pass": False},
            }), encoding="utf-8")
            with self.assertRaises(ValueError):
                rq2._load_reference(path)

    def test_quality_gate_reuses_rq1_tolerance(self):
        reference = {
            "onnx_rescue": {
                "primary": {"misses": 10, "rescued_at_1": 4, "rescued_at_3": 6, "rescued_at_5": 7},
                "top3": {"misses": 9, "rescued_at_1": 4, "rescued_at_3": 6, "rescued_at_5": 7},
                "top10": {"misses": 8, "rescued_at_1": 4, "rescued_at_3": 6, "rescued_at_5": 7},
            }
        }
        candidate = {
            "primary": {"misses": 10, "rescued_at_1": 3, "rescued_at_3": 5, "rescued_at_5": 6},
            "top3": {"misses": 9, "rescued_at_1": 3, "rescued_at_3": 5, "rescued_at_5": 6},
            "top10": {"misses": 8, "rescued_at_1": 3, "rescued_at_3": 5, "rescued_at_5": 6},
        }
        _, passed = rq2._quality_against_rq1(reference, candidate)
        self.assertTrue(passed)
        candidate["top10"]["rescued_at_5"] = 5
        _, passed = rq2._quality_against_rq1(reference, candidate)
        self.assertFalse(passed)


if __name__ == "__main__":
    unittest.main()
