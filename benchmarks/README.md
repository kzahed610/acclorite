# Acclorite benchmark harness

This directory is **evaluation tooling**, not part of the Acclorite runtime.
It intentionally uses Python's standard library to drive the compiled C++ CLI
through `--json` and score a versioned query corpus.

Run the default Arch-focused corpus:

```bash
python3 benchmarks/run.py --binary ./build/acclorite
```

Write reproducible artifacts:

```bash
mkdir -p benchmarks/results
python3 benchmarks/run.py \
  --binary ./build/acclorite \
  --json-out benchmarks/results/benchmark-report.json \
  --markdown-out benchmarks/results/benchmark-report.md
```

Generated benchmark reports belong under `benchmarks/results/`, which is intentionally ignored by Git. The versioned corpora, harness, tests, and research tooling remain tracked; machine-specific result dumps do not.

Use `--strict` when a benchmark failure should produce a non-zero exit status.
The default mode always prints failures but exits successfully unless the
harness itself cannot run; this makes it convenient for exploratory tuning.


## Frozen v0.1.0 gate

`corpus-v1.json` is frozen as the **v0.1.0 regression gate** after the real Arch/CachyOS run reached 25/25 cases and 65/65 assertions. Do not tune future ranking changes by rewriting this corpus to accept new winners. New/adversarial coverage belongs in a new versioned corpus while v1 remains a non-regression contract.

For release validation on an Arch-family machine, run the frozen gate with `--strict`.

## Human-language adversarial gate

`corpus-human-v1.json` deliberately avoids developer-shaped prompts. It contains
incomplete recall, filler words, slang, metaphor, typos, implied tool discovery, and
syntax requests that do not say words such as `flag` or `subcommand`. It is a
separate measurement surface: **do not weaken it to match current behavior**. A
failure should either motivate a general deterministic language/semantic improvement
or remain visible until the architecture can support it.

Run it independently from the frozen regression gate:

```bash
python3 benchmarks/run.py \
  --binary ./build/acclorite \
  --corpus benchmarks/corpus-human-v1.json \
  --json-out benchmark-human-report.json \
  --markdown-out benchmark-human-report.md
```

The real CachyOS reference run reached **25/25 cases and 85/85 assertions** after the
human-intent and role-specific ranking slices. `corpus-human-v1.json` is therefore now
frozen as a strict Milestone-10 regression gate alongside `corpus-v1.json`. New hostile
language coverage belongs in a new versioned corpus rather than weakening v1.

For regression validation, run it with `--strict`.

## Actionable-answer gates

`corpus-actionable-v1.json` records the first argument-binding capability snapshot: verified
root option/subcommand selection, deterministic value binding, invocation completeness,
placeholder structure, and deliberate refusal to overclaim when root grammar is insufficient.
The real CachyOS reference reached **6/6 cases and 43/43 assertions** after the Slice-7
manual-format hotfix. Unlike the discovery/human corpora, actionable snapshots may contain
deliberate negative expectations for capabilities that later slices are expected to unlock;
therefore a superseding version is created rather than rewriting the historical snapshot.

`corpus-actionable-v2.json` records the bounded parent-proven child-man capability snapshot.
The real CachyOS reference reached **8/8 cases and 69/69 assertions** after the Slice-8
real-man alias and binder-paraphrase fixes. Its Git compound-operation cases deliberately expect
`complete = false`, preserving the historical point before child synopsis path selection.

`corpus-actionable-v3.json` records the child-synopsis completeness snapshot. It carries forward v2
unchanged except that the two parent-proven Git create-and-switch invocations must now be
`complete = true`: one explicit child `SYNOPSIS` alternative must prove the selected option/value
path and leave no unmet mandatory syntax. The real CachyOS reference reached **8/8 cases and
69/69 assertions**.

`corpus-actionable-v4.json` records the practical root-binding snapshot. It carries forward v3 and
adds common root-command binding cases whose man SYNOPSIS directly proves positional shapes:
`mv SOURCE DEST`, `cp SOURCE DEST`, `mkdir DIRECTORY...`, `grep PATTERNS [FILE]...`, `touch FILE...`,
and `rm [FILE]...`. The real CachyOS reference reached **14/14 cases and 138/138 assertions** after
the hidden-files paraphrase hotfix.

