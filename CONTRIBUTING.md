# Contributing to Acclorite

Acclorite is intentionally conservative: deterministic behavior, local evidence, offline normal operation, and no command execution are product constraints rather than implementation accidents.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Also validate the no-SQLite fallback when changing indexing or source-selection code:

```bash
cmake -S . -B build-nosqlite \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_DISABLE_FIND_PACKAGE_SQLite3=TRUE
cmake --build build-nosqlite -j"$(nproc)"
ctest --test-dir build-nosqlite --output-on-failure
```

M11 semantic-retrieval experiments are parked and excluded from normal builds. Enable them only when intentionally working on that research track:

```bash
cmake -S . -B build-research -DACCLORITE_BUILD_RESEARCH=ON
```

Research dependencies must never become accidental requirements for the normal CLI.

## Pull requests

Keep changes narrow and explain the user-visible behavior they change. Add or update tests for behavioral changes, preserve stable machine-interface semantics unless the change explicitly advances the schema contract, and avoid network or execution side effects in search/Doctor paths.

For ranking work, benchmark evidence is preferred over intuition. For syntax/guidance work, missing evidence is preferable to fabricated command forms.

## Style

The C++ codebase targets C++20 and is built with `-Wall -Wextra -Wpedantic` on GCC/Clang. Match the surrounding style rather than introducing a formatter-only rewrite in unrelated changes.
