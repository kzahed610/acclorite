#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("m11_embedding_probe", HERE / "m11_embedding_probe.py")
assert SPEC and SPEC.loader
probe = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = probe
SPEC.loader.exec_module(probe)

raw = r'''\" comment
.TH demo 1
.SH NAME
demo \- inspect a file
.SH DESCRIPTION
.B demo
shows readable information about binary contents.
.TP
.B --follow
keep watching appended data
'''
plain = probe.roff_to_plain(raw)
assert "inspect a file" in plain, plain
assert "binary contents" in plain, plain
assert "--follow" in plain, plain
assert "keep watching appended data" in plain, plain
assert ".TH" not in plain, plain

class FakeTokenizer:
    def encode(self, text, add_special_tokens=False):
        return list(range(len(text.split())))

    def decode(self, token_ids, skip_special_tokens=True, clean_up_tokenization_spaces=True):
        return " ".join(f"t{i}" for i in token_ids)

    def num_special_tokens_to_add(self, pair=False):
        return 2

page = probe.PlainPage("demo", "/tmp/demo.1", " ".join(f"w{i}" for i in range(70)))
chunks = probe.chunk_page(page, FakeTokenizer(), chunk_tokens=32, overlap_tokens=8, model_limit=64)
assert len(chunks) == 3, len(chunks)
assert all(chunk.command == "demo" for chunk in chunks)
assert all(chunk.text.startswith("Command demo. ") for chunk in chunks)

accepted = probe._accepted({"expect": {"top1_any": ["a"], "top3_any": ["b", "a"]}})
assert accepted == {"a", "b"}, accepted

print("m11 embedding probe tests: PASS")
