#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("m11_compact_embedding_probe", HERE / "m11_compact_embedding_probe.py")
assert SPEC and SPEC.loader
probe = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = probe
SPEC.loader.exec_module(probe)

Chunk = probe.embedprobe.Chunk
chunks = [
    Chunk("demo", "/tmp/demo.1", "Command demo. inspect objects and report status"),
    Chunk("demo", "/tmp/demo.1", "rare capability alpha frobnicate widgets"),
    Chunk("demo", "/tmp/demo.1", "common file option value arguments"),
    Chunk("demo", "/tmp/demo.1", "another distinct capability omega monitor events"),
    Chunk("demo", "/tmp/demo.1", "final appendix copyright authors"),
    Chunk("other", "/tmp/other.1", "Command other. transform streams"),
    Chunk("other", "/tmp/other.1", "different semantic operation beta records"),
]
selected = probe.select_passages(chunks, 2)
by_command = {}
for item in selected:
    by_command.setdefault(item.chunk.command, []).append(item)
assert set(by_command) == {"demo", "other"}, by_command
assert len(by_command["demo"]) == 2
assert len(by_command["other"]) == 2
assert by_command["demo"][0].original_ordinal == 0
assert by_command["demo"][0].selection_rank == 1
assert {item.selection_rank for item in by_command["demo"]} == {1, 2}
assert probe.lexical_terms("Command demo. unique capability", "demo") == {"unique", "capability"}

print("m11 compact embedding probe tests: PASS")
