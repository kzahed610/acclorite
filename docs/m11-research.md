> **Status — parked for release polish (2026-09-25):** M11 semantic-runtime integration is intentionally paused. The deterministic standalone CLI is the release focus. Research remains opt-in via `ACCLORITE_BUILD_RESEARCH=ON` and must not become a normal build/runtime dependency.

# Acclorite Milestone 11 — Deterministic Research Plan

## Status

Milestone 11 begins after the frozen Milestone-10 actionable-answer gate.

The deterministic engine remains authoritative. M11 is a measured research phase, not a promise to implement every candidate idea. A candidate is accepted only when it materially improves Acclorite without weakening local-first/offline behavior, source-backed syntax authority, non-execution, the stable machine interface, or the frozen M10 gates.

## Frozen baseline

The following gates are pre-M11 contracts and are not research targets:

- `corpus-v1.json`: 25/25 queries, 65/65 assertions on the real CachyOS reference machine;
- `corpus-human-v1.json`: 25/25 queries, 85/85 assertions on the real CachyOS reference machine;
- `corpus-actionable-v5.json`: 14/14 queries, 152/152 assertions on the real CachyOS reference machine;
- CTest: 6/6 suites passed at the M10 handoff.

M11 experiments must run in addition to these gates, never by weakening or rewriting them.

## Primary research question

> What measured limitation remains in deterministic Acclorite now that discovery, query framing, ranking, syntax proof, argument binding, invocation construction, provenance, static Fish completion parsing, and safety classification all work?

The first hypothesis to test is that the largest remaining intelligence gap is **semantic discovery/ranking when the user's wording is clear to a human but lexically distant from the relevant command and its short local descriptions**.

This is a hypothesis, not an implementation decision.

## First research corpus

`benchmarks/corpus-m11-research-v1.json` is the first exploratory M11 measurement surface.

It intentionally asks for common Linux operations without naming the expected command and emphasizes tasks such as:

- periodic command repetition;
- process-name to PID lookup;
- shared-library dependency inspection;
- content-based file identification;
- splitting files;
- column extraction and stdin fan-out;
- adjacent-line deduplication;
- epoch conversion;
- terminal-detached execution;
- binary/string/hex inspection;
- ownership and permission changes;
- filesystem/mount/block-device inspection;
- process/file relationships;
- content fingerprints.

The corpus is deliberately **not frozen** at M11 start. It is a research/failure corpus whose purpose is to expose current limitations. Once an M11 capability contract is accepted, create a separate versioned regression snapshot rather than silently turning the exploratory corpus into a moving acceptance oracle.

The real CachyOS machine is the authoritative Arch baseline. Results from other distributions are useful for diagnosis but are not an Arch regression oracle.

## Evaluation matrix

| Candidate | Exact problem | Expected gain | Cost / risk | Evidence required | Initial decision |
|---|---|---|---|---|---|
| Semantic retrieval / reranking | Clear human task is lexically distant from command name/summary, so the correct candidate is absent or buried | Potentially high retrieval gain on M11 research corpus | Model/dependency footprint, latency, memory, opaque similarity if poorly instrumented | `corpus-m11-research-v1`, cold/warm latency, RSS, ranking trace | **Evaluate first** |
| Smarter deterministic phrase learning | Repeated unseen paraphrases fail despite a small stable concept relation being learnable | Medium to high if failures cluster around reusable concepts | Lexicon creep, benchmark overfitting, maintenance burden | Failure clustering across multiple paraphrases per concept; held-out paraphrases | **Evaluate alongside semantic retrieval** |
| Richer Compare mode | Compare queries identify commands but provide shallow side-by-side capability reasoning | Medium product value | New comparison contract and UX complexity | Dedicated compare corpus with source-backed capability differences | Evaluate after retrieval question |
| Deeper shell-completion semantic indexing | Man evidence is weak while static completion metadata contains useful command/option semantics | Unknown | Parser complexity and shell-specific edge cases | Measured syntax/coverage failures where completion metadata would change an answer | Defer pending coverage evidence |
| Info syntax extraction | Useful syntax exists only in local Info pages | Unknown | Another parser/authority merge path | Concrete M10/M11 failures attributable to missing man/Fish syntax | Defer pending coverage evidence |
| Zsh/Bash/deeper Fish parsing | Existing static Fish subset misses important locally installed tools | Unknown | High parser surface; dynamic shell constructs are dangerous/noisy | Coverage audit proving significant missed static facts | Defer pending coverage evidence |
| Personalized inspectable ranking | Different users prefer different valid tools | Low until real preference conflicts appear | Persistent state, privacy, reproducibility, benchmark complexity | Repeated real-user preference cases where generic ranking is correct but personally undesirable | Defer |
| Interactive learning/exploration mode | Users want to browse related tools/capabilities rather than ask one query | UX gain, not yet core retrieval gain | UI/state complexity; can distract from core engine | Real usage need after core research | Defer |
| `libacclorite` | In-process clients need a stable API instead of `--json` | Integration gain only if a real client requires it | API/ABI maintenance and compatibility burden | Concrete embedding client with measurable reason subprocess JSON is insufficient | Defer |

## Experiment order

### Experiment A — establish the real M10 failure surface

Run the exploratory corpus on the real CachyOS tree without changing ranking, lexicon, or retrieval.

