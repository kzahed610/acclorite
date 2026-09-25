> **Status — parked for release polish (2026-09-25):** M11 semantic-runtime integration is intentionally paused. The deterministic standalone CLI is the release focus. Research remains opt-in via `ACCLORITE_BUILD_RESEARCH=ON` and must not become a normal build/runtime dependency.

# Acclorite M11 — Semantic Assist Production Design

Status: **production-design track after semantic-retrieval research closure**

This document records the product boundary earned by M11 measurements and the remaining work needed before Semantic Assist
can ship. It is intentionally stricter than the research probes.

## 1. Accepted role

Semantic Assist is an optional local recall subsystem. It may surface source-substantiated command alternatives that
ordinary deterministic discovery did not place in its visible Top-10.

It does **not** own Acclorite's primary ranking.

```text
query
  ├── deterministic Acclorite ──> canonical ranking / actionable answer
  └── semantic retrieval
          ↓
      command identities
          ↓
      deterministic source substantiation
          ↓
      separate semantic alternatives
```

The deterministic ranking is never interleaved with Semantic Assist. This preserves the frozen M10 ranking contract by
construction.

## 2. Authority boundary

Semantic data may establish retrieval relevance only. It may never establish:

- executable/package existence;
- provenance;
- command syntax, options, subcommands, or positional structure;
- argument binding;
- invocation completeness;
- safety classification;
- package-manager facts;
- permission/privilege requirements.

A semantic command identity becomes user-visible only after an ordinary Acclorite source independently substantiates it.

## 3. Runtime requirements

A shippable implementation must satisfy all of the following:

1. the deterministic CLI has **zero hard dependency** on an ML runtime or model asset;
2. missing, incompatible, or corrupt semantic assets disable Semantic Assist cleanly and never degrade deterministic search;
3. query-time Semantic Assist performs no network access;
4. model revision, tokenizer files, index format, and checksums are pinned/versioned;
5. model/index updates are explicit packaging/update operations, never surprise query-time downloads;
6. the native runtime must not require Python, PyTorch, or SentenceTransformers;
7. semantic results remain a separate machine/terminal surface and are clearly distinguishable from deterministic ranks;
8. all five distro backends and no-SQLite builds remain valid when Semantic Assist is absent.

## 4. Current model candidate

Research used `sentence-transformers/all-MiniLM-L6-v2` with 384-dimensional normalized embeddings. The model currently
publishes ONNX artifacts and advertises Apache-2.0 licensing. Packaging must pin and re-verify the exact revision and
license before release rather than depending on a floating model identifier.

The first production-runtime candidate is the repository-provided AVX2 uint8 ONNX model:

```text
onnx/model_quint8_avx2.onnx
```

It is roughly one quarter the size of the float ONNX artifact. ARM64 and other architectures need an explicit asset/runtime
policy; the deterministic CLI remains fully usable when no compatible Semantic Assist asset is installed.

## 5. Index contract

The current research index is not yet a product file format. A production format must record at minimum:

```text
format_version
model_id
model_revision
embedding_dimensions
embedding_dtype
normalization
passage_selection_policy
passage_budget
command identity
source identity / source fingerprint
vector payload
```

The loader must reject incompatible model/index pairs rather than silently comparing vectors produced by different
representations.

The current four-passage research index is approximately 14k vectors. Two-passage and one-passage budgets remain viable
space/quality alternatives, but the product choice should follow runtime/index-lifecycle measurements rather than another
retrieval-quality sweep.

## 6. Runtime qualification sequence

### RQ1 — ONNX representation compatibility

Reuse the existing PyTorch-built compact index. Encode only benchmark queries with quantized ONNX and compare
separate-channel rescue coverage against the authoritative PyTorch Experiment G output.

Acceptance target: at most one lost PyTorch rescue at assist@3 and assist@5 for every corpus/miss family.

