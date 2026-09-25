#!/usr/bin/env python3
"""Acclorite deterministic benchmark harness.

This is evaluation tooling, not part of the Acclorite runtime. It executes the
compiled CLI through its stable JSON surface and evaluates a versioned corpus
without depending on internal C++ implementation details.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import platform
import statistics
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable

REPORT_SCHEMA_VERSION = 3


@dataclass
class AssertionResult:
    name: str
    passed: bool
    expected: Any
    actual: Any


@dataclass
class CaseResult:
    case_id: str
    query: str
    tags: list[str]
    return_code: int
    latency_ms: float
    top1: str
    top3: list[str]
    ambiguity: str
    confidence: float
    frame: str
    assertions: list[AssertionResult]
    search_ms: float = 0.0
    stage_timings: dict[str, float] | None = None
    forensics: list[dict[str, Any]] | None = None
    error: str = ""

    @property
    def passed(self) -> bool:
        return not self.error and all(item.passed for item in self.assertions)


def _percentile(values: list[float], percentile: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * percentile
    low = math.floor(position)
    high = math.ceil(position)
    if low == high:
        return ordered[low]
    fraction = position - low
    return ordered[low] * (1.0 - fraction) + ordered[high] * fraction


def _run_json(
    binary: Path,
    query: str,
    timeout: float,
    profile: bool = True,
    explain_ranking: bool = False,
) -> tuple[int, float, dict[str, Any], str]:
    command = [str(binary), "--json"]
    if profile:
        command.append("--profile")
    if explain_ranking:
        command.append("--explain-ranking")
    command.append(query)
    started = time.perf_counter()
    try:
        completed = subprocess.run(
            command,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        elapsed = (time.perf_counter() - started) * 1000.0
        return 124, elapsed, {}, f"timeout after {timeout:.1f}s"
    elapsed = (time.perf_counter() - started) * 1000.0

    try:
        payload = json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        detail = completed.stderr.strip() or completed.stdout.strip()
        return completed.returncode, elapsed, {}, f"invalid JSON: {exc}; output={detail[:240]!r}"

    if not isinstance(payload, dict):
        return completed.returncode, elapsed, {}, "JSON root is not an object"
    return completed.returncode, elapsed, payload, ""


def _names(results: Iterable[dict[str, Any]], limit: int) -> list[str]:
    names: list[str] = []
    for item in list(results)[:limit]:
        command = item.get("command", "")
        if isinstance(command, str) and command:
            names.append(command)
    return names


def _assert(name: str, passed: bool, expected: Any, actual: Any) -> AssertionResult:
    return AssertionResult(name=name, passed=passed, expected=expected, actual=actual)


def _contains_all(actual: Iterable[str], required: Iterable[str]) -> bool:
    actual_set = set(actual)
    return all(item in actual_set for item in required)


def _timing(payload: dict[str, Any]) -> tuple[float, dict[str, float]]:
    raw = payload.get("timing")
    if not isinstance(raw, dict):
        return 0.0, {}
    total = raw.get("total_ms", 0.0)
    total_ms = float(total) if isinstance(total, (int, float)) else 0.0
    stages: dict[str, float] = {}
    raw_stages = raw.get("stages", [])
    if isinstance(raw_stages, list):
        for item in raw_stages:
            if not isinstance(item, dict):
                continue
            name = item.get("name", "")
            value = item.get("milliseconds", 0.0)
            if not isinstance(name, str) or not name or not isinstance(value, (int, float)):
                continue
            stages[name] = stages.get(name, 0.0) + float(value)
    return total_ms, stages


def _forensic_candidates(payload: dict[str, Any], limit: int = 5) -> list[dict[str, Any]]:
    raw_results = payload.get("results", [])
    if not isinstance(raw_results, list):
        return []
    details: list[dict[str, Any]] = []
    for item in raw_results[:limit]:
        if not isinstance(item, dict):
            continue
        ranking = item.get("ranking")
        ranking = ranking if isinstance(ranking, dict) else {}
        adjustments = ranking.get("semantic_adjustments", [])
        adjustment_ids = [
            str(adjustment.get("id", ""))
            for adjustment in adjustments
            if isinstance(adjustment, dict) and adjustment.get("id")
        ] if isinstance(adjustments, list) else []
        details.append({
            "command": str(item.get("command", "")),
            "package": str(item.get("package", "")),
            "source": str(item.get("source", "")),
            "summary": str(item.get("summary", "")),
            "score": float(item.get("score", 0.0)) if isinstance(item.get("score"), (int, float)) else 0.0,
            "semantic_fit": float(ranking.get("semantic_fit", 0.0)) if isinstance(ranking.get("semantic_fit"), (int, float)) else 0.0,
            "semantic_tier": str(ranking.get("semantic_tier", "")),
            "final_utility": float(ranking.get("final_utility", 0.0)) if isinstance(ranking.get("final_utility"), (int, float)) else 0.0,
            "semantic_adjustments": adjustment_ids,
        })
    return details


def _error_assertions(case: dict[str, Any], error: str) -> list[AssertionResult]:
    expected = case.get("expect", {})
    if not isinstance(expected, dict):
        return []
    known = (
        "top1_any", "top3_any", "top10_all", "forbid_top1", "ambiguity", "frame",
        "min_confidence", "targets_all", "targets_none", "clarification_ids",
        "locations_nonempty", "min_results", "action_target_kind",
        "action_target_command", "action_target_terms_all", "actionable_present",
        "actionable_command", "actionable_option_any", "actionable_subcommand_any",
        "actionable_safety", "invocation_present", "invocation_complete", "invocation_command",
        "invocation_arguments", "invocation_arguments_all", "invocation_placeholder_count",
    )
    actual = f"<query error: {error}>"
    return [
        _assert(name, False, expected[name], actual)
        for name in known
        if name in expected
    ]


def evaluate(case: dict[str, Any], payload: dict[str, Any], return_code: int, latency_ms: float) -> CaseResult:
    expected = case.get("expect", {})
    raw_results = payload.get("results", [])
    results = raw_results if isinstance(raw_results, list) else []
    top10 = _names(results, 10)
    top3 = top10[:3]
    top1 = top10[0] if top10 else ""

    confidence_obj = payload.get("confidence", {})
    if not isinstance(confidence_obj, dict):
        confidence_obj = {}
    ambiguity = str(confidence_obj.get("ambiguity", ""))
    confidence = confidence_obj.get("top_candidate", 0.0)
    confidence = float(confidence) if isinstance(confidence, (int, float)) else 0.0

    frame_obj = payload.get("query_frame", {})
    if not isinstance(frame_obj, dict):
        frame_obj = {}
    frame = str(frame_obj.get("type", ""))

    assertions: list[AssertionResult] = []

    if "top1_any" in expected:
        accepted = list(expected["top1_any"])
        assertions.append(_assert("top1_any", top1 in accepted, accepted, top1))

    if "top3_any" in expected:
        accepted = list(expected["top3_any"])
        hit = any(item in accepted for item in top3)
        assertions.append(_assert("top3_any", hit, accepted, top3))

    if "top10_all" in expected:
        required = list(expected["top10_all"])
        assertions.append(_assert("top10_all", _contains_all(top10, required), required, top10))

    if "forbid_top1" in expected:
        forbidden = list(expected["forbid_top1"])
        assertions.append(_assert("forbid_top1", top1 not in forbidden, forbidden, top1))

    if "ambiguity" in expected:
        target = str(expected["ambiguity"])
        assertions.append(_assert("ambiguity", ambiguity == target, target, ambiguity))

    if "frame" in expected:
        target = str(expected["frame"])
        assertions.append(_assert("frame", frame == target, target, frame))

    if "min_confidence" in expected:
        target = float(expected["min_confidence"])
        assertions.append(_assert("min_confidence", confidence >= target, target, confidence))

    if "targets_all" in expected:
        required = list(expected["targets_all"])
        actual_targets = payload.get("targets", [])
        if not isinstance(actual_targets, list):
            actual_targets = []
        assertions.append(_assert("targets_all", _contains_all(actual_targets, required), required, actual_targets))

    if "targets_none" in expected:
        want = bool(expected["targets_none"])
        actual_targets = payload.get("targets", [])
        actual = not isinstance(actual_targets, list) or not actual_targets
        assertions.append(_assert("targets_none", actual == want, want, actual))

    raw_action_target = payload.get("action_target")
    action_target = raw_action_target if isinstance(raw_action_target, dict) else {}

    if "action_target_kind" in expected:
        target = str(expected["action_target_kind"])
        actual = str(action_target.get("kind", ""))
        assertions.append(_assert("action_target_kind", actual == target, target, actual))

    if "action_target_command" in expected:
        target = str(expected["action_target_command"])
        actual = str(action_target.get("command", ""))
        assertions.append(_assert("action_target_command", actual == target, target, actual))

    if "action_target_terms_all" in expected:
        required = list(expected["action_target_terms_all"])
        actual_terms = action_target.get("terms", [])
        if not isinstance(actual_terms, list):
            actual_terms = []
        assertions.append(_assert(
            "action_target_terms_all",
            _contains_all(actual_terms, required),
            required,
            actual_terms,
        ))

    raw_actionable = payload.get("actionable_answer")
    actionable = raw_actionable if isinstance(raw_actionable, dict) else {}

    if "actionable_present" in expected:
        want = bool(expected["actionable_present"])
        actual = bool(actionable)
        assertions.append(_assert("actionable_present", actual == want, want, actual))

    if "actionable_command" in expected:
        target = str(expected["actionable_command"])
        actual = str(actionable.get("command", ""))
        assertions.append(_assert("actionable_command", actual == target, target, actual))

    if "actionable_option_any" in expected:
        accepted = list(expected["actionable_option_any"])
        actual_names: list[str] = []
        raw_options = actionable.get("relevant_options", [])
        if isinstance(raw_options, list):
            for item in raw_options:
                if not isinstance(item, dict):
                    continue
                names = item.get("names", [])
                if isinstance(names, list):
                    actual_names.extend(str(name) for name in names if isinstance(name, str))
        hit = any(name in accepted for name in actual_names)
        assertions.append(_assert("actionable_option_any", hit, accepted, actual_names))

    if "actionable_subcommand_any" in expected:
        accepted = list(expected["actionable_subcommand_any"])
        actual_names: list[str] = []
        raw_subcommands = actionable.get("relevant_subcommands", [])
        if isinstance(raw_subcommands, list):
            actual_names = [
                str(item.get("name", ""))
                for item in raw_subcommands
                if isinstance(item, dict) and isinstance(item.get("name", ""), str)
            ]
        hit = any(name in accepted for name in actual_names)
        assertions.append(_assert("actionable_subcommand_any", hit, accepted, actual_names))

    if "actionable_safety" in expected:
        want = str(expected["actionable_safety"])
        actual = str(actionable.get("safety", ""))
        assertions.append(_assert("actionable_safety", actual == want, want, actual))

    invocation = actionable.get("invocation")
    invocation = invocation if isinstance(invocation, dict) else {}

    if "invocation_present" in expected:
        want = bool(expected["invocation_present"])
        actual = bool(invocation)
        assertions.append(_assert("invocation_present", actual == want, want, actual))

    if "invocation_complete" in expected:
        want = bool(expected["invocation_complete"])
        actual = bool(invocation.get("complete", False)) if invocation else False
        assertions.append(_assert("invocation_complete", actual == want, want, actual))

    if "invocation_command" in expected:
        want = str(expected["invocation_command"])
        actual = str(invocation.get("command", "")) if invocation else ""
        assertions.append(_assert("invocation_command", actual == want, want, actual))

    if "invocation_arguments" in expected:
        want = [str(value) for value in expected["invocation_arguments"]]
        raw_arguments = invocation.get("arguments", []) if invocation else []
        actual = [str(value) for value in raw_arguments] if isinstance(raw_arguments, list) else []
        assertions.append(_assert("invocation_arguments", actual == want, want, actual))

    if "invocation_arguments_all" in expected:
        required = [str(value) for value in expected["invocation_arguments_all"]]
        raw_arguments = invocation.get("arguments", []) if invocation else []
        actual = [str(value) for value in raw_arguments] if isinstance(raw_arguments, list) else []
        assertions.append(_assert(
            "invocation_arguments_all",
            _contains_all(actual, required),
            required,
            actual,
        ))

    if "invocation_placeholder_count" in expected:
        want = int(expected["invocation_placeholder_count"])
        raw_segments = invocation.get("segments", []) if invocation else []
        actual = 0
        if isinstance(raw_segments, list):
            actual = sum(
                1 for segment in raw_segments
                if isinstance(segment, dict) and bool(segment.get("placeholder", False))
            )
        assertions.append(_assert("invocation_placeholder_count", actual == want, want, actual))

    if "clarification_ids" in expected:
        required = list(expected["clarification_ids"])
        raw = payload.get("clarifications", [])
        actual_ids = [
            item.get("id", "") for item in raw
            if isinstance(item, dict) and isinstance(item.get("id", ""), str)
        ] if isinstance(raw, list) else []
        assertions.append(_assert("clarification_ids", _contains_all(actual_ids, required), required, actual_ids))

    if "locations_nonempty" in expected:
        want = bool(expected["locations_nonempty"])
        locations = payload.get("locations", [])
        actual = isinstance(locations, list) and bool(locations)
        assertions.append(_assert("locations_nonempty", actual == want, want, actual))

    if "min_results" in expected:
        target = int(expected["min_results"])
        assertions.append(_assert("min_results", len(results) >= target, target, len(results)))

    search_ms, stage_timings = _timing(payload)

    return CaseResult(
        case_id=str(case.get("id", "")),
        query=str(case.get("query", "")),
        tags=list(case.get("tags", [])),
        return_code=return_code,
        latency_ms=latency_ms,
        top1=top1,
        top3=top3,
        ambiguity=ambiguity,
        confidence=confidence,
        frame=frame,
        assertions=assertions,
        search_ms=search_ms,
        stage_timings=stage_timings,
        forensics=None,
    )


def metric_for_assertion(cases: list[CaseResult], name: str) -> dict[str, float | int]:
    selected = [assertion for case in cases for assertion in case.assertions if assertion.name == name]
    passed = sum(1 for assertion in selected if assertion.passed)
    total = len(selected)
    return {
        "passed": passed,
        "total": total,
        "rate": (passed / total) if total else 0.0,
    }


def build_report(binary: Path, corpus_path: Path, corpus: dict[str, Any], cases: list[CaseResult]) -> dict[str, Any]:
    latencies = [case.latency_ms for case in cases if not case.error]
    passed_cases = sum(1 for case in cases if case.passed)
    assertion_total = sum(len(case.assertions) for case in cases)
    assertion_passed = sum(sum(1 for item in case.assertions if item.passed) for case in cases)

    search_totals = [case.search_ms for case in cases if not case.error and case.search_ms > 0.0]
    stage_names = sorted({
        name
        for case in cases if not case.error and case.stage_timings
        for name in case.stage_timings
    })
    stage_summary: dict[str, dict[str, float | int]] = {}
    for name in stage_names:
        values = [
            case.stage_timings[name]
            for case in cases
            if not case.error and case.stage_timings and name in case.stage_timings
        ]
        stage_summary[name] = {
            "samples": len(values),
            "median": statistics.median(values) if values else 0.0,
            "p95": _percentile(values, 0.95),
            "max": max(values) if values else 0.0,
        }

    try:
        version = subprocess.run(
            [str(binary), "--version"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=5,
            check=False,
        ).stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        version = ""

    return {
        "benchmark_report_schema": REPORT_SCHEMA_VERSION,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "binary": str(binary),
        "acclorite_version": version,
        "corpus": str(corpus_path),
        "corpus_schema_version": corpus.get("schema_version"),
        "corpus_name": corpus.get("name", ""),
        "environment": {
            "platform": platform.platform(),
            "python": platform.python_version(),
        },
        "summary": {
            "cases_passed": passed_cases,
            "cases_total": len(cases),
            "case_pass_rate": (passed_cases / len(cases)) if cases else 0.0,
            "assertions_passed": assertion_passed,
            "assertions_total": assertion_total,
            "assertion_pass_rate": (assertion_passed / assertion_total) if assertion_total else 0.0,
            "top1": metric_for_assertion(cases, "top1_any"),
            "top3": metric_for_assertion(cases, "top3_any"),
            "frame": metric_for_assertion(cases, "frame"),
            "ambiguity": metric_for_assertion(cases, "ambiguity"),
            "action_target": metric_for_assertion(cases, "action_target_kind"),
            "actionable": metric_for_assertion(cases, "actionable_present"),
            "latency_ms": {
                "median": statistics.median(latencies) if latencies else 0.0,
                "p95": _percentile(latencies, 0.95),
                "max": max(latencies) if latencies else 0.0,
            },
            "profiled_search_ms": {
                "median": statistics.median(search_totals) if search_totals else 0.0,
                "p95": _percentile(search_totals, 0.95),
                "max": max(search_totals) if search_totals else 0.0,
            },
            "stage_timings_ms": stage_summary,
        },
        "cases": [
            {
                **{key: value for key, value in asdict(case).items() if key != "assertions"},
                "passed": case.passed,
                "assertions": [asdict(item) for item in case.assertions],
            }
            for case in cases
        ],
    }


def render_terminal(report: dict[str, Any]) -> str:
    summary = report["summary"]
    top1 = summary["top1"]
    top3 = summary["top3"]
    frame = summary["frame"]
    ambiguity = summary["ambiguity"]
    action_target = summary.get("action_target", {"passed": 0, "total": 0, "rate": 0.0})
    actionable = summary.get("actionable", {"passed": 0, "total": 0, "rate": 0.0})
    latency = summary["latency_ms"]

    def pct(value: float) -> str:
        return f"{100.0 * value:5.1f}%"

    lines = [
        "Acclorite benchmark",
        "────────────────────────────────────────────────────────────────────────────",
        f"Version: {report.get('acclorite_version') or 'unknown'}",
        f"Corpus:  {report.get('corpus_name')} ({summary['cases_total']} cases)",
        "",
        f"Query pass rate     {summary['cases_passed']:>3}/{summary['cases_total']:<3}  {pct(summary['case_pass_rate'])}",
        f"Assertion pass rate {summary['assertions_passed']:>3}/{summary['assertions_total']:<3}  {pct(summary['assertion_pass_rate'])}",
        f"Top-1 accuracy      {top1['passed']:>3}/{top1['total']:<3}  {pct(top1['rate']) if top1['total'] else '  n/a'}",
        f"Top-3 hit rate      {top3['passed']:>3}/{top3['total']:<3}  {pct(top3['rate']) if top3['total'] else '  n/a'}",
        f"Frame accuracy      {frame['passed']:>3}/{frame['total']:<3}  {pct(frame['rate']) if frame['total'] else '  n/a'}",
        f"Ambiguity accuracy  {ambiguity['passed']:>3}/{ambiguity['total']:<3}  {pct(ambiguity['rate']) if ambiguity['total'] else '  n/a'}",
        f"Action-target acc.  {action_target['passed']:>3}/{action_target['total']:<3}  {pct(action_target['rate']) if action_target['total'] else '  n/a'}",
        f"Actionable presence {actionable['passed']:>3}/{actionable['total']:<3}  {pct(actionable['rate']) if actionable['total'] else '  n/a'}",
        f"Latency             p50 {latency['median']:.1f} ms · p95 {latency['p95']:.1f} ms · max {latency['max']:.1f} ms",
    ]
    profiled = summary.get("profiled_search_ms", {})
    if profiled.get("median", 0.0) > 0.0:
        lines.append(
            f"Internal search     p50 {profiled['median']:.1f} ms · p95 {profiled['p95']:.1f} ms · max {profiled['max']:.1f} ms"
        )
    stages = summary.get("stage_timings_ms", {})
    if stages:
        lines += ["", "Profiled stages", "────────────────────────────────────────────────────────────────────────────"]
        for name, values in sorted(stages.items(), key=lambda item: item[1]["median"], reverse=True):
            lines.append(
                f"{name:<28} p50 {values['median']:7.1f} ms · p95 {values['p95']:7.1f} ms · max {values['max']:7.1f} ms"
            )
    lines += [
        "",
        "Cases",
        "────────────────────────────────────────────────────────────────────────────",
    ]

    for case in report["cases"]:
        mark = "✓" if case["passed"] else "✗"
        state = case.get("ambiguity") or "-"
        top1_name = case.get("top1") or "<none>"
        lines.append(f"{mark} {case['case_id']:<27} top1={top1_name:<22} state={state:<14} {case['latency_ms']:7.1f} ms")
        if not case["passed"]:
            if case.get("error"):
                lines.append(f"    error: {case['error']}")
            for assertion in case["assertions"]:
                if assertion["passed"]:
                    continue
                lines.append(
                    f"    {assertion['name']}: expected {assertion['expected']!r}, got {assertion['actual']!r}"
                )
            if case.get("forensics"):
                lines.append("    forensic top candidates:")
                for index, item in enumerate(case["forensics"], 1):
                    adjustments = ",".join(item.get("semantic_adjustments", [])) or "-"
                    lines.append(
                        f"      {index}. {item.get('command') or '<none>'} · "
                        f"semantic={item.get('semantic_fit', 0.0):.4f} {item.get('semantic_tier') or '-'} · "
                        f"utility={item.get('final_utility', 0.0):.4f} · adjustments={adjustments}"
                    )
    return "\n".join(lines) + "\n"


def render_markdown(report: dict[str, Any]) -> str:
    summary = report["summary"]
    latency = summary["latency_ms"]
    lines = [
        "# Acclorite Benchmark Report",
        "",
        f"- Version: `{report.get('acclorite_version') or 'unknown'}`",
        f"- Corpus: `{report.get('corpus_name')}`",
        f"- Generated: `{report.get('generated_at')}`",
        f"- Cases: **{summary['cases_passed']}/{summary['cases_total']}** passed ({summary['case_pass_rate'] * 100:.1f}%)",
        f"- Assertions: **{summary['assertions_passed']}/{summary['assertions_total']}** passed ({summary['assertion_pass_rate'] * 100:.1f}%)",
        f"- Latency: p50 **{latency['median']:.1f} ms**, p95 **{latency['p95']:.1f} ms**, max **{latency['max']:.1f} ms**",
    ]
    profiled = summary.get("profiled_search_ms", {})
    if profiled.get("median", 0.0) > 0.0:
        lines.append(
            f"- Internal search: p50 **{profiled['median']:.1f} ms**, p95 **{profiled['p95']:.1f} ms**, max **{profiled['max']:.1f} ms**"
        )
    stages = summary.get("stage_timings_ms", {})
    if stages:
        lines += ["", "## Profiled Stages", "", "| Stage | Samples | p50 | p95 | Max |", "|---|---:|---:|---:|---:|"]
        for name, values in sorted(stages.items(), key=lambda item: item[1]["median"], reverse=True):
            lines.append(
                f"| `{name}` | {values['samples']} | {values['median']:.1f} ms | {values['p95']:.1f} ms | {values['max']:.1f} ms |"
            )
    lines += [
        "",
        "| Case | Query | Top 1 | Ambiguity | Confidence | Latency | Result |",
        "|---|---|---|---|---:|---:|---|",
    ]
    for case in report["cases"]:
        result = "PASS" if case["passed"] else "FAIL"
        query = case["query"].replace("|", "\\|")
        lines.append(
            f"| `{case['case_id']}` | {query} | `{case['top1'] or '<none>'}` | `{case['ambiguity'] or '-'}` | "
            f"{case['confidence']:.3f} | {case['latency_ms']:.1f} ms | **{result}** |"
        )

    failures = [case for case in report["cases"] if not case["passed"]]
    if failures:
        lines += ["", "## Failures", ""]
        for case in failures:
            lines.append(f"### `{case['case_id']}` — {case['query']}")
            if case.get("error"):
                lines.append(f"- Error: `{case['error']}`")
            for assertion in case["assertions"]:
                if not assertion["passed"]:
                    lines.append(
                        f"- `{assertion['name']}` expected `{assertion['expected']}`, got `{assertion['actual']}`"
                    )
            if case.get("forensics"):
                lines += [
                    "",
                    "#### Forensic top candidates",
                    "",
                    "| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |",
                    "|---:|---|---:|---|---:|---|---|",
                ]
                for index, item in enumerate(case["forensics"], 1):
                    adjustments = ", ".join(item.get("semantic_adjustments", [])) or "-"
                    source = str(item.get("source", "")).replace("|", "\\|")
                    lines.append(
                        f"| {index} | `{item.get('command') or '<none>'}` | "
                        f"{item.get('semantic_fit', 0.0):.4f} | `{item.get('semantic_tier') or '-'}` | "
                        f"{item.get('final_utility', 0.0):.4f} | `{source}` | `{adjustments}` |"
                    )
            lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def parse_args(argv: list[str]) -> argparse.Namespace:
    here = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description="Run the Acclorite deterministic benchmark corpus")
    parser.add_argument("--binary", type=Path, default=Path("./build/acclorite"), help="Acclorite executable")
    parser.add_argument("--corpus", type=Path, default=here / "corpus-v1.json", help="Benchmark corpus JSON")
    parser.add_argument("--json-out", type=Path, help="Write machine-readable benchmark report")
    parser.add_argument("--markdown-out", type=Path, help="Write Markdown benchmark report")
    parser.add_argument("--timeout", type=float, default=20.0, help="Per-query timeout in seconds")
    parser.add_argument("--no-warmup", action="store_true", help="Skip one discarded warm-up query")
    parser.add_argument("--no-profile", action="store_true", help="Do not request internal per-stage search timings")
    parser.add_argument("--strict", action="store_true", help="Exit non-zero if any benchmark case fails")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    binary = args.binary.resolve()
    corpus_path = args.corpus.resolve()

    if not binary.exists() or not os.access(binary, os.X_OK):
        print(f"error: benchmark binary is not executable: {binary}", file=sys.stderr)
        return 2

    try:
        corpus = json.loads(corpus_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"error: cannot read corpus {corpus_path}: {exc}", file=sys.stderr)
        return 2

    cases = corpus.get("cases", [])
    if corpus.get("schema_version") != 1 or not isinstance(cases, list):
        print("error: unsupported or malformed benchmark corpus", file=sys.stderr)
        return 2

    if not args.no_warmup:
        _run_json(binary, "search text", args.timeout, profile=not args.no_profile)

    results: list[CaseResult] = []
    for case in cases:
        if not isinstance(case, dict) or not isinstance(case.get("query"), str):
            continue
        return_code, latency_ms, payload, error = _run_json(binary, case["query"], args.timeout, profile=not args.no_profile)
        if error:
            results.append(CaseResult(
                case_id=str(case.get("id", "")),
                query=str(case.get("query", "")),
                tags=list(case.get("tags", [])),
                return_code=return_code,
                latency_ms=latency_ms,
                top1="",
                top3=[],
                ambiguity="",
                confidence=0.0,
                frame="",
                assertions=_error_assertions(case, error),
                forensics=None,
                error=error,
            ))
            continue
        evaluated = evaluate(case, payload, return_code, latency_ms)
        if not evaluated.passed:
            _, _, forensic_payload, forensic_error = _run_json(
                binary, case["query"], args.timeout, profile=False, explain_ranking=True
            )
            if not forensic_error:
                evaluated.forensics = _forensic_candidates(forensic_payload)
        results.append(evaluated)

    report = build_report(binary, corpus_path, corpus, results)
    sys.stdout.write(render_terminal(report))

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if args.markdown_out:
        args.markdown_out.parent.mkdir(parents=True, exist_ok=True)
        args.markdown_out.write_text(render_markdown(report), encoding="utf-8")

    if args.strict and report["summary"]["cases_passed"] != report["summary"]["cases_total"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