```bash
python3 benchmarks/run.py \
  --binary ./build/acclorite \
  --corpus benchmarks/corpus-m11-research-v1.json \
  --json-out benchmark-m11-research-v1-baseline.json \
  --markdown-out benchmark-m11-research-v1-baseline.md
```

Do **not** use `--strict` for the initial research baseline. Expected failures are the point.

For each failure, classify the cause before coding:

1. expected command was never retrieved;
2. expected command was retrieved but ranked too low;
3. local evidence for the command was too weak/placeholder-like;
4. query normalization/concept recovery lost the important relation;
5. a wrong candidate received misleading lexical or specialization evidence;
6. the benchmark expectation is genuinely ambiguous or wrong.

### Experiment B — compare deterministic phrase work against semantic reranking

Before adding an embedding dependency, use the failure clusters to define two competing prototypes:

- a bounded deterministic semantic/phrase improvement that is fully traceable;
- a local semantic retrieval/reranking experiment, isolated so it can be disabled and measured independently.

Neither prototype becomes production architecture merely because it improves one corpus.

### Experiment C — held-out paraphrases

Any approach that improves v1 must be tested against held-out paraphrases that were not used to design it. A feature that only memorizes benchmark wording fails M11 even if the benchmark score rises.

## Acceptance dimensions

Every candidate experiment is measured on separate axes:

### Correctness

- Top-1 accuracy;
- Top-3 hit rate;
- regressions on all frozen M10 corpora;
- false-positive semantic matches;
- behavior on ambiguous requests.

### Coverage

- number of previously failing semantic tasks recovered;
- whether gains generalize across paraphrases and command families;
- whether installed and repository-only candidates both benefit where applicable.

### Performance

Measure cold-ish and warm runs separately with repeated trials:

- end-to-end latency;
- retrieval/reranking stage latency;
- startup cost;
- peak/resident memory;
- index/model size if applicable.

Do not attribute one noisy run to an implementation change.

### Determinism and debuggability

An accepted feature must expose enough trace information to answer:

- why was this candidate retrieved?;
- why was it promoted/demoted?;
- which source or semantic relation contributed?;
- can the behavior be reproduced offline?;
- can the feature be disabled for A/B measurement?

A semantic score that cannot be inspected is not sufficient merely because it is local.

### Product-boundary safety

M11 must preserve:

- normal runtime remains local-first/offline;
- Acclorite does not execute constructed target commands;
- syntax/flags/subcommands remain source-backed rather than model-invented;
- package backends remain query-only/read-only within their established boundaries;
- Search schema 15 / Doctor schema 1 / capabilities schema 1 remain compatible unless an intentional future schema change is separately justified;
- Realmheart remains a future client, not a requirement for M11.

## Decision rule

A candidate earns production status only when its measured benefit is large enough to justify its complexity and costs.

A valid M11 outcome may be:

```text
evaluated 7 candidates
implemented 2
rejected 3
deferred 2
```

That is preferable to shipping seven clever subsystems with marginal value.

## Real CachyOS M11-v1 baseline — 2026-09-13

The first authoritative M11 research run on the real CachyOS reference machine produced:

```text
Query pass rate       3/26    11.5%
Assertion pass rate   7/52    13.5%
Top-1 accuracy        3/26    11.5%
Top-3 hit rate        4/26    15.4%
Latency               p50 141.3 ms · p95 220.5 ms · max 256.7 ms
Internal search       p50 137.4 ms · p95 216.5 ms · max 250.6 ms
```

The three Top-1 successes were `hexdump`, `chown`, and `lsof`. `split` was already rank 2 and `ln` rank 4, proving that M11 contains both ranking misses and larger recall/semantic-evidence misses.

The baseline also exposed a query-representation limitation: `meaningful_terms()` keeps only the first six surviving terms. In longer natural-language requests, later high-value words can disappear before concept scoring. Examples from this corpus include `watch`, `date`, `binary`, `owner`, `filesystem`, `tree`, `spawned`, `contents`, and `alphabetically`. A naive term-cap increase is not accepted as a solution because unmatched extra words also dilute semantic coverage.

### Experiment A — full local man-text lexical probe

Before adding embeddings or changing production ranking, measure how much failure comes from the current index's shallow evidence surface. M10 indexes command names plus short catalog/desktop summaries; several M11 tasks describe capabilities that live deeper in local manuals, especially option descriptions.

`benchmarks/m11_mantext_probe.py` is a research-only probe. It:

- reads local section 1/8 man source files directly;
- supports plain, gzip, bzip2, xz, and lzma sources;
- resolves simple local roff `.so` alias pages without executing `man`;
- performs no network access and executes no candidate command;
- builds an in-memory deterministic BM25 ranking from full manual text;
- reports the rank of the corpus-accepted command(s), plus indexing/evaluation timing;
- does not affect Acclorite runtime behavior, schemas, or frozen M10 gates.

Run it on the real reference machine:

```bash
python3 benchmarks/m11_mantext_probe.py \
  --corpus benchmarks/corpus-m11-research-v1.json \
  --json-out benchmark-m11-mantext-probe.json
```

Decision rule:

- **large Top-10/Top-3 recovery gain:** prioritize a bounded source-backed semantic/capability index before embeddings;
- **correct commands recovered but ranked weakly:** next experiment is semantic reranking over richer evidence;
- **little recovery even with full manuals:** lexical evidence is insufficient and local embeddings / another semantic representation earn a serious experiment;
- **large cost or unsupported-page gap:** measure/index only selected static sections (option/subcommand descriptions, Fish metadata) rather than full text.

