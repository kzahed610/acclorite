#!/usr/bin/env python3
"""End-to-end checks for Acclorite's human-facing terminal UX."""

from __future__ import annotations

import argparse
import errno
import json
import os
from pathlib import Path
import pty
import stat
import subprocess
import tempfile


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run(binary: Path, *args: str, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(binary), *args],
        text=True,
        capture_output=True,
        env=env,
        check=False,
        timeout=20,
    )


def run_pty(binary: Path, *args: str, env: dict[str, str] | None = None) -> tuple[int, str]:
    master, slave = pty.openpty()
    proc = subprocess.Popen(
        [str(binary), *args],
        stdin=subprocess.DEVNULL,
        stdout=slave,
        stderr=slave,
        env=env,
        close_fds=True,
        text=False,
    )
    os.close(slave)
    chunks: list[bytes] = []
    try:
        while True:
            try:
                chunk = os.read(master, 4096)
            except OSError as error:
                if error.errno == errno.EIO:
                    break
                raise
            if not chunk:
                break
            chunks.append(chunk)
    finally:
        os.close(master)
    return proc.wait(timeout=20), b"".join(chunks).decode("utf-8", errors="replace")


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


def test_help_and_color_policy(binary: Path) -> None:
    captured = run(binary, "--help")
    require(captured.returncode == 0, "--help must succeed")
    require("Acclorite 0.3.3" in captured.stdout, "help must expose the current version")
    require("Deterministic · local-first · no network at runtime" in captured.stdout,
            "help must state the local/offline operating model")
    require("Try it" in captured.stdout and "Options" in captured.stdout,
            "help must expose a concise usage hierarchy and examples")
    require("\x1b[" not in captured.stdout,
            "redirected help output must never contain ANSI styling")

    tty_env = os.environ.copy()
    tty_env.pop("NO_COLOR", None)
    tty_env["TERM"] = "xterm-256color"
    code, tty_help = run_pty(binary, "--help", env=tty_env)
    require(code == 0, "TTY --help must succeed")
    require("\x1b[" in tty_help, "interactive TTY help should use restrained ANSI styling")

    no_color_env = tty_env.copy()
    no_color_env["NO_COLOR"] = "1"
    code, no_color_help = run_pty(binary, "--help", env=no_color_env)
    require(code == 0, "NO_COLOR TTY --help must succeed")
    require("\x1b[" not in no_color_help,
            "NO_COLOR must disable ANSI styling even when stdout is a TTY")

    code, tty_capabilities = run_pty(binary, "--capabilities", env=tty_env)
    require(code == 0, "TTY --capabilities must succeed")
    require("\x1b[" not in tty_capabilities,
            "machine-readable JSON must never gain ANSI styling even on a TTY")
    require(json.loads(tty_capabilities)["capabilities_schema_version"] == 1,
            "TTY capability output must remain valid JSON")


def test_human_search_and_doctor_hierarchy(binary: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="acclorite-terminal-ux-") as tmp:
        root = Path(tmp)
        fixture_bin = root / "bin"
        fixture_bin.mkdir(parents=True)
        fixture = fixture_bin / "uxfixturetool"
        fixture.write_text("fixture\n", encoding="utf-8")
        fixture.chmod(fixture.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        env = isolated_env(root, fixture_bin)

        result = run(binary, "what is uxfixturetool", env=env)
        require(result.returncode == 0, "human fixture search must succeed")
        require("Explain · uxfixturetool" in result.stdout,
                "human search should lead with query frame and target")
        require("✓ installed" in result.stdout,
                "human search should expose compact installed-state metadata")
        require("Package" not in result.stdout and "Matched" not in result.stdout and "Sources" not in result.stdout,
                "normal human search should hide diagnostic metadata")
        require("low-confidence" not in result.stdout and "competitive" not in result.stdout,
                "normal terminal UX should not expose internal ambiguity enum labels")
        require("\x1b[" not in result.stdout,
                "captured human search must remain ANSI-free")

        broken_env = isolated_env(root / "broken", root / "missing-path")
        doctor = run(binary, "doctor", env=broken_env)
        require(doctor.returncode == 4, "broken Doctor fixture must use diagnostic exit 4")
        require("Acclorite Doctor" in doctor.stdout and "✗ broken" in doctor.stdout,
                "Doctor should lead with an obvious overall health state")
        require("checks ·" in doctor.stdout and "Read-only diagnostic · no changes were made" in doctor.stdout,
                "Doctor should summarize checks and make read-only behavior obvious")
        require("Status  broken" in doctor.stdout,
                "Doctor should close with an explicit status line")

        miss = run(binary, "acclorite-terminal-ux-certainly-missing-91a", env=broken_env)
        require(miss.returncode == 1, "no-result human search must retain exit 1")
        require("No strong match" in miss.stdout and "Try" in miss.stdout,
                "no-result UX should explain the miss and give actionable next steps")
        require("acclorite doctor" in miss.stdout,
                "no-result UX should point users toward source diagnostics")


def test_reindex_human_recovery_message(binary: Path) -> None:
    capabilities = json.loads(run(binary, "--capabilities").stdout)
    if not capabilities["build"]["sqlite_fts"]:
        return

    with tempfile.TemporaryDirectory(prefix="acclorite-terminal-reindex-") as tmp:
        root = Path(tmp)
        fixture_bin = root / "bin"
        fixture_bin.mkdir(parents=True)
        fixture = fixture_bin / "uxreindexfixture"
        fixture.write_text("fixture\n", encoding="utf-8")
        fixture.chmod(fixture.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        env = isolated_env(root, fixture_bin)

        rebuilt = run(binary, "--reindex", env=env)
        require(rebuilt.returncode == 0, "human reindex fixture must succeed")
        require(rebuilt.stdout.startswith("✓ Index rebuilt\n  "),
                "successful reindex should be compact and visibly successful")

        fixture.unlink()
        failed = run(binary, "--reindex", env=env)
        require(failed.returncode == 3, "failed human reindex must retain operational exit 3")
        require("previous usable index was preserved" in failed.stderr,
                "failed reindex must tell the human that recovery data was preserved")
        require("acclorite doctor" in failed.stderr,
                "failed reindex must direct the human to diagnostics")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()

    test_help_and_color_policy(binary)
    test_human_search_and_doctor_hierarchy(binary)
    test_reindex_human_recovery_message(binary)
    print("Acclorite terminal UX tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
