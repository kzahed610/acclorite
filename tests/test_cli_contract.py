#!/usr/bin/env python3
"""End-to-end tests for Acclorite's Milestone-7 machine interface contract."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile

SUCCESS = 0
NO_RESULT = 1
USAGE_ERROR = 2
OPERATIONAL_ERROR = 3
DIAGNOSTIC_ERROR = 4


def run(binary: Path, *args: str, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(binary), *args],
        text=True,
        capture_output=True,
        env=env,
        check=False,
        timeout=20,
    )


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


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def test_capabilities(binary: Path) -> None:
    proc = run(binary, "--capabilities")
    require(proc.returncode == SUCCESS, "--capabilities must exit 0")
    require(proc.stderr == "", "--capabilities must not write diagnostics on success")
    payload = json.loads(proc.stdout)

    require(payload["capabilities_schema_version"] == 1, "capabilities schema must start at 1")
    require(payload["acclorite_version"] == "0.2.6", "capabilities must expose the binary version")
    require(payload["search_schema_version"] == 15, "capabilities must expose search schema 15")
    require(payload["doctor_schema_version"] == 1, "capabilities must expose doctor schema 1")
    require(payload["offline_runtime"] is True, "normal runtime must declare offline operation")
    require(payload["non_interactive"] is True, "machine interface must be non-interactive")
    require(payload["commands"] == {"search": True, "doctor": True, "reindex": True},
            "capabilities command surface changed unexpectedly")
    require(payload["output"] == {"json": True, "ranking_explanations": True, "profiling": True},
            "capabilities output surface changed unexpectedly")
    require(payload["exit_codes"] == {
        "success": SUCCESS,
        "no_result": NO_RESULT,
        "usage_error": USAGE_ERROR,
        "operational_error": OPERATIONAL_ERROR,
        "diagnostic_error": DIAGNOSTIC_ERROR,
    }, "published exit-code contract changed unexpectedly")

    for key in (
        "sqlite_fts",
    ):
        require(isinstance(payload["build"][key], bool), f"build.{key} must be boolean")
    for key in (
        "manual_guidance", "info_guidance", "tldr_cache", "curated_guidance",
        "arch_packages", "pkgfile",
    ):
        require(isinstance(payload["integrations"][key], bool), f"integrations.{key} must be boolean")

    redundant_json = run(binary, "--json", "--capabilities")
    require(redundant_json.returncode == SUCCESS, "--json --capabilities must remain accepted")
    require(json.loads(redundant_json.stdout)["capabilities_schema_version"] == 1,
            "--json --capabilities must emit the same JSON contract")


def test_usage_errors(binary: Path) -> None:
    no_args = run(binary)
    require(no_args.returncode == USAGE_ERROR, "missing invocation must exit 2")
    require(no_args.stdout == "", "usage errors must not pollute stdout")

    unknown = run(binary, "--definitely-not-an-acclorite-option")
    require(unknown.returncode == USAGE_ERROR, "unknown options must exit 2")
    require("unknown option" in unknown.stderr, "unknown-option error should be explicit")

    conflict = run(binary, "--capabilities", "query")
    require(conflict.returncode == USAGE_ERROR, "capabilities + query must exit 2")

    mixed_reindex = run(binary, "--reindex", "rg")
    require(mixed_reindex.returncode == USAGE_ERROR, "--reindex must be standalone")


def test_search_exit_codes(binary: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="acclorite-cli-contract-") as tmp:
        root = Path(tmp)
        fixture_bin = root / "bin"
        fixture_bin.mkdir(parents=True)
        fixture = fixture_bin / "m7fixturetool"
        fixture.write_text("fixture\n", encoding="utf-8")
        fixture.chmod(fixture.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

        success_env = isolated_env(root / "success", fixture_bin)
        success = run(binary, "--json", "what is m7fixturetool", env=success_env)
        require(success.returncode == SUCCESS, "a successful search must exit 0")
        success_json = json.loads(success.stdout)
        require(success_json["schema_version"] == 15, "successful search must emit schema 15")
        require(success_json["results"], "successful search must contain at least one result")
        require(success_json["results"][0]["command"] == "m7fixturetool",
                "hermetic exact-command search must resolve fixture tool")

        missing_path = root / "does-not-exist"
        miss_env = isolated_env(root / "miss", missing_path)
        miss = run(binary, "--json", "acclorite-m7-certainly-no-result-9f812e", env=miss_env)
        require(miss.returncode == NO_RESULT, "a valid search with no result must exit 1")
        miss_json = json.loads(miss.stdout)
        require(miss_json["schema_version"] == 15, "no-result search must still emit valid search JSON")
        require(miss_json["results"] == [], "no-result search must have an empty results array")


def test_doctor_and_operational_exit_codes(binary: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="acclorite-cli-doctor-") as tmp:
        root = Path(tmp)
        broken_env = isolated_env(root, root / "missing-path")
        doctor = run(binary, "--json", "doctor", env=broken_env)
        require(doctor.returncode == DIAGNOSTIC_ERROR,
                "doctor finding a fundamental environment error must exit 4")
        payload = json.loads(doctor.stdout)
        require(payload["doctor_schema_version"] == 1, "doctor must retain schema 1")
        require(payload["status"] == "broken", "hermetic missing PATH should be diagnosed as broken")

    operational_env = os.environ.copy()
    operational_env["ACCLORITE_INDEX_PATH"] = "/proc/acclorite-machine-interface/index.db"
    failed_reindex = run(binary, "--reindex", env=operational_env)
    require(failed_reindex.returncode == OPERATIONAL_ERROR,
            "reindex failure must use the operational-error exit code 3")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()

    test_capabilities(binary)
    test_usage_errors(binary)
    test_search_exit_codes(binary)
    test_doctor_and_operational_exit_codes(binary)
    print("Acclorite CLI contract tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