Do not merge full-man indexing into production merely because the probe scores well. First measure rebuild cost, index size, warm query latency, and whether a bounded subset of the manual preserves the gain.

### Experiment A result — real CachyOS

The authoritative full-man lexical probe produced:

```text
Pages discovered      4164
Indexed commands      4060
Indexed tokens        7,608,937
Expected man coverage 26/26
Top-1 recovery         3/26   11.5%
Top-3 recovery         3/26   11.5%
Top-10 recovery       10/26   38.5%
Index time             6536.2 ms
Evaluation time         259.8 ms
Total                  6828.9 ms
```

Compared with the production M10 baseline (`3/26` Top-1, `4/26` Top-3), richer full-man lexical evidence materially improved **deep recall** but did not improve useful front-rank accuracy. The expected command reached Top-10 in ten cases, while sixteen cases remained below rank 10 or absent. Whole-man lexical indexing is therefore **rejected as the primary M11 retrieval solution**.

The experiment still established two useful facts:

1. local manuals contain capability evidence that the shallow production index does not expose;
2. lexical matching alone cannot reliably translate the user's wording into that evidence.

This makes semantic representation a measured problem rather than a speculative feature request.

### Experiment B — local latent semantics over the same evidence

Experiment B isolates **representation** while holding the evidence source constant. `benchmarks/m11_lsa_probe.py` reuses the exact tokenized local section 1/8 manual corpus from Experiment A, then builds a research-only Latent Semantic Analysis representation:

```text
local static man pages
      ↓
TF-IDF sparse matrix
      ↓
Truncated SVD (fixed random seed)
      ↓
normalized latent command vectors
      ↓
query projection + cosine ranking
```

This is intentionally **not** a production dependency proposal. It asks whether useful semantic structure can be learned from local documentation at all, before evaluating a downloaded/pretrained embedding model.

The probe requires `scikit-learn` only for the research run. Acclorite runtime/build does not depend on it, and the probe performs no network access or candidate-command execution.

A held-out corpus, `corpus-m11-heldout-v1.json`, is created **before reading Experiment B results**. It covers the same capability families with different wording. Do not tune Experiment B on held-out failures and then report that same corpus as untouched generalization evidence.

Run both corpora in one fit:

```bash
python3 benchmarks/m11_lsa_probe.py \
  --corpus benchmarks/corpus-m11-research-v1.json \
  --corpus benchmarks/corpus-m11-heldout-v1.json \
  --json-out benchmark-m11-lsa-probe.json
```

Decision rule:

- **large Top-3 gain on research-v1 and comparable held-out gain:** local latent semantics earns a bounded production-design experiment;
- **Top-10 rises but Top-3 remains weak:** semantic recall exists but needs a separate reranker/evidence-compression design;
- **research-v1 improves but held-out collapses:** reject/treat as overfitting or corpus-specific latent structure;
- **little gain on both:** local man-derived semantics are insufficient; a compact pretrained local embedding model finally earns an isolated comparison experiment;
- **quality gain is large but fit/RSS cost is unreasonable:** keep the semantic finding but redesign the representation/index lifecycle before production consideration.

Do not modify production ranking from Experiment B alone.

### Experiment B result — real CachyOS

The authoritative local-man LSA run produced:

```text
Evidence              4,164 pages / 4,060 commands / 7,608,937 tokens
Sparse features        50,000
Latent dimensions      128
Explained variance     61.5%
Peak RSS               574.8 MiB
Total probe time       22,002.2 ms

research-v1   Top-1  2/26   7.7% · Top-3  2/26   7.7% · Top-10 2/26   7.7%
heldout-v1    Top-1  1/26   3.8% · Top-3  3/26  11.5% · Top-10 5/26  19.2%
```

This is worse than both the production baseline at useful ranks and Experiment A's lexical deep recall. LSA over
full local manuals is therefore **rejected as the primary M11 semantic representation**. The failure is not a
zero-vector artifact: every query projected into the latent space. The approach consumed substantial fit time and
memory while failing to preserve obvious lexical wins such as `split` and `uniq` on research-v1.

The result answers the previous strategic question: semantics learned only from the machine's local manual corpus
are not sufficient in this classical latent representation. A compact pretrained local semantic model has now
earned an isolated experiment.

### Experiment C — compact pretrained semantic retrieval

Experiment C tests a pretrained sentence embedding representation while preserving the source/evidence boundary.
The model may decide that a query and a static manual passage are semantically related; it does **not** become an
authority for command syntax, options, package metadata, invocation construction, or safety.

`benchmarks/m11_embedding_probe.py` uses `sentence-transformers/all-MiniLM-L6-v2` by default. Long manuals are not
encoded as one silently truncated vector. Instead the probe:

1. reads the same static local section 1/8 man sources as Experiments A/B;
2. conservatively removes common roff presentation markup without executing `man` or candidate commands;
3. chunks each page below the embedding model's configured sequence limit with overlap;
4. embeds each passage locally and L2-normalizes vectors;
5. embeds benchmark queries with the same model;
6. scores a command by its highest-scoring source passage;
7. reports Top-1 / Top-3 / Top-10 recovery, source/snippet provenance, model/index cost, and peak RSS.

