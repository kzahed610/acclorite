#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).resolve().parent / "run.py"
spec = importlib.util.spec_from_file_location("acclorite_benchmark", MODULE_PATH)
assert spec is not None and spec.loader is not None
bench = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = bench
spec.loader.exec_module(bench)


class HarnessTests(unittest.TestCase):
    def test_evaluate_accepts_equivalent_top1_and_confidence(self) -> None:
        case = {
            "id": "dupes",
            "query": "duplicate files",
            "tags": [],
            "expect": {
                "top1_any": ["fdupes", "rdfind"],
                "top3_any": ["fdupes", "rdfind"],
                "ambiguity": "competitive",
                "min_confidence": 0.70,
            },
        }
        payload = {
            "query_frame": {"type": "Discover"},
            "confidence": {"ambiguity": "competitive", "top_candidate": 0.75},
            "results": [{"command": "rdfind"}, {"command": "fdupes"}],
        }
        result = bench.evaluate(case, payload, 0, 12.0)
        self.assertTrue(result.passed)

    def test_failure_records_expected_and_actual(self) -> None:
        case = {
            "id": "search",
            "query": "search text",
            "tags": [],
            "expect": {"top1_any": ["rg"], "forbid_top1": ["pdfgrep"]},
        }
        payload = {
            "query_frame": {"type": "Discover"},
            "confidence": {"ambiguity": "clear", "top_candidate": 0.8},
            "results": [{"command": "pdfgrep"}],
        }
        result = bench.evaluate(case, payload, 0, 10.0)
        self.assertFalse(result.passed)
        self.assertEqual(2, len(result.assertions))
        self.assertFalse(result.assertions[0].passed)
        self.assertFalse(result.assertions[1].passed)


    def test_timeout_keeps_declared_assertions_in_denominator(self) -> None:
        case = {
            "id": "timeout",
            "query": "slow query",
            "tags": [],
            "expect": {
                "top1_any": ["rg"],
                "ambiguity": "clear",
                "min_results": 1,
            },
        }
        assertions = bench._error_assertions(case, "timeout after 20.0s")
        self.assertEqual(3, len(assertions))
        self.assertTrue(all(not item.passed for item in assertions))

    def test_forensics_extracts_semantic_ranking_details(self) -> None:
        payload = {
            "results": [
                {
                    "command": "wrong-tool",
                    "source": "path+man",
                    "summary": "specialized helper",
                    "score": 0.9,
                    "ranking": {
                        "semantic_fit": 0.72,
                        "semantic_tier": "plausible",
                        "final_utility": 0.94,
                        "semantic_adjustments": [{"id": "scope-role"}],
                    },
                }
            ]
        }
        details = bench._forensic_candidates(payload)
        self.assertEqual(1, len(details))
        self.assertEqual("wrong-tool", details[0]["command"])
        self.assertEqual(["scope-role"], details[0]["semantic_adjustments"])

    def test_report_metrics_are_assertion_scoped(self) -> None:
        passed = bench.CaseResult(
            case_id="a", query="a", tags=[], return_code=0, latency_ms=10.0,
            top1="rg", top3=["rg"], ambiguity="clear", confidence=0.8, frame="Discover",
            assertions=[
                bench.AssertionResult("top1_any", True, ["rg"], "rg"),
                bench.AssertionResult("ambiguity", True, "clear", "clear"),
            ],
            search_ms=8.0,
            stage_timings={"source:index": 3.0, "ranking-finalize": 1.0},
        )
        failed = bench.CaseResult(
            case_id="b", query="b", tags=[], return_code=0, latency_ms=30.0,
            top1="grep", top3=["grep"], ambiguity="clear", confidence=0.6, frame="Discover",
            assertions=[bench.AssertionResult("top1_any", False, ["rg"], "grep")],
            search_ms=18.0,
            stage_timings={"source:index": 5.0, "ranking-finalize": 2.0},
        )
        report = bench.build_report(Path("/bin/true"), Path("corpus.json"), {"schema_version": 1, "name": "x"}, [passed, failed])
        self.assertEqual(1, report["summary"]["cases_passed"])
        self.assertEqual(2, report["summary"]["cases_total"])
        self.assertEqual(1, report["summary"]["top1"]["passed"])
        self.assertEqual(2, report["summary"]["top1"]["total"])
        self.assertAlmostEqual(20.0, report["summary"]["latency_ms"]["median"])
        self.assertAlmostEqual(13.0, report["summary"]["profiled_search_ms"]["median"])
        self.assertAlmostEqual(4.0, report["summary"]["stage_timings_ms"]["source:index"]["median"])
        self.assertEqual(3, report["benchmark_report_schema"])


if __name__ == "__main__":
    unittest.main()
