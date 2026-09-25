#!/usr/bin/env python3
"""Staged-install regression test for Acclorite's public package payload."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    cmake = shutil.which("cmake")
    require(cmake is not None, "cmake must be available for install-layout test")

    with tempfile.TemporaryDirectory(prefix="acclorite-install-") as tmp:
        stage = Path(tmp) / "stage"
        env = os.environ.copy()
        env["DESTDIR"] = str(stage)
        subprocess.run(
            [cmake, "--install", str(build_dir), "--prefix", "/usr"],
            env=env,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        root = stage / "usr"
        binary = root / "bin" / "acclorite"
        expected = [
            binary,
            root / "share" / "acclorite" / "curated-guidance.tsv",
            root / "share" / "acclorite" / "zypper-readonly.conf",
            root / "share" / "man" / "man1" / "acclorite.1",
            root / "share" / "doc" / "acclorite" / "LICENSE",
            root / "share" / "doc" / "acclorite" / "README.md",
            root / "share" / "doc" / "acclorite" / "install.md",
            root / "share" / "doc" / "acclorite" / "machine-interface.md",
            root / "share" / "doc" / "acclorite" / "release.md",
        ]
        for path in expected:
            require(path.is_file(), f"staged install is missing {path.relative_to(stage)}")

        zypper_config = root / "share" / "acclorite" / "zypper-readonly.conf"
        require("runSearchPackages = never" in zypper_config.read_text(encoding="utf-8"),
                "installed Zypper policy must disable the network search-packages plugin")

        # Ensure the staged binary is self-contained rather than silently consulting the
        # original build tree. Hide both build-tree data artifacts, then select the openSUSE
        # backend with a fake Zypper executable. Capability detection does not execute it;
        # success therefore proves the installed binary found its staged read-only config.
        build_guidance = build_dir / "share" / "acclorite" / "curated-guidance.tsv"
        build_zypper_config = build_dir / "share" / "acclorite" / "zypper-readonly.conf"
        hidden_guidance = build_guidance.with_suffix(".tsv.install-test-hidden")
        hidden_zypper = build_zypper_config.with_suffix(".conf.install-test-hidden")
        moved_guidance = False
        moved_zypper = False
        if build_guidance.exists():
            build_guidance.rename(hidden_guidance)
            moved_guidance = True
        if build_zypper_config.exists():
            build_zypper_config.rename(hidden_zypper)
            moved_zypper = True

        fixture_bin = Path(tmp) / "fixture-bin"
        fixture_bin.mkdir()
        fake_zypper = fixture_bin / "zypper"
        fake_zypper.write_text("#!/bin/sh\nexit 99\n", encoding="utf-8")
        fake_zypper.chmod(0o755)
        os_release = Path(tmp) / "os-release"
        os_release.write_text('ID=opensuse-tumbleweed\nID_LIKE="suse opensuse"\n', encoding="utf-8")

        try:
            result = subprocess.run(
                [str(binary), "--capabilities"],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                env={
                    **os.environ,
                    "PATH": str(fixture_bin),
                    "ACCLORITE_CURATED_GUIDANCE": "",
                    "ACCLORITE_OS_RELEASE": str(os_release),
                },
            )
        finally:
            if moved_zypper:
                hidden_zypper.rename(build_zypper_config)
            if moved_guidance:
                hidden_guidance.rename(build_guidance)

        payload = json.loads(result.stdout)
        require(payload["acclorite_version"] == "0.3.3", "installed binary version mismatch")
        require(payload["integrations"]["curated_guidance"] is True,
                "staged binary must discover bundled curated guidance relative to itself")
        require(payload["integrations"]["zypper_packages"] is True,
                "staged binary must discover bundled Zypper policy relative to itself")

        uninstall_env = os.environ.copy()
        uninstall_env["DESTDIR"] = str(stage)
        subprocess.run(
            [cmake, "--build", str(build_dir), "--target", "uninstall"],
            env=uninstall_env,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        for path in expected:
            require(not path.exists(),
                    f"uninstall left staged file behind: {path.relative_to(stage)}")

    print("Acclorite install-layout tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