The pre-Experiment-B `corpus-m11-heldout-v1.json` remains untouched and is evaluated simultaneously. Do not modify
it in response to Experiment C failures.

The model dependency is **research-only**. Normal Acclorite still has no SentenceTransformers/PyTorch dependency.
The probe disables network access by default. A one-time model fetch requires explicit `--allow-download`, after
which cached runs can be offline.

Run:

```bash
python3 benchmarks/m11_embedding_probe.py \
  --corpus benchmarks/corpus-m11-research-v1.json \
  --corpus benchmarks/corpus-m11-heldout-v1.json \
  --allow-download \
  --json-out benchmark-m11-embedding-probe.json
```

Decision rule:

- **large Top-3 gain on both corpora:** pretrained semantic retrieval earns a production-footprint/design experiment;
- **strong Top-10 but weak Top-3:** use embeddings as bounded candidate recall and research a deterministic/source-aware reranker;
- **research-v1 gain without comparable held-out gain:** reject as insufficiently general;
- **little gain on both:** pretrained embeddings do not justify their dependency/model cost for this problem;
- **quality is strong but cold index/model cost is excessive:** preserve the semantic finding, but test precomputed embeddings, bounded evidence selection, and a smaller inference/runtime format before any production proposal.

A successful Experiment C still does **not** authorize a runtime dependency. It only establishes whether pretrained
semantic information solves a measured retrieval problem strongly enough to justify the next engineering experiment.

### Experiment C result — real CachyOS

The authoritative pretrained-passage run used `sentence-transformers/all-MiniLM-L6-v2` over 103,010 source-backed
chunks from 4,164 local section 1/8 pages (4,060 commands):

```text
Embedding dimensions   384
Peak RSS               914.2 MiB
Cold probe total       5,478,719.7 ms (~91.3 min)
Chunk embedding        5,191,836.6 ms (~86.5 min)

research-v1   Top-1 11/26 42.3% · Top-3 19/26 73.1% · Top-10 22/26 84.6%
heldout-v1    Top-1  4/26 15.4% · Top-3 15/26 57.7% · Top-10 20/26 76.9%
```

This is the first M11 representation to show a large improvement on both the research corpus and the sealed held-out
paraphrases. Pretrained semantic information therefore **earns further engineering research**. Experiment C does not,
however, authorize the current implementation as a production architecture: its full-man passage index is far too
expensive in cold build time and memory.

The rank shape is also important. Held-out Top-1 remains only 15.4% while Top-3/Top-10 reach 57.7%/76.9%. Treat the
embedding subsystem as a promising **candidate-recall mechanism**, not as final command authority or an independent
replacement for deterministic ranking, syntax proof, provenance, or safety.

Observed residual failures also support that interpretation. Some correct tools are near the top but not first
(`find`, `pidof`/`pgrep`, `split`, `chmod`, `ln`, `sort`, `pstree`), while a smaller set remains outside Top-10
(`tail`, `strings`, content fingerprints, and several device/filesystem cases). A later reranker must not be designed
until the semantic index footprint itself is bounded.

### Experiment D — compact query-blind semantic index

Experiment D asks whether most of Experiment C's candidate recall can survive a radically smaller source-backed
semantic index.

`benchmarks/m11_compact_embedding_probe.py` first creates the same bounded man-text chunks as Experiment C but does
**not** embed them all. For each command it deterministically selects a small passage budget without seeing any query:

1. always retain the first identity/description passage;
2. estimate command-level inverse document frequency for static lexical salience;
3. prefer capability-specific passages with rare terms;
4. reward lexical novelty relative to already-selected passages;
5. reward positional coverage so long manuals do not collapse onto one local region.

The default maximum is four passages per command. One embedding pass then evaluates nested budgets of 1, 2, and 4
passages per command. Because selection is query-blind and corpus-blind, the experiment cannot cherry-pick passages
for the benchmark.

Success criterion:

- retain a large majority of Experiment C Top-10 recall on **both** corpora;
- materially reduce the number of embedded passages and cold embedding time;
- preserve source/snippet provenance;
- do not weaken the frozen M10 gates or introduce a production dependency.

If four or fewer passages retain useful candidate recall, persist the compact research index and move to a separate
hybrid-reranking experiment. If quality collapses, increase/alter the query-blind evidence budget before discussing a
production embedding runtime. Do not solve compact-index failures by consulting benchmark queries during indexing.

### Experiment D result — real CachyOS

The compact query-blind index retained most of Experiment C's recall while reducing the embedded evidence surface from
103,010 passages to 14,392 passages at the maximum four-passage budget (14.0% of the full C index):

```text
                                  research-v1                 heldout-v1
1 passage/command   Top-1 50.0% · Top-3 53.8% · Top-10 84.6%   Top-1 34.6% · Top-3 42.3% · Top-10 69.2%
2 passages/command  Top-1 46.2% · Top-3 65.4% · Top-10 80.8%   Top-1 38.5% · Top-3 50.0% · Top-10 73.1%
4 passages/command  Top-1 38.5% · Top-3 53.8% · Top-10 84.6%   Top-1 34.6% · Top-3 53.8% · Top-10 76.9%
```