`corpus-actionable-v5.json` is the **final strict Milestone-10 actionable gate**. It carries forward v4 unchanged
and adds an `actionable_safety` assertion to every case. Complete, concrete invocations may be labeled
`ReadOnly`, `Mutating`, `Destructive`, `Privileged`, or `Network` only when requested intent and
source-backed command/option/subcommand semantics agree. Explanation-only, incomplete, placeholder,
or semantically conflicting answers remain `Unknown`.

The authoritative real CachyOS M10 run reached **14/14 cases and 152/152 assertions**. This corpus is
frozen. M11 must create separate research/versioned corpora rather than changing v5 expectations.

```bash
python3 benchmarks/run.py \
  --binary ./build/acclorite \
  --corpus benchmarks/corpus-actionable-v5.json \
  --strict \
  --json-out benchmark-actionable-v5-report.json \
  --markdown-out benchmark-actionable-v5-report.md
```

## Milestone-11 research corpus

`corpus-m11-research-v1.json` is an **exploratory failure corpus**, not a frozen regression gate. It targets
semantically clear but lexically distant Linux tasks to measure whether M10's next meaningful limitation is
discovery/ranking rather than syntax authority. It deliberately includes operations whose command names often
share little vocabulary with the request, such as `watch`, `pgrep`, `ldd`, `xargs`, `uniq`, `nohup`, `findmnt`,
`lsblk`, and checksum utilities.

Run the initial baseline **without `--strict`** because failures are expected and informative:

```bash
python3 benchmarks/run.py \
  --binary ./build/acclorite \
  --corpus benchmarks/corpus-m11-research-v1.json \
  --json-out benchmark-m11-research-v1-baseline.json \
  --markdown-out benchmark-m11-research-v1-baseline.md
```

The real CachyOS result is the authoritative Arch baseline. Keep M11 research metrics separate from the frozen
M10 corpora. If an M11 capability is accepted, create a new versioned regression snapshot instead of rewriting
this exploratory surface to hide failures.

## Corpus contract

Each case may assert only the dimensions that are meaningful for that query:

- `top1_any`: any accepted top-1 command;
- `top3_any`: at least one accepted command in the top three;
- `top10_all`: all listed commands must appear in the first ten;
- `forbid_top1`: commands that must not win;
- `frame`: expected Query Frame;
- `ambiguity`: expected confidence/ambiguity state;
- `min_confidence`: lower bound for top-candidate confidence;
- `targets_all`: named targets that must be extracted;
- `targets_none`: whether no named entity target may be invented from filler language;
- `clarification_ids`: deterministic clarification choices that must be present;
- `locations_nonempty`: whether Locate must return at least one path;
- `min_results`: minimum candidate count;
- `action_target_kind`: structured option/subcommand/positional/operation target kind;
- `action_target_command`: parent command bound to that syntax target;
- `action_target_terms_all`: capability terms that must survive structured parsing;
- `actionable_present`: whether a source-backed actionable answer must exist;
- `actionable_command`: command owned by the actionable answer;
- `actionable_option_any`: at least one accepted verified option spelling;
- `actionable_subcommand_any`: at least one accepted verified subcommand;
- `actionable_safety`: exact evidence-driven safety label (`ReadOnly`, `Mutating`, `Destructive`, `Privileged`, `Network`, or `Unknown`);
- `invocation_present`: whether a structured invocation must exist;
- `invocation_complete`: whether the proven invocation has every required bound value;
- `invocation_command`: exact command owning the invocation;
- `invocation_arguments`: exact ordered argument projection;
- `invocation_arguments_all`: required argument values/tokens that must all occur in the projection;
- `invocation_placeholder_count`: number of structured placeholder segments.

The corpus deliberately avoids exact score assertions. Ranking weights are
implementation details; the benchmark measures user-visible behavior.

## Metrics

The report includes:

