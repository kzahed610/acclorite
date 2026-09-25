#!/usr/bin/env python3
"""Build Acclorite's compact lore corpus from the maintainer master dataset.

The master dataset is intentionally not vendored. This tool validates it, selects
only quotes independently present in both source datasets, balances coverage
across volumes/types, preserves quote text and attribution byte-for-byte, and
adds explicitly declared manual overrides.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import unicodedata
from collections import defaultdict
from pathlib import Path

ALLOWED_TYPES = ("funny", "badass", "emotional", "wisdom")
EXPECTED_VOLUMES = tuple(range(1, 11))
TARGET_LENGTH = 90
BASE_PER_VOLUME_TYPE = 2

# These are deliberate project-level additions. They are not represented as
# entries from the master dataset and must never be silently merged back into it.
MANUAL_OVERRIDES = (
    ("Regis", "Begone, thot."),
)


def fail(message: str) -> "NoReturn":
    raise ValueError(message)


def normalized_quote(text: str) -> str:
    text = unicodedata.normalize("NFKC", text).casefold()
    text = re.sub(r"[^\w]+", "", text, flags=re.UNICODE)
    return text


def quote_score(entry: dict) -> tuple[int, str]:
    length = len(entry["quote"])
    penalty = abs(length - TARGET_LENGTH)
    if length < 25:
        penalty += 50
    if length > 180:
        penalty += 100
    return penalty, entry["master_id"]


def validate_master(data: dict) -> list[dict]:
    quotes = data.get("quotes")
    if not isinstance(quotes, list) or not quotes:
        fail("master dataset must contain a non-empty quotes array")
    if data.get("quote_count") != len(quotes):
        fail(f"quote_count={data.get('quote_count')!r} does not match {len(quotes)} entries")

    required = {
        "quote", "speaker", "type", "sources", "volume", "master_id",
        "chapter", "chapter_title", "book_title", "source_volumes",
    }
    seen_ids: set[str] = set()
    seen_exact: set[str] = set()
    seen_normalized: set[str] = set()

    for index, entry in enumerate(quotes, start=1):
        if not isinstance(entry, dict):
            fail(f"quotes[{index - 1}] is not an object")
        missing = sorted(required - entry.keys())
        if missing:
            fail(f"quotes[{index - 1}] missing fields: {', '.join(missing)}")

        master_id = entry["master_id"]
        expected_id = f"tbate-{index:04d}"
        if master_id != expected_id:
            fail(f"expected {expected_id}, found {master_id!r}")
        if master_id in seen_ids:
            fail(f"duplicate master_id: {master_id}")
        seen_ids.add(master_id)

        if entry["volume"] not in EXPECTED_VOLUMES:
            fail(f"{master_id}: unsupported volume {entry['volume']!r}")
        if entry["type"] not in ALLOWED_TYPES:
            fail(f"{master_id}: unsupported type {entry['type']!r}")
        if not isinstance(entry["sources"], list) or not entry["sources"]:
            fail(f"{master_id}: sources must be a non-empty array")

        quote = entry["quote"]
        speaker = entry["speaker"]
        if not isinstance(quote, str) or not quote.strip():
            fail(f"{master_id}: empty/non-string quote")
        if not isinstance(speaker, str) or not speaker.strip():
            fail(f"{master_id}: empty/non-string speaker")
        if any(ch in quote for ch in "\t\r\n") or any(ch in speaker for ch in "\t\r\n"):
            fail(f"{master_id}: tab/newline would make the runtime TSV ambiguous")

        if quote in seen_exact:
            fail(f"{master_id}: duplicate exact quote text remains after master dedup")
        seen_exact.add(quote)
        key = normalized_quote(quote)
        if key in seen_normalized:
            fail(f"{master_id}: duplicate normalized quote text remains after master dedup")
        seen_normalized.add(key)

    return quotes


def choose_group(candidates: list[dict], count: int) -> list[dict]:
    ordered = sorted(candidates, key=quote_score)
    chosen: list[dict] = []
    speakers: set[str] = set()

    # First pass prefers distinct speakers so Arthur's huge corpus does not
    # drown every volume/type cell.
    for entry in ordered:
        if entry["speaker"] in speakers:
            continue
        chosen.append(entry)
        speakers.add(entry["speaker"])
        if len(chosen) == count:
            return chosen

    for entry in ordered:
        if entry not in chosen:
            chosen.append(entry)
            if len(chosen) == count:
                break
    return chosen


def select_runtime_quotes(quotes: list[dict]) -> tuple[list[dict], list[tuple[str, str]]]:
    cross_confirmed = [
        entry for entry in quotes
        if {"chatgpt", "lumen"}.issubset(set(entry["sources"]))
    ]
    groups: dict[tuple[int, str], list[dict]] = defaultdict(list)
    for entry in cross_confirmed:
        groups[(entry["volume"], entry["type"])].append(entry)

    selected: dict[str, dict] = {}
    for volume in EXPECTED_VOLUMES:
        for quote_type in ALLOWED_TYPES:
            for entry in choose_group(groups[(volume, quote_type)], BASE_PER_VOLUME_TYPE):
                selected[entry["master_id"]] = entry

    # The original product design intentionally skews the lore toward Regis.
    # Keep every cross-confirmed Regis line in the runtime corpus.
    for entry in cross_confirmed:
        if entry["speaker"] == "Regis":
            selected[entry["master_id"]] = entry

    ordered = sorted(selected.values(), key=lambda entry: entry["master_id"])
    existing = {entry["quote"] for entry in ordered}
    overrides = [item for item in MANUAL_OVERRIDES if item[1] not in existing]
    return ordered, overrides


def write_runtime(path: Path, selected: list[dict], overrides: list[tuple[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as out:
        out.write("# attribution<TAB>quote\n")
        out.write("# generated from the maintainer Volume 1-10 master dataset\n")
        out.write("# source-backed entries below were present in both ChatGPT and Lumen source datasets\n")
        for entry in selected:
            out.write(f"{entry['speaker']}\t{entry['quote']}\n")
        if overrides:
            out.write("# manual project overrides (not represented as master-dataset entries)\n")
            for speaker, quote in overrides:
                out.write(f"{speaker}\t{quote}\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="build the compact Acclorite lore corpus")
    parser.add_argument("master", type=Path, help="path to tbate_banger_quotes_v1-v10_master.json")
    parser.add_argument("output", nargs="?", type=Path, default=Path("data/quotes.tsv"))
    args = parser.parse_args()

    try:
        with args.master.open("r", encoding="utf-8") as source:
            data = json.load(source)
        quotes = validate_master(data)
        selected, overrides = select_runtime_quotes(quotes)
        write_runtime(args.output, selected, overrides)
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    cross_count = sum({"chatgpt", "lumen"}.issubset(set(entry["sources"])) for entry in quotes)
    regis_count = sum(entry["speaker"] == "Regis" for entry in selected) + sum(
        speaker == "Regis" for speaker, _ in overrides
    )
    print(f"master quotes: {len(quotes)}")
    print(f"cross-confirmed pool: {cross_count}")
    print(f"runtime corpus: {len(selected) + len(overrides)} ({regis_count} Regis)")
    print(f"manual overrides: {len(overrides)}")
    print(f"wrote: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
