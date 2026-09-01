# Acclorite

Acclorite is a local-first Linux tool-discovery assistant.

> I know what I want to do — just tell me what Linux tool I should be looking at.

This repository is currently at the first implementation milestone. The current development build searches executable names available in `PATH`; documentation indexing, FTS5, fuzzy matching, synonym/phrase expansion, and Arch package metadata are the next layers.

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Current usage

```bash
./build/acclorite grep
./build/acclorite --json grep
```

The current PATH-only search is intentionally primitive. It exists to establish the native core/result/source/renderer architecture before the actual retrieval engine is layered on top.
