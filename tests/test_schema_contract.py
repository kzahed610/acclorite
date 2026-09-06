#!/usr/bin/env python3
"""Structural contract tests for Acclorite's public JSON schemas.

These tests deliberately validate required fields, JSON types, enum domains,
nullability, and schema/exit-code pairing without snapshotting unstable values
such as scores, timings, temporary paths, or ranking weights.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile
from typing import Any, Iterable

SUCCESS = 0
NO_RESULT = 1
DIAGNOSTIC_ERROR = 4

SEARCH_SCHEMA = 15
DOCTOR_SCHEMA = 1
CAPABILITIES_SCHEMA = 1

QUERY_FRAMES = {
    "Discover",
    "Explain",
    "Locate",
    "Inspect",
    "Modify",
    "Compare",
    "Diagnose",
    "Unknown",
}
CONFIDENCE_LEVELS = {"low", "medium", "high"}
AMBIGUITY_STATES = {"clear", "competitive", "ambiguous", "low-confidence", "no-result"}
INTERFACE_KINDS = {"Unknown", "CLI", "GUI", "Hybrid"}
GUIDANCE_SOURCE_TYPES = {"man", "info", "tldr", "curated", "package"}
DOCTOR_STATUSES = {"healthy", "degraded", "broken"}
DOCTOR_STATES = {"ready", "warning", "optional", "error"}
ADJUSTMENT_KINDS = {"add", "multiply", "floor", "clamp"}
CONCEPT_ROLES = {"action", "subject", "context"}
MATCH_KINDS = {"canonical", "alternative", "none"}
MATCH_LOCATIONS = {"name", "description", "none"}


def run(binary: Path, *args: str, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(binary), *args],
        text=True,
        capture_output=True,
        env=env,
        check=False,
        timeout=20,
    )


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load_json(proc: subprocess.CompletedProcess[str], context: str) -> dict[str, Any]:
    try:
        payload = json.loads(proc.stdout)
    except json.JSONDecodeError as error:
        raise AssertionError(f"{context}: stdout is not valid JSON: {error}\n{proc.stdout}") from error
    require(isinstance(payload, dict), f"{context}: root must be an object")
    return payload


def require_keys(value: dict[str, Any], keys: Iterable[str], context: str) -> None:
    missing = [key for key in keys if key not in value]
    require(not missing, f"{context}: missing required field(s): {', '.join(missing)}")


def require_bool(value: Any, context: str) -> None:
    require(type(value) is bool, f"{context}: expected boolean, got {type(value).__name__}")


def require_int(value: Any, context: str) -> None:
    require(type(value) is int, f"{context}: expected integer, got {type(value).__name__}")


def require_number(value: Any, context: str) -> None:
    require(type(value) in (int, float), f"{context}: expected number, got {type(value).__name__}")


def require_string(value: Any, context: str) -> None:
    require(type(value) is str, f"{context}: expected string, got {type(value).__name__}")


def require_string_list(value: Any, context: str) -> None:
    require(isinstance(value, list), f"{context}: expected array")
    for index, item in enumerate(value):
        require_string(item, f"{context}[{index}]")


def isolated_env(root: Path, path: Path) -> dict[str, str]:
    env = os.environ.copy()
    env.update(
        {
            "HOME": str(root / "home"),
            "PATH": str(path),
            "XDG_CACHE_HOME": str(root / "cache"),
            "XDG_CONFIG_HOME": str(root / "config"),
            "XDG_DATA_HOME": str(root / "data"),
            "XDG_DATA_DIRS": str(root / "system-data"),
            "MANPATH": str(root / "man"),
            "ACCLORITE_INDEX_PATH": str(root / "index.db"),
            "ACCLORITE_DISABLE_ARCH_CACHE": "1",
        }
    )
    return env


def make_executable(path: Path, body: str = "#!/bin/sh\nexit 0\n") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(body, encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def validate_query_frame(value: Any, context: str) -> None:
    require(isinstance(value, dict), f"{context}: expected object")
    require_keys(value, ("type", "confidence", "explicit", "signals"), context)
    require_string(value["type"], f"{context}.type")
    require(value["type"] in QUERY_FRAMES, f"{context}.type: undocumented enum {value['type']!r}")
    require_number(value["confidence"], f"{context}.confidence")
    require_bool(value["explicit"], f"{context}.explicit")
    require_string_list(value["signals"], f"{context}.signals")


def validate_confidence(value: Any, context: str) -> None:
    require(isinstance(value, dict), f"{context}: expected object")
    require_keys(
        value,
        ("top_candidate", "level", "interpretation", "separation", "semantic_gap", "utility_gap", "ambiguity", "signals"),
        context,
    )
    for key in ("top_candidate", "interpretation", "separation", "semantic_gap", "utility_gap"):
        require_number(value[key], f"{context}.{key}")
    require_string(value["level"], f"{context}.level")
    require(value["level"] in CONFIDENCE_LEVELS, f"{context}.level: undocumented enum {value['level']!r}")
    require_string(value["ambiguity"], f"{context}.ambiguity")
    require(value["ambiguity"] in AMBIGUITY_STATES,
            f"{context}.ambiguity: undocumented enum {value['ambiguity']!r}")
    require_string_list(value["signals"], f"{context}.signals")


def validate_adjustment(value: Any, context: str) -> None:
    require(isinstance(value, dict), f"{context}: expected object")
    require_keys(value, ("id", "label", "kind", "value", "before", "after"), context)
    require_string(value["id"], f"{context}.id")
    require_string(value["label"], f"{context}.label")
    require_string(value["kind"], f"{context}.kind")
    require(value["kind"] in ADJUSTMENT_KINDS, f"{context}.kind: undocumented enum {value['kind']!r}")
    for key in ("value", "before", "after"):
        require_number(value[key], f"{context}.{key}")


def validate_semantic_breakdown(value: Any, context: str) -> None:
    require(isinstance(value, dict), f"{context}: expected object")
    require_keys(
        value,
        (
            "exact_name_match", "coverage", "average_quality", "formula_score",
            "full_match_ceiling", "after_ceiling", "frame_before", "frame_after",
            "frame_effect", "concepts",
        ),
        context,
    )
    require_bool(value["exact_name_match"], f"{context}.exact_name_match")
    for key in ("coverage", "average_quality", "formula_score", "full_match_ceiling", "after_ceiling", "frame_before", "frame_after"):
        require_number(value[key], f"{context}.{key}")
    require_string(value["frame_effect"], f"{context}.frame_effect")
    require(isinstance(value["concepts"], list), f"{context}.concepts: expected array")
    for index, concept in enumerate(value["concepts"]):
        c = f"{context}.concepts[{index}]"
        require(isinstance(concept, dict), f"{c}: expected object")
        require_keys(concept, ("term", "role", "weight", "implicit", "quality", "match_kind", "location", "matched_via", "matched_token"), c)
        for key in ("term", "role", "match_kind", "location", "matched_via", "matched_token"):
            require_string(concept[key], f"{c}.{key}")
        require(concept["role"] in CONCEPT_ROLES, f"{c}.role: undocumented enum {concept['role']!r}")
        require(concept["match_kind"] in MATCH_KINDS, f"{c}.match_kind: undocumented enum {concept['match_kind']!r}")
        require(concept["location"] in MATCH_LOCATIONS, f"{c}.location: undocumented enum {concept['location']!r}")
        require_number(concept["weight"], f"{c}.weight")
        require_bool(concept["implicit"], f"{c}.implicit")
        require_number(concept["quality"], f"{c}.quality")


def validate_ranking(value: Any, context: str) -> None:
    require(isinstance(value, dict), f"{context}: expected object")
    require_keys(
        value,
        (
            "raw_semantic_fit", "semantic_fit", "semantic_adjustments", "semantic_tier",
            "base_score", "base_utility", "base_evidence", "base_merges", "adjustments",
            "final_utility", "final_score",
        ),
        context,
    )
    for key in ("raw_semantic_fit", "semantic_fit", "base_score", "base_utility", "final_utility", "final_score"):
        require_number(value[key], f"{context}.{key}")
    require_string(value["semantic_tier"], f"{context}.semantic_tier")

    for key in ("semantic_adjustments", "adjustments"):
        require(isinstance(value[key], list), f"{context}.{key}: expected array")
        for index, adjustment in enumerate(value[key]):
            validate_adjustment(adjustment, f"{context}.{key}[{index}]")

    require(isinstance(value["base_evidence"], list), f"{context}.base_evidence: expected array")
    for index, evidence in enumerate(value["base_evidence"]):
        e = f"{context}.base_evidence[{index}]"
        require(isinstance(evidence, dict), f"{e}: expected object")
        require_keys(evidence, ("source", "method", "semantic", "adjustments", "final_score"), e)
        require_string(evidence["source"], f"{e}.source")
        require_string(evidence["method"], f"{e}.method")
        if evidence["semantic"] is not None:
            validate_semantic_breakdown(evidence["semantic"], f"{e}.semantic")
        require(isinstance(evidence["adjustments"], list), f"{e}.adjustments: expected array")
        for adjustment_index, adjustment in enumerate(evidence["adjustments"]):
            validate_adjustment(adjustment, f"{e}.adjustments[{adjustment_index}]")
        require_number(evidence["final_score"], f"{e}.final_score")

    require(isinstance(value["base_merges"], list), f"{context}.base_merges: expected array")
    for index, merge in enumerate(value["base_merges"]):
        m = f"{context}.base_merges[{index}]"
        require(isinstance(merge, dict), f"{m}: expected object")
        require_keys(merge, ("incoming_source", "before", "incoming_score", "strongest", "supporting", "raw_after", "after"), m)
        require_string(merge["incoming_source"], f"{m}.incoming_source")
        for key in ("before", "incoming_score", "strongest", "supporting", "raw_after", "after"):
            require_number(merge[key], f"{m}.{key}")


def validate_guidance_record(value: Any, context: str, resource: bool) -> None:
    require(isinstance(value, dict), f"{context}: expected object")
    if resource:
        require_keys(value, ("label", "target", "source_type", "source_reference", "verified", "verified_by", "verified_on"), context)
        require_string(value["label"], f"{context}.label")
        require_string(value["target"], f"{context}.target")
    else:
        require_keys(value, ("text", "source_type", "source_reference", "verified", "verified_by", "verified_on"), context)
        require_string(value["text"], f"{context}.text")
    require_string(value["source_type"], f"{context}.source_type")
    require(value["source_type"] in GUIDANCE_SOURCE_TYPES,
            f"{context}.source_type: undocumented enum {value['source_type']!r}")
    require_string(value["source_reference"], f"{context}.source_reference")
    require_bool(value["verified"], f"{context}.verified")
    require_string(value["verified_by"], f"{context}.verified_by")
    require_string(value["verified_on"], f"{context}.verified_on")


def validate_candidate(value: Any, context: str) -> None:
    require(isinstance(value, dict), f"{context}: expected object")
    require_keys(
        value,
        (
            "command", "path", "summary", "source", "package", "repository", "package_version",
            "installed", "repository_available", "interface", "cli_capable", "gui_capable",
            "matched_terms", "provided_commands", "examples", "learning_resources", "score", "ranking",
        ),
        context,
    )
    for key in ("command", "path", "summary", "source", "package", "repository", "package_version", "interface"):
        require_string(value[key], f"{context}.{key}")
    require(value["interface"] in INTERFACE_KINDS,
            f"{context}.interface: undocumented enum {value['interface']!r}")
    for key in ("installed", "repository_available", "cli_capable", "gui_capable"):
        require_bool(value[key], f"{context}.{key}")
    require_string_list(value["matched_terms"], f"{context}.matched_terms")
    require_string_list(value["provided_commands"], f"{context}.provided_commands")
    require(isinstance(value["examples"], list), f"{context}.examples: expected array")
    for index, example in enumerate(value["examples"]):
        validate_guidance_record(example, f"{context}.examples[{index}]", resource=False)
    require(isinstance(value["learning_resources"], list), f"{context}.learning_resources: expected array")
    for index, resource in enumerate(value["learning_resources"]):
        validate_guidance_record(resource, f"{context}.learning_resources[{index}]", resource=True)
    require_number(value["score"], f"{context}.score")
    if value["ranking"] is not None:
        validate_ranking(value["ranking"], f"{context}.ranking")


def validate_timing(value: Any, context: str) -> None:
    require(isinstance(value, dict), f"{context}: expected object")
    require_keys(value, ("total_ms", "stages"), context)
    require_number(value["total_ms"], f"{context}.total_ms")
    require(isinstance(value["stages"], list), f"{context}.stages: expected array")
    for index, stage in enumerate(value["stages"]):
        s = f"{context}.stages[{index}]"
        require(isinstance(stage, dict), f"{s}: expected object")
        require_keys(stage, ("name", "milliseconds", "items"), s)
        require_string(stage["name"], f"{s}.name")
        require_number(stage["milliseconds"], f"{s}.milliseconds")
        require_int(stage["items"], f"{s}.items")


def validate_search(payload: dict[str, Any], context: str) -> None:
    require_keys(
        payload,
        ("schema_version", "query", "normalized_query", "timing", "query_frame", "confidence", "clarifications", "targets", "locations", "results"),
        context,
    )
    require_int(payload["schema_version"], f"{context}.schema_version")
    require(payload["schema_version"] == SEARCH_SCHEMA, f"{context}: expected search schema {SEARCH_SCHEMA}")
    require_string(payload["query"], f"{context}.query")
    require_string(payload["normalized_query"], f"{context}.normalized_query")
    if payload["timing"] is not None:
        validate_timing(payload["timing"], f"{context}.timing")
    validate_query_frame(payload["query_frame"], f"{context}.query_frame")
    validate_confidence(payload["confidence"], f"{context}.confidence")

    require(isinstance(payload["clarifications"], list), f"{context}.clarifications: expected array")
    for index, clarification in enumerate(payload["clarifications"]):
        c = f"{context}.clarifications[{index}]"
        require(isinstance(clarification, dict), f"{c}: expected object")
        require_keys(clarification, ("id", "label"), c)
        require_string(clarification["id"], f"{c}.id")
        require_string(clarification["label"], f"{c}.label")

    require_string_list(payload["targets"], f"{context}.targets")
    require(isinstance(payload["locations"], list), f"{context}.locations: expected array")
    for index, location in enumerate(payload["locations"]):
        loc = f"{context}.locations[{index}]"
        require(isinstance(location, dict), f"{loc}: expected object")
        require_keys(location, ("path", "kind", "source", "score"), loc)
        for key in ("path", "kind", "source"):
            require_string(location[key], f"{loc}.{key}")
        require_number(location["score"], f"{loc}.score")

    require(isinstance(payload["results"], list), f"{context}.results: expected array")
    for index, candidate in enumerate(payload["results"]):
        validate_candidate(candidate, f"{context}.results[{index}]")


def validate_doctor(payload: dict[str, Any], context: str) -> None:
    require_keys(payload, ("doctor_schema_version", "status", "mutated_system", "checks"), context)
    require_int(payload["doctor_schema_version"], f"{context}.doctor_schema_version")
    require(payload["doctor_schema_version"] == DOCTOR_SCHEMA, f"{context}: expected doctor schema {DOCTOR_SCHEMA}")
    require_string(payload["status"], f"{context}.status")
    require(payload["status"] in DOCTOR_STATUSES, f"{context}.status: undocumented enum {payload['status']!r}")
    require_bool(payload["mutated_system"], f"{context}.mutated_system")
    require(payload["mutated_system"] is False, f"{context}.mutated_system must remain false for read-only doctor")
    require(isinstance(payload["checks"], list), f"{context}.checks: expected array")
    for index, check in enumerate(payload["checks"]):
        c = f"{context}.checks[{index}]"
        require(isinstance(check, dict), f"{c}: expected object")
        require_keys(check, ("id", "section", "label", "state", "detail", "hint"), c)
        for key in ("id", "section", "label", "state", "detail", "hint"):
            require_string(check[key], f"{c}.{key}")
        require(check["state"] in DOCTOR_STATES, f"{c}.state: undocumented enum {check['state']!r}")


def validate_capabilities(payload: dict[str, Any], context: str) -> None:
    require_keys(
        payload,
        (
            "capabilities_schema_version", "acclorite_version", "search_schema_version",
            "doctor_schema_version", "offline_runtime", "non_interactive", "commands",
            "output", "build", "integrations", "exit_codes",
        ),
        context,
    )
    for key in ("capabilities_schema_version", "search_schema_version", "doctor_schema_version"):
        require_int(payload[key], f"{context}.{key}")
    require(payload["capabilities_schema_version"] == CAPABILITIES_SCHEMA,
            f"{context}: expected capabilities schema {CAPABILITIES_SCHEMA}")
    require(payload["search_schema_version"] == SEARCH_SCHEMA,
            f"{context}: published search schema mismatch")
    require(payload["doctor_schema_version"] == DOCTOR_SCHEMA,
            f"{context}: published doctor schema mismatch")
    require_string(payload["acclorite_version"], f"{context}.acclorite_version")
    require_bool(payload["offline_runtime"], f"{context}.offline_runtime")
    require_bool(payload["non_interactive"], f"{context}.non_interactive")
    for object_key in ("commands", "output", "build", "integrations", "exit_codes"):
        require(isinstance(payload[object_key], dict), f"{context}.{object_key}: expected object")

    require_keys(payload["commands"], ("search", "doctor", "reindex"), f"{context}.commands")
    require_keys(payload["output"], ("json", "ranking_explanations", "profiling"), f"{context}.output")
    require_keys(payload["build"], ("sqlite_fts",), f"{context}.build")
    require_keys(payload["integrations"], ("manual_guidance", "info_guidance", "tldr_cache", "curated_guidance", "arch_packages", "pkgfile"), f"{context}.integrations")
    require_keys(payload["exit_codes"], ("success", "no_result", "usage_error", "operational_error", "diagnostic_error"), f"{context}.exit_codes")
    for group in (payload["commands"], payload["output"], payload["build"], payload["integrations"]):
        for key, value in group.items():
            require_bool(value, f"{context}.{key}")
    for key, value in payload["exit_codes"].items():
        require_int(value, f"{context}.exit_codes.{key}")


def test_capability_schema(binary: Path) -> dict[str, Any]:
    proc = run(binary, "--capabilities")
    require(proc.returncode == SUCCESS, "capabilities contract must exit 0")
    payload = load_json(proc, "capabilities")
    validate_capabilities(payload, "capabilities")
    require(payload["acclorite_version"] == "0.2.6", "capabilities must expose v0.2.6")
    return payload


def test_search_schema(binary: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="acclorite-schema-search-") as tmp:
        root = Path(tmp)
        fixture_bin = root / "bin"
        make_executable(fixture_bin / "m7fixturetool")
        make_executable(fixture_bin / "m7fixturetool2")
        for archive_tool in ("archive", "archive-create", "archive-extract", "archive-manager"):
            make_executable(fixture_bin / archive_tool)

        curated = root / "curated.tsv"
        curated.write_text(
            "# schema: 1\n"
            "# command\\tkind\\tid\\tlabel\\tvalue\\tsource_reference\\tverified_on\n".replace("\\t", "\t")
            + "m7fixturetool\texample\tfixture-example\t\tm7fixturetool --stable\thttps://example.invalid/upstream\t2026-09-06\n"
            + "m7fixturetool\tresource\tfixture-resource\tFixture upstream docs\thttps://example.invalid/upstream\thttps://example.invalid/upstream\t2026-09-06\n",
            encoding="utf-8",
        )

        env = isolated_env(root, fixture_bin)
        env["ACCLORITE_CURATED_GUIDANCE"] = str(curated)

        success = run(binary, "--json", "what is m7fixturetool", env=env)
        require(success.returncode == SUCCESS, "schema success fixture must exit 0")
        success_json = load_json(success, "search.success")
        validate_search(success_json, "search.success")
        require(success_json["timing"] is None, "timing must be null without --profile")
        require(success_json["query_frame"]["type"] == "Explain", "Explain frame is part of schema-15 contract")
        require(success_json["results"], "success fixture must emit a candidate")
        require(success_json["results"][0]["ranking"] is None,
                "results[].ranking must be null without --explain-ranking")
        require(success_json["results"][0]["examples"], "curated fixture should exercise example record shape")
        require(success_json["results"][0]["learning_resources"],
                "curated fixture should exercise learning-resource shape")

        profiled = run(binary, "--json", "--profile", "--explain-ranking", "what is m7fixturetool", env=env)
        require(profiled.returncode == SUCCESS, "profiled ranking fixture must exit 0")
        profiled_json = load_json(profiled, "search.profiled")
        validate_search(profiled_json, "search.profiled")
        require(profiled_json["timing"] is not None, "--profile must make timing an object")
        require(profiled_json["results"][0]["ranking"] is not None,
                "--explain-ranking must make ranking an object")

        miss_env = isolated_env(root / "miss", root / "missing-path")
        miss_env["ACCLORITE_CURATED_GUIDANCE"] = str(root / "missing-curated.tsv")
        miss = run(binary, "--json", "what is acclorite-m7-certainly-no-result-9f812e", env=miss_env)
        require(miss.returncode == NO_RESULT, "schema no-result fixture must exit 1")
        miss_json = load_json(miss, "search.no_result")
        validate_search(miss_json, "search.no_result")
        require(miss_json["results"] == [], "no-result contract requires empty results")
        require(miss_json["locations"] == [], "no-result contract requires empty locations")
        require(miss_json["confidence"]["ambiguity"] == "no-result",
                "no-result search must publish no-result ambiguity")
        require(miss_json["timing"] is None, "non-profiled no-result timing must remain null")

        frame_queries = {
            "Discover": "find duplicate files",
            "Explain": "what is acclorite-m7-certainly-no-result-9f812e",
            "Locate": "where is acclorite-m7-certainly-no-result-9f812e config",
            "Inspect": "where do i see acclorite-m7-certainly-no-result-9f812e status",
            "Modify": "where do i edit acclorite-m7-certainly-no-result-9f812e config",
            "Compare": "difference between acclorite-m7-left-9f812e and acclorite-m7-right-9f812e",
            "Diagnose": "why is acclorite-m7-certainly-no-result-9f812e broken",
            "Unknown": "!!!",
        }
        for expected_frame, query in frame_queries.items():
            proc = run(binary, "--json", query, env=miss_env)
            require(proc.returncode in (SUCCESS, NO_RESULT), f"{expected_frame} frame query must be a valid search")
            payload = load_json(proc, f"search.frame.{expected_frame}")
            validate_search(payload, f"search.frame.{expected_frame}")
            require(payload["query_frame"]["type"] == expected_frame,
                    f"query frame contract changed for {expected_frame}: {payload['query_frame']['type']!r}")

        ambiguous = run(binary, "--json", "archive files", env=env)
        require(ambiguous.returncode == SUCCESS, "ambiguous fixture must still be a successful search")
        ambiguous_json = load_json(ambiguous, "search.ambiguous")
        validate_search(ambiguous_json, "search.ambiguous")
        require(ambiguous_json["confidence"]["ambiguity"] == "ambiguous",
                "underspecified archive query must preserve ambiguous state")
        require(ambiguous_json["clarifications"],
                "ambiguous search must exercise clarification record shape")

        # Exercise location-object shape with a deterministic bounded XDG config hit.
        config_path = Path(env["XDG_CONFIG_HOME"]) / "m7fixturetool" / "config"
        config_path.parent.mkdir(parents=True, exist_ok=True)
        config_path.write_text("fixture=true\n", encoding="utf-8")
        located = run(binary, "--json", "where is m7fixturetool config", env=env)
        require(located.returncode == SUCCESS, "location fixture must exit 0")
        located_json = load_json(located, "search.location")
        validate_search(located_json, "search.location")
        require(located_json["query_frame"]["type"] == "Locate", "location fixture must remain Locate")
        require(located_json["locations"], "location fixture must emit locations[]")


def test_doctor_schema(binary: Path, capabilities: dict[str, Any]) -> None:
    with tempfile.TemporaryDirectory(prefix="acclorite-schema-doctor-") as tmp:
        root = Path(tmp)

        broken_env = isolated_env(root / "broken", root / "missing-path")
        broken = run(binary, "--json", "doctor", env=broken_env)
        require(broken.returncode == DIAGNOSTIC_ERROR, "broken Doctor fixture must exit 4")
        broken_json = load_json(broken, "doctor.broken")
        validate_doctor(broken_json, "doctor.broken")
        require(broken_json["status"] == "broken", "broken Doctor fixture must publish status=broken")
        require(any(check["state"] == "error" for check in broken_json["checks"]),
                "broken Doctor fixture must publish at least one error check")

        healthy_root = root / "healthy"
        fixture_bin = healthy_root / "bin"
        make_executable(fixture_bin / "m7fixturetool")
        make_executable(fixture_bin / "man")
        healthy_env = isolated_env(healthy_root, fixture_bin)

        if capabilities["build"]["sqlite_fts"]:
            rebuilt = run(binary, "--reindex", env=healthy_env)
            require(rebuilt.returncode == SUCCESS,
                    f"healthy Doctor fixture requires a fresh index; reindex failed: {rebuilt.stderr}")

        healthy = run(binary, "--json", "doctor", env=healthy_env)
        require(healthy.returncode == SUCCESS, "healthy Doctor fixture must exit 0")
        healthy_json = load_json(healthy, "doctor.healthy")
        validate_doctor(healthy_json, "doctor.healthy")
        require(healthy_json["status"] == "healthy",
                f"hermetic ready/optional Doctor fixture should be healthy, got {healthy_json['status']!r}")
        require(not any(check["state"] in {"warning", "error"} for check in healthy_json["checks"]),
                "healthy Doctor fixture must contain only ready/optional checks")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()

    capabilities = test_capability_schema(binary)
    test_search_schema(binary)
    test_doctor_schema(binary, capabilities)
    print("Acclorite schema contract tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