The four-passage budget exactly preserved Experiment C's held-out Top-10 recovery while using only 14% as many passage
vectors. The single-passage budget preserved research-v1 Top-10 exactly with only 4,060 vectors, showing that the first
identity/description passage carries surprisingly strong semantic recall. Two passages gave the best overall Top-3
tradeoff among the compact budgets.

Cold research indexing is still not a production path: the four-passage run took about 512 seconds total, including
~444 seconds of passage embedding, and peaked at ~1.35 GiB RSS. In contrast, embedding all 52 benchmark queries took
about 204 ms. This sharply separates **offline index construction cost** from **online query cost** and makes the saved
compact NPZ suitable for the next experiment.

### Experiment E — semantic candidates + deterministic rank fusion

Experiment E tests the architecture suggested by C/D without changing SearchEngine behavior yet:

```text
human query
   ├── compact pretrained semantic retriever ──→ semantic Top-20
   └── existing deterministic Acclorite ───────→ deterministic Top-10
                                      ↓
                             fixed rank fusion
                                      ↓
                                  Top-10
```

`benchmarks/m11_hybrid_fusion_probe.py` reuses Experiment D's saved `benchmark-m11-compact-index.npz`; document
passages are **not** re-embedded. Only the benchmark queries are embedded. The unchanged Acclorite binary supplies its
normal deterministic Top-10. Because raw semantic cosine and Acclorite utility scores are not calibrated to the same
scale, the first fusion experiment uses inspectable Reciprocal Rank Fusion rather than an arbitrary numeric blend.

Predeclared strategies:

- semantic-only;
- deterministic-only;
- equal-weight RRF (`k=60`);
- semantic-heavy RRF (2:1 semantic:deterministic, `k=60`);
- candidate-union coverage for `semantic@20 ∪ deterministic@10` (coverage diagnostic, not a ranking).

Decision rule:

- **held-out Top-3 improves materially over semantic-only:** simple scale-free fusion earns a production-design path;
- **candidate union is strong but both fixed RRF strategies fail:** the retrievers are complementary, but final-list
  fusion is too crude; next experiment should inject semantic candidates before deterministic/source-aware ranking;
- **union adds little over semantic-only:** the existing deterministic retriever contributes little on this failure
  surface; prioritize semantic candidate quality/footprint rather than fusion;
- **research improves but held-out regresses:** reject the fusion rule rather than tuning it against the held-out cases.

Experiment E remains research-only. It does not alter frozen M10 gates, Search schema 15, command syntax authority,
provenance, invocation construction, safety classification, or the non-execution boundary.

### Experiment E result — real CachyOS

The saved compact index made the fusion experiment cheap enough to isolate ranking behavior. Across 52 queries the
probe completed in 15.6 seconds total; model load was ~5.4 seconds, query embedding ~280 ms, and the unchanged
Acclorite CLI spent ~9.4 seconds on the 52 deterministic searches.

The strongest finding was not an RRF score but candidate-pool coverage. At the four-passage budget:

```text
research-v1   expected in semantic@20 ∪ deterministic@10   24/26   92.3%
heldout-v1    expected in semantic@20 ∪ deterministic@10   23/26   88.5%
```

Fixed RRF could not convert that pool quality into comparable Top-3 quality. The best fixed strategy reached only
65.4% Top-3 on research-v1 and 57.7% on heldout-v1. Therefore **post-hoc fusion is rejected as the architecture**.
The two retrievers contain complementary signal, but combining two already-final rankings is too late.

This result motivates pre-ranking candidate injection: semantic retrieval should propose bounded command identities,
then ordinary Acclorite sources must independently substantiate those identities before the existing deterministic
merge/enrichment/ranking pipeline decides their order.

### Experiment F — pre-ranking semantic candidate injection

Experiment F is the first integration experiment. It does **not** give embedding scores to SearchEngine and does not
add a semantic provenance source. The semantic layer supplies only command-name hints.

SearchEngine now has a bounded candidate-hint seam used only by the research runner. For every hinted identity:

1. the hint is validated and deduplicated (maximum 32 names);
2. each active `KnowledgeSource` may perform bounded exact inspection through `inspect_commands(...)`;
3. a candidate is admitted only when a normal source independently returns the exact hinted command;
4. source-backed text is scored against the original query by the existing deterministic relevance machinery;
5. the candidate enters the ordinary merge, prefilter, enrichment, preference, and final sort path;
6. no embedding score, semantic rank, or `semantic` provenance token enters the candidate model.

The persistent `IndexSource` performs exact SQLite inspection in one connection/prepared statement. The no-index
fallback has bounded PATH/man inspection implementations. Package backends are not granted semantic authority;
on Arch, ordinary `pkgfile` enrichment can still enrich an admitted local candidate if it reaches the existing
bounded enrichment window.

The benchmark-only `acclorite_m11_injection_runner` mirrors the normal candidate-source/package setup but omits
post-ranking guidance/syntax work because Experiment F measures ranking only. It is built only with the test/research
configuration and is not installed.

`benchmarks/m11_injection_probe.py` reuses `benchmark-m11-compact-index.npz`, embeds only the 52 benchmark queries,
retrieves semantic Top-20 identities, and compares the deterministic baseline with pre-ranking injection at the
1-, 2-, and 4-passage budgets.

Run:

