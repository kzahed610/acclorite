#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import os
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("m11_injection_probe", ROOT / "m11_injection_probe.py")
assert SPEC and SPEC.loader
probe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe)


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def test_runner_protocol() -> None:
    with tempfile.TemporaryDirectory() as raw:
        path = Path(raw) / "runner"
        path.write_text(
            "#!/bin/sh\n"
            "seen=0\n"
            "while [ $# -gt 0 ]; do\n"
            "  if [ \"$1\" = --hint ]; then [ \"$2\" = right-tool ] && seen=1; shift 2; continue; fi\n"
            "  if [ \"$1\" = -- ]; then shift; break; fi\n"
            "  shift\n"
            "done\n"
            "if [ $seen -eq 1 ]; then printf '%s\\n' right-tool decoy; else printf '%s\\n' decoy; fi\n",
            encoding="utf-8",
        )
        os.chmod(path, 0o755)
        ranking, error = probe.run_injection_runner(path, "show process ancestry", ["right-tool"], 2.0)
        check(not error, f"runner should succeed: {error}")
        check(ranking[:2] == ["right-tool", "decoy"], "hint protocol preserves ranked command lines")


def test_diagnostics() -> None:
    cases = [
        {"expect": {"top1_any": ["right"]}},
        {"expect": {"top3_any": ["other"]}},
    ]
    semantic = [["right", "x"], ["other", "y"]]
    baseline = [["x"], ["other"]]
    injected = [["right", "x"], ["x", "other"]]
    diag = probe.diagnostic_counts(semantic, baseline, injected, cases, 20)
    check(diag["semantic_candidate_hits"] == 2, "both expected commands are present in semantic hints")
    check(diag["semantic_hits_retained_top10"] == 2, "injection retains both semantic hits")
    check(diag["new_top3_recoveries"] == 1, "one case is newly promoted into Top-3")
    check(diag["new_top10_recoveries"] == 1, "one case is newly rescued into Top-10")
    check(diag["top10_regressions"] == 0, "no baseline Top-10 hit is lost")


if __name__ == "__main__":
    test_runner_protocol()
    test_diagnostics()
    print("m11 injection probe tests passed")