- query-level pass rate;
- assertion-level pass rate;
- top-1 accuracy;
- top-3 hit rate;
- query-frame accuracy;
- ambiguity-state accuracy;
- end-to-end CLI latency p50/p95/max;
- internal SearchEngine and per-stage p50/p95/max when profiling is enabled.

Report schema 3 keeps declared assertions in the denominator even when a query
times out or errors. For a non-timeout failed case, the harness automatically
performs one untimed `--explain-ranking` forensic rerun and records the top
candidates with semantic fit/tier, final utility, sources, and semantic
adjustment IDs. This makes ranking regressions diagnosable from the standard
Markdown artifact without requiring a separate manual command.

Latency includes process startup and live knowledge-source work. The harness
performs one discarded warm-up query by default so a stale index can refresh
before timed corpus cases. Use `--no-warmup` when measuring cold behavior.

## M11 research probes

`m11_mantext_probe.py` is a non-runtime M11 experiment that measures whether full local
section 1/8 manual text contains enough lexical evidence to recover difficult
`corpus-m11-research-v1.json` cases. It does not execute candidate commands, fetch
network data, or modify Acclorite's production index. Use its result to decide whether
richer source-backed capability indexing deserves implementation before evaluating a
local embedding dependency.

`m11_lsa_probe.py` is Experiment B. It keeps Experiment A's local full-man evidence surface but replaces
BM25 ranking with a research-only local Latent Semantic Analysis representation (TF-IDF + TruncatedSVD +
cosine similarity). This tests whether semantic structure learned entirely from local documentation can bridge
lexically distant requests before Acclorite evaluates a downloaded/pretrained embedding model. It requires
`scikit-learn` for the research run only; Acclorite's runtime/build dependency set is unchanged.

`corpus-m11-heldout-v1.json` is the pre-Experiment-B held-out paraphrase set. It covers the same capability
families as `corpus-m11-research-v1.json` using materially different wording. Run both corpora in the same LSA
fit so index cost is paid once:

```bash
python3 benchmarks/m11_lsa_probe.py \
  --corpus benchmarks/corpus-m11-research-v1.json \
  --corpus benchmarks/corpus-m11-heldout-v1.json \
  --json-out benchmark-m11-lsa-probe.json
```

### M11 Experiment C — pretrained local embeddings

`m11_embedding_probe.py` evaluates a compact pretrained sentence-embedding model over the same static local
section 1/8 man-page evidence used by Experiments A/B. It cleans roff conservatively, token-chunks manuals below
the model sequence limit, embeds those passages, and ranks each command by its best-matching passage. Candidate
commands are never executed and Acclorite's production index/runtime remains untouched.

The default research model is `sentence-transformers/all-MiniLM-L6-v2`. The model is **not** an Acclorite runtime
dependency proposal. Network access is disabled by default; if the model is not already cached, the first research
run must opt in explicitly with `--allow-download`. Subsequent runs can omit that flag and remain offline.

Run both the original and pre-existing held-out corpus together:

```bash
python3 benchmarks/m11_embedding_probe.py \
  --corpus benchmarks/corpus-m11-research-v1.json \
  --corpus benchmarks/corpus-m11-heldout-v1.json \
  --allow-download \
  --json-out benchmark-m11-embedding-probe.json
```

Do not interpret a quality win as permission to ship PyTorch/SentenceTransformers. If the pretrained semantic
representation wins materially, the next research step is a production-footprint experiment (precomputed index,
model/runtime size, warm query latency, offline packaging, and likely ONNX or another bounded inference path).

### M11 Experiment D — compact pretrained semantic index

Experiment C proved that pretrained semantic retrieval materially improves command recall, but its full-man passage
index is intentionally too expensive for production: 103,010 chunks, ~914 MiB peak RSS, and ~91 minutes of cold CPU
embedding on the reference CachyOS run. `m11_compact_embedding_probe.py` keeps the same model/evidence boundary while
selecting a small **query-blind** subset of source-backed passages per command before embedding.

The selector always retains the command's first identity/description passage, then greedily adds passages using only
static document properties: command-level term rarity, lexical novelty, and positional coverage. Benchmark queries and
expected commands are never consulted during selection.