```bash
.venv-m11/bin/python benchmarks/m11_injection_probe.py \
  --runner ./build/acclorite_m11_injection_runner \
  --index benchmark-m11-compact-index.npz \
  --corpus benchmarks/corpus-m11-research-v1.json \
  --corpus benchmarks/corpus-m11-heldout-v1.json \
  --json-out benchmark-m11-injection-probe.json
```

Decision rule:

- **large held-out Top-3/Top-10 gain with few/no regressions:** semantic identity recall + deterministic ranking earns
  a production-architecture/footprint experiment;
- **semantic hits are present but frequently disappear from injected Top-10:** current deterministic scoring cannot
  exploit the newly recovered source-backed candidates; the next experiment should improve deterministic capability
  evidence/reranking rather than increase embedding depth;
- **injected Top-10 improves but Top-1/Top-3 remain weak:** candidate recall is solved more than ordering; research a
  bounded source-aware reranker inside the deterministic engine, still without trusting embedding scores;
- **held-out gains are marginal:** do not integrate ML merely because Experiment C retrieved good candidates; reject or
  redesign the semantic role;
- **baseline hits regress:** treat that as a blocker. Semantic candidate expansion must not degrade established
  deterministic answers just by enlarging the pool.

Frozen M10 corpora remain authoritative and must still pass unchanged after this research seam is applied.

### Experiment F result — real CachyOS

Identity-only pre-ranking injection failed to improve ranking despite excellent semantic candidate recall.
At the four-passage compact budget, the semantic retriever contained an accepted command in 24/26 research-v1 cases
and 23/26 heldout-v1 cases, but the injected deterministic ranking was **identical to baseline** on Top-1/3/10:

```text
research-v1   baseline/injected Top-1 3/26 · Top-3 4/26 · Top-10 8/26
heldout-v1    baseline/injected Top-1 4/26 · Top-3 6/26 · Top-10 8/26
```

No new Top-3 or Top-10 recoveries occurred and no baseline Top-10 regressions occurred. This is a clean negative result:
source-backed identity injection solves admission but not ordering. The existing ranker receives no information that a
lexically distant candidate was independently surfaced by a strong semantic retriever, so its ordinary semantic-fit
tier remains weak and the candidate loses before recommendation utility can matter.

Therefore Experiment F's exact architecture is rejected. The next experiment must test whether a semantic retrieval hit
may contribute a **bounded relevance signal** after source substantiation without becoming provenance, syntax, safety,
or exact-name authority.

### Experiment G — bounded source-substantiated semantic relevance

Experiment G keeps F's trust boundary and changes only one variable: a validated semantic hint may carry a bounded
query-relative relevance floor after an ordinary `KnowledgeSource` independently substantiates the exact command.

The floor:

- is capped at `0.88`, so it can never manufacture the exact-name tier (`>= 0.97`);
- is not appended to candidate provenance;
- does not prove command syntax, options, subcommands, argument shapes, invocation completeness, or safety;
- is applied before the existing query-relative specialization penalties, so an unrequested narrow-domain candidate
  can still be demoted normally;
- is absent from all ordinary searches, preserving frozen M10 behavior when no research hints are supplied.

Experiment G deliberately uses **semantic rank buckets rather than raw cosine scores**. Cosine values are model- and
index-dependent and are not calibrated to Acclorite's deterministic score scale. The following schedules are fixed
before results:

```text
plausible-uniform
  semantic ranks 1-20 → 0.70

conservative-ranked
  rank 1      → 0.84
  ranks 2-3   → 0.80
  ranks 4-10  → 0.74
  ranks 11-20 → 0.70

tiered-ranked
  rank 1      → 0.88
  ranks 2-3   → 0.84
  ranks 4-10  → 0.78
  ranks 11-20 → 0.70
```

The default quality probe uses the four-passage compact index because Experiment E/F established that budget as the
highest candidate-union ceiling. It evaluates only this budget so the experiment measures ranking policy rather than
repeating the compact-index search.

A new `acclorite-m11-transfer-v1` corpus is sealed **before Experiment G results**. It contains 26 different capability
families (`realpath`, `readlink`, `stat`, `mktemp`, `timeout`, `nice`, `renice`, `taskset`, `flock`, `lsns`, `nsenter`,
`unshare`, `uptime`, `free`, `vmstat`, `tr`, `paste`, `join`, `tee`, `nl`, `shuf`, `seq`, `tac`, `fold`, `id`, and
`printenv`/`env`). This prevents repeatedly inspected research/heldout command families from being the only evidence
used to choose a relevance policy.

Decision rule:

- **material Top-3/Top-10 gain on research, heldout, and transfer with few/no baseline regressions:** bounded semantic
  relevance earns a production-design experiment;
- **research/heldout improve but transfer does not:** reject the selected schedule as overfit and do not tune against
  transfer-v1;
- **only aggressive schedules improve while causing regressions:** semantic ranking support is too authoritative;
  redesign the deterministic reranking/evidence representation instead;
- **no schedule helps:** reject rank-derived floors and revisit deterministic capability evidence rather than increasing
  ML authority.


### Experiment G result — real CachyOS

Bounded source-substantiated semantic relevance succeeded on all three M11 corpora. The fixed
`conservative-ranked` schedule was the best current quality/safety tradeoff:

```text
research-v1   Top-1  9/26 · Top-3 16/26 · Top-10 21/26
heldout-v1    Top-1  8/26 · Top-3 15/26 · Top-10 21/26
transfer-v1   Top-1 15/26 · Top-3 20/26 · Top-10 24/26
```

