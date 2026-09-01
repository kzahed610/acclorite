# Acclorite

Acclorite is a local-first Linux tool-discovery assistant.

> I know what I want to do — just tell me what Linux tool I should be looking at.

The current development build is still entirely deterministic: **no LLM is involved.**

## Current knowledge pipeline

Acclorite can now build a persistent local tool index from:

- executable discovery through `PATH`;
- command-oriented local manual metadata (`apropos`, or `man -k` fallback);
- freedesktop `.desktop` application metadata.

When SQLite3 development support is available at build time, Acclorite stores this catalog in an FTS5 database and queries it with BM25-backed full-text retrieval before applying its own concept-aware relevance scoring.

If SQLite support is unavailable, the build still succeeds and Acclorite falls back to the live PATH/man/desktop sources.

## CLI / GUI / Hybrid capability

Candidate tools track two independent capabilities:

- CLI-capable;
- GUI-capable.

This produces a derived interface label:

```text
CLI
GUI
Hybrid
Unknown
```

For example, a command found in `PATH` and also referenced by an installed `.desktop` application naturally becomes `Hybrid`. This is intended to let tools such as KDE Ark participate in discovery without hard-coded application names.

## Build

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

On Arch Linux, the normal `sqlite` package provides the headers and shared library needed for the indexed path.

## Usage

```bash
./build/acclorite grep
./build/acclorite "search text"
./build/acclorite "disk usage"
./build/acclorite "network connections"
./build/acclorite "process monitoring"
./build/acclorite "archive files"
```

Machine-readable output:

```bash
./build/acclorite --json "archive files"
```

Force a clean local-index rebuild:

```bash
./build/acclorite --reindex
```

Default index location:

```text
$XDG_CACHE_HOME/acclorite/index.db
```

or, when `XDG_CACHE_HOME` is unset:

```text
~/.cache/acclorite/index.db
```

## What changed in 0.0.4-dev

- persistent SQLite FTS5 tool index;
- BM25-backed indexed retrieval;
- first-run automatic index creation;
- explicit `--reindex` command;
- PATH + man + desktop metadata merged into the indexed tool catalog;
- GUI application metadata participates in search;
- CLI / GUI / Hybrid capability model;
- interface capability emitted in terminal and JSON output;
- JSON schema bumped to version 2;
- SQLite is optional at build time; live-search fallback remains functional;
- query relevance scoring extracted into a reusable module;
- regression coverage for persistent semantic retrieval and Ark-style hybrid tools.

## Current limitations

The persistent index is deliberately simple in this development build:

- automatic package/database staleness detection is not implemented yet;
- full man-page bodies are not indexed yet;
- desktop-entry parsing is intentionally conservative;
- RapidFuzz typo tolerance is not integrated yet;
- phrase/entity handling is still small;
- no Arch package-repository backend exists yet;
- no confidence/ambiguity model exists yet;
- no common-tool/simplicity prior exists yet.

The next major retrieval work is **richer documentation indexing + fuzzy matching**, followed by the first-class Arch package backend.
