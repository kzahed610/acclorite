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
    require(payload["acclorite_version"] == "0.3.3", "capabilities must expose the binary version")
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
        "arch_packages", "apt_packages", "dnf_packages", "zypper_packages", "xbps_packages", "pkgfile",
    ):
        require(isinstance(payload["integrations"][key], bool), f"integrations.{key} must be boolean")

    redundant_json = run(binary, "--json", "--capabilities")
    require(redundant_json.returncode == SUCCESS, "--json --capabilities must remain accepted")
    require(json.loads(redundant_json.stdout)["capabilities_schema_version"] == 1,
            "--json --capabilities must emit the same JSON contract")


def test_debian_backend_autoselection(binary: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="acclorite-cli-apt-backend-") as tmp:
        root = Path(tmp)
        fixture_bin = root / "bin"
        fixture_bin.mkdir(parents=True)

        apt_cache = fixture_bin / "apt-cache"
        apt_cache.write_text(
            "#!/bin/sh\n"
            "if [ \"$1\" = \"stats\" ]; then\n"
            "  printf '%s\\n' 'Total package names: 123'\n"
            "  exit 0\n"
            "fi\n"
            "if [ \"$1\" = \"search\" ]; then\n"
            "  printf '%s\\n' 'fdupes - identify duplicate files in directories'\n"
            "  exit 0\n"
            "fi\n"
            "exit 2\n",
            encoding="utf-8",
        )
        apt_cache.chmod(apt_cache.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

        os_release = root / "os-release"
        os_release.write_text('ID=ubuntu\nID_LIKE="debian"\n', encoding="utf-8")
        env = isolated_env(root, fixture_bin)
        env["ACCLORITE_OS_RELEASE"] = str(os_release)

        capabilities = run(binary, "--capabilities", env=env)
        require(capabilities.returncode == SUCCESS, "Debian capability probe must exit 0")
        payload = json.loads(capabilities.stdout)
        require(payload["integrations"]["apt_packages"] is True,
                "Ubuntu fixture must activate the APT package backend")
        require(payload["integrations"]["arch_packages"] is False,
                "Ubuntu fixture must not activate the Arch package backend")
        require(payload["integrations"]["dnf_packages"] is False,
                "Ubuntu fixture must not activate the DNF package backend")
        require(payload["integrations"]["zypper_packages"] is False,
                "Ubuntu fixture must not activate the Zypper package backend")
        require(payload["integrations"]["xbps_packages"] is False,
                "Ubuntu fixture must not activate the XBPS package backend")

        search = run(binary, "--json", "duplicate files", env=env)
        require(search.returncode == SUCCESS, "Debian package discovery query must succeed")
        search_json = json.loads(search.stdout)
        require(search_json["results"] and search_json["results"][0]["command"] == "fdupes",
                "APT backend should discover an uninstalled repository tool")
        require("apt" in search_json["results"][0]["source"].split("+"),
                "APT discovery must retain package provenance")



def test_fedora_backend_autoselection(binary: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="acclorite-cli-dnf-backend-") as tmp:
        root = Path(tmp)
        fixture_bin = root / "bin"
        fixture_bin.mkdir(parents=True)
        marker = root / "forbidden-dnf-call"

        dnf5 = fixture_bin / "dnf5"
        dnf5.write_text(
            "#!/bin/sh\n"
            "if [ \"$1\" != \"--cacheonly\" ]; then\n"
            f"  printf x > '{marker}'\n"
            "  exit 99\n"
            "fi\n"
            "shift\n"
            "if [ \"$1\" = \"search\" ]; then\n"
            "  printf '%s\\n' 'fdupes.x86_64 : identify duplicate files in directories'\n"
            "  exit 0\n"
            "fi\n"
            "if [ \"$1\" = \"repoquery\" ]; then\n"
            "  printf '%s\\n' 'fdupes\t2.3.0-6.fc42\tfedora\tidentify duplicate files in directories'\n"
            "  printf '%s\\n' 'rpm\t4.19.1.1-3.fc42\tfedora\tRPM package manager'\n"
            "  exit 0\n"
            "fi\n"
            f"printf x > '{marker}'\n"
            "exit 99\n",
            encoding="utf-8",
        )
        dnf5.chmod(dnf5.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

        os_release = root / "os-release"
        os_release.write_text('ID=fedora\nID_LIKE="rhel"\n', encoding="utf-8")
        env = isolated_env(root, fixture_bin)
        env["ACCLORITE_OS_RELEASE"] = str(os_release)

        capabilities = run(binary, "--capabilities", env=env)
        require(capabilities.returncode == SUCCESS, "Fedora capability probe must exit 0")
        payload = json.loads(capabilities.stdout)
        require(payload["integrations"]["dnf_packages"] is True,
                "Fedora fixture must activate the DNF package backend")
        require(payload["integrations"]["arch_packages"] is False,
                "Fedora fixture must not activate the Arch package backend")
        require(payload["integrations"]["apt_packages"] is False,
                "Fedora fixture must not activate the APT package backend")
        require(payload["integrations"]["zypper_packages"] is False,
                "Fedora fixture must not activate the Zypper package backend")
        require(payload["integrations"]["xbps_packages"] is False,
                "Fedora fixture must not activate the XBPS package backend")

        search = run(binary, "--json", "duplicate files", env=env)
        require(search.returncode == SUCCESS, "Fedora package discovery query must succeed")
        search_json = json.loads(search.stdout)
        require(search_json["results"] and search_json["results"][0]["command"] == "fdupes",
                "DNF backend should discover an uninstalled repository tool")
        require("dnf" in search_json["results"][0]["source"].split("+"),
                "DNF discovery must retain package provenance")
        require(not marker.exists(),
                "Fedora backend must remain cache-only and avoid mutation command paths")



def test_opensuse_backend_autoselection(binary: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="acclorite-cli-zypper-backend-") as tmp:
        root = Path(tmp)
        fixture_bin = root / "bin"
        fixture_bin.mkdir(parents=True)
        marker = root / "forbidden-zypper-call"

        zypper = fixture_bin / "zypper"
        zypper.write_text(
            "#!/bin/sh\n"
            "fail() { printf x > '" + str(marker) + "'; exit 99; }\n"
            "case \" $* \" in *\" --no-refresh \"*) ;; *) fail ;; esac\n"
            "case \" $* \" in *\" --non-interactive \"*) ;; *) fail ;; esac\n"
            "case \" $* \" in *\" --xmlout \"*) ;; *) fail ;; esac\n"
            "case \" $* \" in *\" --ignore-unknown \"*) ;; *) fail ;; esac\n"
            "cfg=''\nprev=''\n"
            "for arg in \"$@\"; do [ \"$prev\" = '--config' ] && cfg=\"$arg\"; prev=\"$arg\"; done\n"
            "[ -n \"$cfg\" ] || fail\n"
            "policy=0\n"
            "while IFS= read -r line; do\n"
            "  case \"$line\" in *runSearchPackages*never*) policy=1 ;; esac\n"
            "done < \"$cfg\"\n"
            "[ \"$policy\" = 1 ] || fail\n"
            "case \" $* \" in *\" refresh \"*|*\" install \"*|*\" update \"*|*\" remove \"*) fail ;; esac\n"
            "case \" $* \" in *\" search \"*) ;; *) fail ;; esac\n"
            "case \" $* \" in\n"
            "  *\" --details \"*)\n"
            "    printf '%s\\n' '<stream><search-result><solvable-list>'\n"
            "    printf '%s\\n' '<solvable status=\"not-installed\" name=\"fdupes\" kind=\"package\" edition=\"2.3.0-1.2\" arch=\"x86_64\" repository=\"repo-oss\"/>'\n"
            "    printf '%s\\n' '<solvable status=\"installed\" name=\"rpm\" kind=\"package\" edition=\"4.20.1-1.1\" arch=\"x86_64\" repository=\"repo-oss\"/>'\n"
            "    printf '%s\\n' '</solvable-list></search-result></stream>' ;;\n"
            "  *)\n"
            "    printf '%s\\n' '<stream><search-result><solvable-list><solvable status=\"not-installed\" name=\"fdupes\" summary=\"Identify duplicate files in directories\" kind=\"package\"/></solvable-list></search-result></stream>' ;;\n"
            "esac\nexit 0\n",
            encoding="utf-8",
        )
        zypper.chmod(zypper.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

        rpm = fixture_bin / "rpm"
        rpm.write_text("#!/bin/sh\nprintf '%s\\n' 'rpm\t4.20.1-1.1'\nexit 0\n", encoding="utf-8")
        rpm.chmod(rpm.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

        os_release = root / "os-release"
        os_release.write_text('ID=opensuse-tumbleweed\nID_LIKE="suse opensuse"\n', encoding="utf-8")
        env = isolated_env(root, fixture_bin)
        env["ACCLORITE_OS_RELEASE"] = str(os_release)

        capabilities = run(binary, "--capabilities", env=env)
        require(capabilities.returncode == SUCCESS, "openSUSE capability probe must exit 0")
        payload = json.loads(capabilities.stdout)
        require(payload["integrations"]["zypper_packages"] is True,
                "openSUSE fixture must activate the Zypper package backend")
        require(payload["integrations"]["arch_packages"] is False,
                "openSUSE fixture must not activate the Arch package backend")
        require(payload["integrations"]["apt_packages"] is False,
                "openSUSE fixture must not activate the APT package backend")
        require(payload["integrations"]["dnf_packages"] is False,
                "openSUSE fixture must not activate the DNF package backend")
        require(payload["integrations"]["xbps_packages"] is False,
                "openSUSE fixture must not activate the XBPS package backend")

        search = run(binary, "--json", "duplicate files", env=env)
        require(search.returncode == SUCCESS, "openSUSE package discovery query must succeed")
        search_json = json.loads(search.stdout)
        require(search_json["results"] and search_json["results"][0]["command"] == "fdupes",
                "Zypper backend should discover an uninstalled repository tool")
        require("zypper" in search_json["results"][0]["source"].split("+"),
                "Zypper discovery must retain package provenance")
        require(not marker.exists(),
                "openSUSE backend must remain no-refresh, plugin-disabled, and query-only")



def test_void_backend_autoselection(binary: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="acclorite-cli-xbps-backend-") as tmp:
        root = Path(tmp)
        fixture_bin = root / "bin"
        fixture_bin.mkdir(parents=True)
        marker = root / "forbidden-xbps-call"

        xbps_query = fixture_bin / "xbps-query"
        xbps_query.write_text(
            "#!/bin/sh\n"
            "fail() { printf x > '" + str(marker) + "'; exit 99; }\n"
            "for arg in \"$@\"; do\n"
            "  case \"$arg\" in -M|--memory-sync|--repository|--repository=*) fail ;; esac\n"
            "done\n"
            "if [ \"$1\" = '-L' ]; then printf '%s\\n' '123 https://repo-default.voidlinux.org/current'; exit 0; fi\n"
            "if [ \"$1\" = '-l' ]; then printf '%s\\n' '[*] xbps-0.60.7_1'; exit 0; fi\n"
            "has_regex=0; has_search=0\n"
            "for arg in \"$@\"; do\n"
            "  [ \"$arg\" = '--regex' ] && has_regex=1\n"
            "  [ \"$arg\" = '-Rs' ] && has_search=1\n"
            "done\n"
            "[ \"$has_regex\" = 1 ] && [ \"$has_search\" = 1 ] || fail\n"
            "printf '%s\\n' '[-] fdupes-2.3.0_1 identify duplicate files in directories'\n"
            "exit 0\n",
            encoding="utf-8",
        )
        xbps_query.chmod(xbps_query.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

        helper = fixture_bin / "xbps-uhelper"
        helper.write_text(
            "#!/bin/sh\n"
            "case \"$1\" in\n"
            "  getpkgname) printf '%s\\n' 'fdupes' ;;\n"
            "  getpkgversion) printf '%s\\n' '2.3.0_1' ;;\n"
            "  *) exit 99 ;;\n"
            "esac\n",
            encoding="utf-8",
        )
        helper.chmod(helper.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

        xbps_install = fixture_bin / "xbps-install"
        xbps_install.write_text(
            "#!/bin/sh\nprintf x > '" + str(marker) + "'\nexit 99\n",
            encoding="utf-8",
        )
        xbps_install.chmod(xbps_install.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

        os_release = root / "os-release"
        os_release.write_text("ID=void\n", encoding="utf-8")
        env = isolated_env(root, fixture_bin)
        env["ACCLORITE_OS_RELEASE"] = str(os_release)

        capabilities = run(binary, "--capabilities", env=env)
        require(capabilities.returncode == SUCCESS, "Void capability probe must exit 0")
        payload = json.loads(capabilities.stdout)
        require(payload["integrations"]["xbps_packages"] is True,
                "Void fixture must activate the XBPS package backend")
        require(payload["integrations"]["arch_packages"] is False,
                "Void fixture must not activate the Arch package backend")
        require(payload["integrations"]["apt_packages"] is False,
                "Void fixture must not activate the APT package backend")
        require(payload["integrations"]["dnf_packages"] is False,
                "Void fixture must not activate the DNF package backend")
        require(payload["integrations"]["zypper_packages"] is False,
                "Void fixture must not activate the Zypper package backend")

        search = run(binary, "--json", "duplicate files", env=env)
        require(search.returncode == SUCCESS, "Void package discovery query must succeed")
        search_json = json.loads(search.stdout)
        require(search_json["results"] and search_json["results"][0]["command"] == "fdupes",
                "XBPS backend should discover an uninstalled repository tool")
        require("xbps" in search_json["results"][0]["source"].split("+"),
                "XBPS discovery must retain package provenance")
        require(not marker.exists(),
                "Void backend must never enable memory-sync or invoke xbps-install")


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


def test_hidden_lore_command(binary: Path) -> None:
    lore = run(binary, "--lore")
    require(lore.returncode == SUCCESS, "hidden --lore command must succeed when bundled quote data exists")
    require("\n  — " in lore.stdout and lore.stdout.startswith("“") and "”" in lore.stdout,
            "hidden --lore command must emit compact quote + attribution flavor text")

    help_text = run(binary, "--help")
    require("--lore" not in help_text.stdout, "lore command must remain absent from normal help")

    conflict = run(binary, "--lore", "rg")
    require(conflict.returncode == USAGE_ERROR, "--lore must remain a standalone hidden command")


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


def test_reindex_preserves_live_index_on_failure(binary: Path) -> None:
    capabilities = json.loads(run(binary, "--capabilities").stdout)
    if not capabilities["build"]["sqlite_fts"]:
        return

    with tempfile.TemporaryDirectory(prefix="acclorite-cli-reindex-recovery-") as tmp:
        root = Path(tmp)
        fixture_bin = root / "bin"
        fixture_bin.mkdir(parents=True)
        fixture = fixture_bin / "m8recoverytool"
        fixture.write_text("fixture\n", encoding="utf-8")
        fixture.chmod(fixture.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

        env = isolated_env(root, fixture_bin)
        rebuilt = run(binary, "--reindex", env=env)
        require(rebuilt.returncode == SUCCESS, "recovery fixture initial reindex must succeed")

        db = Path(env["ACCLORITE_INDEX_PATH"])
        require(db.is_file(), "successful reindex must create the live index")
        before = db.read_bytes()
        require(before, "recovery fixture live index must not be empty")

        fixture.unlink()
        failed = run(binary, "--reindex", env=env)
        require(failed.returncode == OPERATIONAL_ERROR,
                "empty-catalog reindex must report operational failure")
        require(db.is_file(), "failed reindex must preserve the previous live index")
        require(db.read_bytes() == before,
                "failed reindex must preserve the previous live index byte-for-byte")
        require(not Path(str(db) + ".rebuild").exists(),
                "failed reindex must clean the staging database")
        require(not Path(str(db) + ".rebuild-wal").exists(),
                "failed reindex must clean the staging WAL")
        require(not Path(str(db) + ".rebuild-shm").exists(),
                "failed reindex must clean the staging shared-memory file")


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



def test_bundled_lore_corpus() -> None:
    corpus = Path(__file__).resolve().parents[1] / "data" / "quotes.tsv"
    require(corpus.is_file(), "bundled lore corpus must exist in the source tree")

    entries: list[tuple[str, str]] = []
    for raw in corpus.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        require(len(parts) == 2, "every shipped lore row must remain speaker<TAB>quote")
        speaker, quote = parts
        require(bool(speaker) and bool(quote), "shipped lore fields must be non-empty")
        entries.append((speaker, quote))

    require(len(entries) >= 90, "lore corpus must remain extensive rather than collapsing to a tiny sampler")
    require(len({quote for _, quote in entries}) == len(entries), "shipped lore quote texts must remain unique")
    require(sum(speaker == "Regis" for speaker, _ in entries) >= 15,
            "lore corpus must retain the intentional Regis skew")
    require(("Regis", "Begone, thot.") in entries,
            "required manual Regis override must remain in the shipped lore corpus")

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()

    test_capabilities(binary)
    test_debian_backend_autoselection(binary)
    test_fedora_backend_autoselection(binary)
    test_opensuse_backend_autoselection(binary)
    test_void_backend_autoselection(binary)
    test_usage_errors(binary)
    test_hidden_lore_command(binary)
    test_bundled_lore_corpus()
    test_search_exit_codes(binary)
    test_reindex_preserves_live_index_on_failure(binary)
    test_doctor_and_operational_exit_codes(binary)
    print("Acclorite CLI contract tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