Relative to deterministic baseline it introduced only Top-1 regressions (3 research, 1 heldout, 2 transfer) and
**zero Top-3 or Top-10 regressions on every corpus**. The weaker `plausible-uniform` schedule caused no regressions
at all but left substantial recovery on the table. The stronger `tiered-ranked` schedule did not materially improve
the broad result and began causing Top-3 regressions, so it is rejected as unnecessarily authoritative.

Therefore pretrained semantic relevance has earned continued integration research, with `conservative-ranked` as the
current ceiling policy. The remaining problem is protecting already-good deterministic Top-1 answers without giving up
the large gains on lexically distant requests.

### Experiment H — confidence-gated semantic fallback

Experiment H changes no ranking formula. Instead it tests whether Acclorite's existing confidence assessment can decide
*when* the conservative semantic-supported ranking is allowed to replace baseline. This treats ML as a fallback path
rather than a permanently co-equal ranking authority.

For each query the research harness computes:

1. ordinary deterministic ranking + existing `Confidence`;
2. Experiment G `conservative-ranked` semantic-supported ranking;
3. a final exposed ranking selected by a fixed confidence gate.

Predeclared gates, chosen before Experiment H results:

```text
uncertain-state
  use semantic support iff ambiguity != clear

non-high-confidence
  use semantic support iff top_candidate < 0.75
  (0.75 is the existing product threshold for confidence_level_name("high"))

uncertain-or-non-high
  use semantic support iff either condition above is true
```

The probe also reports baseline correctness by confidence state so the confidence system itself is evaluated rather
than assumed to be calibrated. As regression guardrails, the default Experiment H run includes the frozen M10
`core-v1`, `human-v1`, and `actionable-v5` corpora in addition to all three M11 corpora. Frozen expectations are not
modified.

Decision rule:

- **large M11 gains + zero frozen-corpus regressions + fewer/no M11 Top-1 regressions:** confidence-gated semantic
  fallback earns the production-design path;
- **confidence gate removes most gains:** confidence is too conservative for this role; do not weaken it just to rescue
  ML, revisit deterministic reranking/guardrails instead;
- **clear/high-confidence baseline is frequently wrong on M11:** confidence itself has a measured blind spot and must be
  treated as a separate deterministic research problem;
- **frozen M10 regressions occur even under gating:** semantic integration is not safe enough to productize yet.

Experiment H remains research-only. It does not change Search schema 15, syntax authority, command construction,
safety classification, provenance, or the non-execution boundary.

## Experiment H result — confidence gating rejected as the production gate

Real CachyOS evaluation showed that the existing ambiguity/confidence state is
not a calibrated probability that Top-1 is correct. This is expected in
retrospect: it measures semantic fit/separation and interpretation quality.

The frozen M10 corpora exposed the mismatch directly:

- `acclorite-core-arch-v1`: baseline 22/22 Top-1, but confidence gating still
  activated on correct competitive/low-confidence cases and dropped Top-1 to
  19/22;
- `acclorite-human-language-v1`: baseline 25/25 Top-1, but gating dropped Top-1
  to 19/25 and Top-3 to 23/25;
- `acclorite-actionable-v5`: remained 14/14 because all 14 cases were already
  classified clear and no fallback activated.

On the M11 hard corpora the gates retained most Experiment G gains, but they did
not remove G's Top-1 regressions because almost every difficult query is already
low-confidence. Therefore **confidence-gated semantic fallback is rejected as
the production arbitration rule**. Do not retune the 0.75 threshold against
these corpora; the gating variable itself answers a different question.

## Experiment I — cross-system agreement arbitration

Experiment I changes no production ranking behavior. It tests whether the
relationship between the deterministic Top-1 and semantic Top-20 can arbitrate
when the already-validated `conservative-ranked` support should be exposed.

Predeclared policies:

- `anchor-top20`: preserve deterministic Top-1 whenever it appears in
  semantic@20; otherwise expose the conservative-supported ranking;
- `anchor-top10`: same with semantic@10;
- `rank-gap-5`: expose the supported ranking only when its Top-1 outranks the
  deterministic Top-1 by at least five semantic positions;
- `rank-gap-10`: same, requiring a ten-position advantage.

The policies use semantic rank only, not raw cosine thresholds, and are evaluated
against all three M11 corpora plus all three frozen M10 gates. The purpose is to
learn whether cross-system disagreement is a safer arbitration signal than the
legacy confidence state. No policy is a winner until it preserves frozen M10
behavior while retaining material M11 gain.

## Experiment I result — automatic arbitration rejected

Cross-system agreement was safer than confidence gating, but it still failed the frozen-corpus bar for automatic
Top-1 replacement. `anchor-top20` was the strongest safety-oriented policy:

```text
M11 research-v1   Top-1 11/26 · Top-3 14/26 · Top-10 17/26 · zero regressions
M11 heldout-v1    Top-1  8/26 · Top-3 13/26 · Top-10 18/26 · zero regressions
M11 transfer-v1   Top-1 14/26 · Top-3 18/26 · Top-10 21/26 · zero regressions
```