The runtime probe must normalize raw benchmark ``expect`` fields to the semantic-assist ``accepted`` schema and must
cross-check computed miss counts against Experiment G's stored deterministic baseline metrics. Any mismatch is an analysis
failure; a zero-vs-zero comparison is never allowed to qualify a runtime.

This isolates model/runtime representation quality from index-building cost.

### RQ2 — native C++ query encoder

Only after RQ1 passes:

- load the pinned ONNX artifact through ONNX Runtime's C++ API;
- implement or embed the exact pinned WordPiece/tokenizer behavior;
- reproduce SentenceTransformers mean-pooling + normalization semantics;
- compare native vectors/rankings against RQ1;
- measure cold load, warm query latency, peak RSS, and failure handling.

Python remains a reference oracle only.

### RQ3 — index lifecycle

Determine how the semantic index is produced and updated without imposing an eight-minute first-run tax. Candidate designs
must be measured rather than assumed, for example:

- release-built baseline index plus local source substantiation;
- distro/release-generated indexes;
- bounded local augmentation;
- incremental rebuild keyed to source fingerprints.

Do not perform expensive automatic indexing in the foreground of an ordinary query.

### RQ4 — product surface

Define stable optional machine fields and terminal presentation only after the runtime/index lifecycle is acceptable.
Semantic alternatives should be additive and schema-compatible; clients must be able to ignore them.

## 7. Explicit non-goals

Do not reopen the rejected M11 paths merely because production engineering becomes difficult:

- no semantic takeover of deterministic Top-1;
- no merged deterministic/semantic ranking;
- no confidence/rank/cosine threshold stack;
- no runtime internet inference;
- no LLM-generated command syntax;
- no ML-derived safety claims.

If the native runtime cannot meet footprint/latency requirements, Semantic Assist may remain optional or unshipped. M11's
research result establishes value, not an obligation to accept a bad production dependency.

### RQ1 result — quantized ONNX qualified on real CachyOS

After correcting the rescue-analysis schema bug, the AVX2 uint8 ONNX artifact passed Runtime Qualification 1 on the real
CachyOS reference machine. Its separate-channel rescue counts matched the accepted PyTorch research runtime exactly on
`research-v1` and `heldout-v1`, and improved transfer-v1 rescue by one case at assist@1/@3/@5. The corrected RQ1 output
therefore qualifies the quantized ONNX representation for native-runtime work.

The Python/SentenceTransformers ONNX wrapper emitted noisy backend diagnostics during cached-model discovery, but inference
completed normally. RQ2 intentionally removes that wrapper from the encoder under test rather than attempting to paper over
wrapper-specific diagnostics.

### RQ2 — native C++ ONNX encoder qualification

RQ2 is a research-only native runner, not yet a production Semantic Assist integration. It requires ONNX Runtime and
`libutf8proc` development files at build time and is compiled only when both pkg-config modules are present. The ordinary
Acclorite binary remains independent of both libraries.

The native runner implements the pinned model contract directly:

```text
UTF-8 query
  ↓
BERT BasicTokenizer semantics
  ↓
WordPiece using pinned vocab.txt
  ↓
[CLS] / [SEP], right truncation at 256 tokens
  ↓
quantized MiniLM ONNX through ONNX Runtime C++
  ↓
attention-mask mean pooling
  ↓
L2 normalization
  ↓
384-dimensional query embedding
```

Qualification is fail-closed in two independent ways:

1. **tokenizer parity:** native token IDs must exactly match the cached Hugging Face tokenizer for every M11 benchmark query
   plus a fixed Unicode/punctuation parity set;
2. **retrieval/rescue parity:** native embeddings are searched against the already-qualified compact index and must remain
   within RQ1's accepted tolerance: at most one lost rescue at assist@3 and assist@5 for every corpus/miss family.

Python remains only the qualification harness/oracle. The native encoder itself does not import Python, Transformers,
SentenceTransformers, or Hugging Face code at runtime.