The default run embeds at most four passages per command and evaluates 1-, 2-, and 4-passage budgets from that single
embedding pass. It may also persist the selected research embeddings for a later hybrid-reranking experiment.

```bash
.venv-m11/bin/python benchmarks/m11_compact_embedding_probe.py \
  --corpus benchmarks/corpus-m11-research-v1.json \
  --corpus benchmarks/corpus-m11-heldout-v1.json \
  --json-out benchmark-m11-compact-embedding-probe.json \
  --index-out benchmark-m11-compact-index.npz
```

The model should already be cached from Experiment C, so network access remains disabled by default. Do not pass
`--allow-download` unless intentionally repeating this experiment on a machine without the cached research model.

Interpretation target: preserve most of Experiment C's Top-10 recall while cutting semantic passages by roughly an
order of magnitude. If a compact budget succeeds, embeddings remain a candidate generator and the saved index can be
used by the next experiment to test deterministic/source-aware reranking without repeating the full embedding build.

### M11 Experiment E — reuse compact index + deterministic rank fusion

Experiment D retained most of Experiment C's deep semantic recall with a tiny query-blind evidence budget. Experiment E
asks whether that semantic candidate list and Acclorite's existing deterministic Top-10 contain complementary ranking
signal. It **does not rebuild or re-embed the document index**. It loads `benchmark-m11-compact-index.npz`, embeds only
the benchmark queries with the already-cached research model, runs the unchanged Acclorite CLI once per query, and
compares fixed scale-free Reciprocal Rank Fusion (RRF) strategies.

The probe reports deterministic-only, semantic-only, equal RRF, semantic-weighted RRF, and an oracle-like candidate
union coverage (`semantic@20 ∪ deterministic@10`). The union coverage is not a user-facing ranking; it measures whether
the two retrievers together put the expected command in the candidate pool.

```bash
.venv-m11/bin/python benchmarks/m11_hybrid_fusion_probe.py \
  --binary ./build/acclorite \
  --index benchmark-m11-compact-index.npz \
  --corpus benchmarks/corpus-m11-research-v1.json \
  --corpus benchmarks/corpus-m11-heldout-v1.json \
  --json-out benchmark-m11-hybrid-fusion.json
```

No model download should occur. The compact index and MiniLM model are reused from Experiments C/D. A useful fusion win
must reproduce on the sealed held-out corpus; do not choose RRF weights after reading individual benchmark failures and
then call the same held-out set untouched evidence.

### M11 Experiment F — pre-ranking semantic candidate injection

Experiment E showed that `semantic@20 ∪ deterministic@10` contains the expected command in 92.3% of research-v1 and
88.5% of heldout-v1 cases, while post-hoc RRF cannot rank that pool well enough. Experiment F therefore injects only
semantic **command identities** before Acclorite's normal deterministic ranking.

The embedding score is never passed to SearchEngine. A hinted command enters the pool only if an ordinary local source
independently substantiates that exact identity through the bounded `KnowledgeSource::inspect_commands(...)` seam.
Source-backed metadata is then scored against the original query and follows the unchanged merge/enrichment/preference
pipeline. Unsubstantiated hints are dropped; semantic retrieval is not provenance or syntax/safety authority.

Build the research runner with the normal test build, then run:

```bash
.venv-m11/bin/python benchmarks/m11_injection_probe.py \
  --runner ./build/acclorite_m11_injection_runner \
  --index benchmark-m11-compact-index.npz \
  --corpus benchmarks/corpus-m11-research-v1.json \
  --corpus benchmarks/corpus-m11-heldout-v1.json \
  --json-out benchmark-m11-injection-probe.json
```

The probe reports semantic-only, deterministic baseline, and injected deterministic Top-1/3/10 plus diagnostics for
semantic hits retained after deterministic inspection/ranking, new Top-3/Top-10 recoveries, and any baseline Top-10
regressions. The runner is benchmark-only and is not installed as part of Acclorite.


### M11 Experiment G — bounded ranked semantic support

