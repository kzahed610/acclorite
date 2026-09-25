#!/usr/bin/env python3
from __future__ import annotations

import gzip
import importlib.util
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("m11_mantext_probe", HERE / "m11_mantext_probe.py")
assert SPEC and SPEC.loader
probe = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = probe
SPEC.loader.exec_module(probe)

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    man1, man8 = root / "man1", root / "man8"
    man1.mkdir(); man8.mkdir()
    with gzip.open(man1 / "watch.1.gz", "wt", encoding="utf-8") as handle:
        handle.write("WATCH execute a program periodically and show output repeatedly every interval")
    (man1 / "periodic.1").write_text(".so man1/watch.1\n", encoding="utf-8")
    (man1 / "noise.1").write_text("NOISE unrelated archive formatter", encoding="utf-8")
    (man8 / "admin.8").write_text("ADMIN inspect system administration state", encoding="utf-8")
    (man1 / "ignored.5").write_text("not section one", encoding="utf-8")

    pages = probe.discover_pages([root])
    index, skipped = probe.build_documents(pages)
    assert not skipped, skipped
    assert {"watch", "periodic", "noise", "admin"} <= index.documents.keys()
    assert "ignored" not in index.documents
    assert index.documents["periodic"].counts == index.documents["watch"].counts
    ranking = probe.bm25_rank("run something repeatedly every interval", index, 10)
    assert ranking and ranking[0][0] in {"periodic", "watch"}, ranking
    corpus = {
        "name": "synthetic",
        "cases": [{
            "id": "periodic",
            "query": "run something repeatedly every interval",
            "expect": {"top1_any": ["watch", "periodic"], "top3_any": ["watch", "periodic"]},
        }],
    }
    report = probe.evaluate(corpus, index, 10)
    assert report["available_cases"] == 1
    assert report["top1"] == report["top3"] == report["top10"] == 1

print("m11 man-text probe tests: PASS")