It fully preserved `acclorite-core-arch-v1` and `acclorite-actionable-v5`, but still regressed the frozen
human-language corpus from 25/25 Top-1 to 22/25 and from 25/25 Top-3 to 23/25. The stricter/looser semantic-rank-gap
policies also regressed frozen M10 answers. Therefore **semantic/deterministic disagreement is not sufficient evidence
for automatic replacement of a deterministic Top-1**.

Do not continue stacking confidence/rank/cosine thresholds. Experiments H and I together show that automatic arbitration
needs a qualitatively better deterministic relevance model if it is revisited later, not another gate tuned against the
same corpora.

## M11 semantic integration decision

The semantic research question is now sufficiently answered to choose a product boundary:

### Accepted

- A compact pretrained local embedding model provides large, generalizing recall gains on lexically distant requests.
- Semantic retrieval may propose command identities.
- Proposed identities must be independently substantiated by ordinary Acclorite sources before they are user-visible.
- Semantic retrieval is useful as a **secondary recall/alternative surface**.
- Semantic ranking data may be exposed diagnostically as retrieval relevance, but it is not provenance, syntax authority,
  invocation authority, package authority, or safety evidence.

### Rejected for the current architecture

- full-man lexical retrieval as the primary solution;
- local-man LSA;
- identity-only candidate injection as sufficient ranking integration;
- unconditional semantic relevance overriding deterministic Top-1;
- confidence-gated semantic takeover;
- cross-system rank-agreement takeover;
- further threshold stacking against the existing M11 corpora.

### Protected-primary merged list — rejected

The first semantic-assist acceptance analysis pinned deterministic Top-1 but inserted semantic-supported candidates into
ranks below it. Real CachyOS evaluation exposed a remaining regression: `numeric-line-sort-heldout`, which had been a
baseline Top-3 hit, was pushed out of Top-3 by inserted semantic alternatives. Therefore pinning only the primary result
is **not** enough to preserve the deterministic ranking contract. A merged list is still a reranker even when rank 1 is
protected.

Do not claim that a protected primary guarantees Top-3/Top-10 safety. It guarantees only Top-1 safety.

### Current safe product candidate — separate semantic-assist channel

Preserve the complete deterministic Acclorite ranking byte-for-byte. Source-substantiated semantic-supported candidates
may appear only in a **separate, clearly labeled alternatives channel** and must not occupy deterministic rank positions.
Candidates already visible in deterministic Top-10 are omitted from that channel so it represents genuine recall
expansion rather than duplicate presentation.

This creates a strict product boundary:

- deterministic Top-1/Top-3/Top-10 cannot regress because semantic assist does not modify that ranking;
- semantic assist remains useful for surfacing lexically distant commands that deterministic discovery missed;
- semantic alternatives are source-substantiated before presentation;
- semantic relevance remains non-authoritative for provenance, syntax, invocation construction, package facts, and safety;
- automatic semantic replacement of deterministic Top-1 remains rejected unless a future deterministic capability
  reranker provides qualitatively stronger arbitration evidence.

`benchmarks/m11_semantic_assist_acceptance.py` now evaluates **rescue coverage**, not a merged ranking. It consumes the
already-generated Experiment G JSON, leaves deterministic rankings untouched, derives a separate semantic-alternatives
list, and reports how often that channel rescues deterministic Top-1/Top-3/Top-10 misses at assist depths 1/3/5. It
performs no model load, embedding work, or Acclorite searches.

## Semantic retrieval research closure — real CachyOS

The final separate-channel acceptance analysis closes the question of **whether pretrained semantic retrieval provides
useful incremental recall without weakening deterministic Acclorite**.

With the deterministic Top-10 left unchanged and semantic alternatives deduplicated against it, real CachyOS measured:

```text
research-v1 Top-10 misses: 18 · rescued assist@1 8 · assist@3 10 · assist@5 11  (61.1% @5)
heldout-v1  Top-10 misses: 18 · rescued assist@1 4 · assist@3  8 · assist@5  9  (50.0% @5)
transfer-v1 Top-10 misses: 14 · rescued assist@1 8 · assist@3 10 · assist@5 10  (71.4% @5)
```

Combined discovery with unchanged deterministic Top-10 plus assist@5 reaches 19/26, 17/26, and 22/26 respectively.
Because Semantic Assist occupies a separate channel, deterministic Top-1/Top-3/Top-10 cannot regress by construction.

**Decision:** pretrained semantic retrieval has earned an optional product role as a source-substantiated recall assistant.
The retrieval/arbitration research track is closed. Do not continue A/B/C-style ranking experiments unless a new real-user
failure invalidates this product boundary.

Production work now lives in `docs/m11-semantic-assist-production.md`. The first task is Runtime Qualification 1: determine
whether a quantized ONNX query encoder preserves the accepted rescue behavior while reusing the existing compact index.

## Runtime Qualification 1 closure and Runtime Qualification 2

Corrected real-CachyOS RQ1 passed for `onnx/model_quint8_avx2.onnx`. Research-v1 and heldout-v1 separate-channel rescue
matched the PyTorch reference exactly; transfer-v1 improved by one rescue at assist@1/@3/@5. Quantized ONNX is therefore
accepted as representation-compatible with the compact semantic index.

The next production-engineering step is RQ2, a native C++ ONNX query encoder with exact tokenizer-ID parity and the same
separate-channel rescue gate. This is not a reopening of semantic ranking research. Deterministic Acclorite remains the
canonical ranking and Semantic Assist remains a separate source-substantiated recall channel.