Experiment F proved that identity-only pre-ranking injection is insufficient: semantic retrieval often finds the
correct command, but Acclorite ranks the source-substantiated candidate exactly as if the semantic retriever had never
surfaced it. Experiment G therefore tests a narrowly bounded relevance signal. Semantic hints still require ordinary
source substantiation, but may carry a rank-derived semantic-fit floor capped at `0.88` (below the exact-name tier).
The floor is relevance only: it is not provenance, syntax, invocation, or safety authority, and ordinary specialization
penalties are applied after the floor.

Three policies are predeclared before results: uniform plausible (`0.70` for semantic ranks 1–20), conservative-ranked
(`0.84 / 0.80 / 0.74 / 0.70`), and tiered-ranked (`0.88 / 0.84 / 0.78 / 0.70`) across rank buckets
1, 2–3, 4–10, and 11–20. The default probe uses Experiment D's four-passage compact index.

`corpus-m11-transfer-v1.json` is sealed before Experiment G results and deliberately uses different command families
from research-v1/heldout-v1. Do not tune a policy against transfer-v1 and then reuse that corpus as validation.

```bash
.venv-m11/bin/python benchmarks/m11_ranked_injection_probe.py \
  --runner ./build/acclorite_m11_injection_runner \
  --index benchmark-m11-compact-index.npz \
  --corpus benchmarks/corpus-m11-research-v1.json \
  --corpus benchmarks/corpus-m11-heldout-v1.json \
  --corpus benchmarks/corpus-m11-transfer-v1.json \
  --json-out benchmark-m11-ranked-injection.json
```

A policy earns further architecture work only if it materially improves Top-3/Top-10 across corpora without significant
baseline regressions. A large research/heldout win with transfer failure is evidence of overfitting, not success.

### M11 Experiment H — confidence-gated semantic fallback

Experiment G established that bounded `conservative-ranked` semantic relevance produces large Top-3/Top-10 gains
with no Top-3/Top-10 regressions across research-v1, heldout-v1, and transfer-v1, but it can still displace a few
correct deterministic Top-1 answers. Experiment H asks whether Acclorite's existing observational confidence system
can act as the gate instead of inventing another ML score.

The probe runs the ordinary deterministic ranking first, then separately computes the conservative-ranked semantic
result. Three fixed gates decide which ranking would be exposed:

- `uncertain-state`: semantic support only when ambiguity state is not `clear`;
- `non-high-confidence`: semantic support only when top-candidate confidence is below the existing `0.75` high threshold;
- `uncertain-or-non-high`: union of those two conditions.

In addition to the three M11 corpora, the default run includes frozen core-v1, human-v1, and actionable-v5 as
regression guardrails. The semantic index is reused; no document passages are re-embedded.

```bash
.venv-m11/bin/python benchmarks/m11_confidence_gate_probe.py \
  --runner ./build/acclorite_m11_injection_runner \
  --index benchmark-m11-compact-index.npz \
  --json-out benchmark-m11-confidence-gate.json
```

### M11 Experiment I: cross-system agreement arbitration

After Experiment H showed that existing confidence states are not calibrated
correctness gates, Experiment I evaluates fixed cross-system agreement policies
without changing production SearchEngine behavior:

```bash
.venv-m11/bin/python benchmarks/m11_agreement_gate_probe.py \
  --runner ./build/acclorite_m11_injection_runner \
  --index benchmark-m11-compact-index.npz \
  --json-out benchmark-m11-agreement-gate.json
```

By default the probe evaluates the three M11 research corpora and all three
frozen M10 corpora. It reuses the four-passage compact index and Experiment G's
`conservative-ranked` semantic support, then compares `anchor-top20`,
`anchor-top10`, `rank-gap-5`, and `rank-gap-10` arbitration rules. These are
research-only gates; semantic retrieval remains non-authoritative for existence,
provenance, syntax, invocation construction, and safety.

### M11 semantic-assist acceptance analysis

Experiments H/I rejected automatic semantic Top-1 arbitration, and the first protected-primary merged-list analysis
showed that inserting semantic alternatives below rank 1 can still displace correct deterministic Top-3 results. The
accepted product shape therefore keeps the deterministic ranking completely unchanged and exposes semantic assist in a
separate alternatives channel.

Analyze that shape without rerunning MiniLM or Acclorite:

```bash
python3 benchmarks/m11_semantic_assist_acceptance.py \
  --ranked-report benchmark-m11-ranked-injection.json \
  --json-out benchmark-m11-semantic-assist.json
```

The analyzer removes candidates already present in deterministic Top-10 from the semantic-supported ordering and reports
assist rescue at depths 1/3/5 for deterministic Top-1, Top-3, and Top-10 misses. It consumes the existing Experiment G
JSON only and performs no model/index/search work. Because the channels are separate, deterministic ranking metrics are
not recomputed or modified by semantic assist.

### M11 Semantic Assist Runtime Qualification 1 — quantized ONNX query encoder

The semantic-retrieval research track accepted a separate semantic-assist channel and rejected automatic changes to
Acclorite's deterministic ranking. Runtime Qualification 1 therefore asks a production-oriented question: can a small
quantized ONNX encoder preserve the rescue value of the PyTorch research model **without rebuilding the compact passage
index**?

The probe reuses `benchmark-m11-compact-index.npz` and the authoritative Experiment G report. It encodes only the M11
benchmark queries with the selected ONNX artifact, retrieves semantic Top-20 command identities from the existing index,
passes those identities through the source-substantiated `conservative-ranked` research seam, and compares separate-channel
rescue coverage against the PyTorch reference.

The default candidate is the model repository's AVX2 uint8 artifact:

```text
onnx/model_quint8_avx2.onnx
```

ONNX networking is disabled by default. In the isolated M11 virtualenv, install the research backend once:

```bash
.venv-m11/bin/pip install -U "sentence-transformers[onnx]"
```

Then fetch the ONNX artifact intentionally on the first run:

```bash
.venv-m11/bin/python benchmarks/m11_semantic_runtime_probe.py \
  --runner ./build/acclorite_m11_injection_runner \
  --index benchmark-m11-compact-index.npz \
  --reference-report benchmark-m11-ranked-injection.json \
  --allow-download \
  --json-out benchmark-m11-semantic-runtime-onnx.json
```

Subsequent runs should omit `--allow-download` and remain offline.

The predeclared quality gate allows at most one lost PyTorch rescue at assist@3 and assist@5 for each corpus/miss family.
assist@1 is reported but is not a gate because Semantic Assist is a separate alternatives surface rather than an
ML-controlled primary ranking. The probe normalizes raw corpus expectations before rescue analysis and validates its
computed miss counts against Experiment G's stored baseline metrics; a mismatch is a hard analysis error rather than a
quality pass. A quality pass does **not** yet select the final native tokenizer/runtime packaging; it only qualifies
quantized ONNX embeddings as representation-compatible with the saved MiniLM index.

### M11 Semantic Assist Runtime Qualification 2 — native C++ ONNX encoder

After corrected RQ1 passes, install native research dependencies on Arch/CachyOS:

```bash
sudo pacman -S onnxruntime-cpu libutf8proc
```

Reconfigure and rebuild so CMake can discover `libonnxruntime.pc` and `libutf8proc.pc`:

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Then qualify the native encoder against the corrected RQ1 result:

```bash
.venv-m11/bin/python benchmarks/m11_native_runtime_probe.py \
  --native-runner ./build/acclorite_m11_native_onnx_encoder \
  --injection-runner ./build/acclorite_m11_injection_runner \
  --index benchmark-m11-compact-index.npz \
  --rq1-report benchmark-m11-semantic-runtime-onnx.json \
  --ranked-report benchmark-m11-ranked-injection.json \
  --json-out benchmark-m11-semantic-runtime-native.json
```

RQ2 performs no downloads. It locates the already-cached qint8 ONNX artifact and `vocab.txt`, requires exact tokenizer-ID
parity for benchmark plus fixed Unicode/punctuation texts, encodes benchmark queries with native ONNX Runtime C++, and
compares separate-channel rescue against corrected RQ1. The native runner is research-only and is not installed.
