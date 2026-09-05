# Acclorite

Acclorite is a local-first Linux tool-discovery assistant.

> I know what I want to do — just tell me what Linux tool I should be looking at.

Acclorite **v0.2.0** is entirely deterministic: **no LLM is involved.**

Acclorite now also recognizes broad query frames such as Discover, Explain, Locate, Inspect, Modify, Compare, and Diagnose. Frame recognition is weighted and whole-query-aware rather than being a brittle `what`/`where` prefix parser.

On Arch/pacman systems, Acclorite can also search synchronized repository metadata for useful tools that are not currently installed.

## Current knowledge pipeline

Acclorite can now build a persistent local tool index from:

- executable discovery through `PATH`;
- command-oriented local manual metadata (`apropos`, or `man -k` fallback);
- freedesktop `.desktop` application metadata.

Arch Linux receives an additional synchronized-repository package source. When `expac` is available, Acclorite snapshots the local sync catalog and fingerprints pacman's local `sync/*.db` metadata plus the `expac` producer identity. SQLite builds persist the catalog as `$XDG_CACHE_HOME/acclorite/arch-packages.db` and use FTS5 for bounded warm retrieval; no-SQLite builds retain the versioned TSV fallback. A changed sync database or producer invalidates and rebuilds the snapshot automatically. If the acceleration cache is unavailable, Acclorite falls back to the existing read-only `expac -Ss` / `pacman -Ss` path. Optional `pkgfile` enrichment can map repository packages to the executable names they actually provide.

When SQLite3 development support is available at build time, Acclorite stores the local PATH/man/desktop catalog in an FTS5 database and queries it with BM25-backed full-text retrieval before applying its own concept-aware relevance scoring. The hot path uses cheap source fingerprints on every invocation and performs a deeper documentation-tree verification periodically (and whenever `doctor` probes freshness), avoiding a full man-tree directory walk for every query.

If SQLite support is unavailable, the build still succeeds and Acclorite falls back to the live PATH/man/desktop sources; Arch repository discovery remains available independently through expac/pacman when present.

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

Inspect Acclorite's local capabilities and optional integrations without changing anything:

```bash
./build/acclorite doctor
./build/acclorite --json doctor
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



## v0.2.0

`v0.2.0` begins the post-v0.1 learning layer without changing the frozen discovery/ranking policy. Results can now carry **source-backed usage examples** and **learning resources** through a post-ranking `GuidanceProvider` boundary. Guidance is observational: it cannot change recall, candidate order, scores, semantic fit, or confidence.

The first provider is local man documentation. When the winning candidate already has proven `man` evidence, Acclorite offers `man <tool>` as a verified local learning resource and scans only an explicit `EXAMPLE` / `EXAMPLES` section for conservative command-looking lines. Extracted examples are preserved verbatim apart from terminal overstrike/ANSI cleanup and optional shell-prompt removal. If no command example can be verified, Acclorite says so instead of synthesizing syntax. Ordinary discovery enriches only the best match; Compare may enrich both resolved targets.

Search JSON advances to schema 14 with per-candidate `examples[]` and `learning_resources[]`, including `source_type`, `source_reference`, and `verified`. Future v0.2.x providers (Info, local TLDR/tealdeer cache, and carefully curated metadata) plug into the same boundary without becoming ranking sources. The frozen `acclorite-core-arch-v1` corpus remains unchanged.


## v0.1.0

`v0.1.0` freezes the first deterministic Arch/CachyOS ranking gate at **25/25 query cases and 65/65 assertions** on `acclorite-core-arch-v1`. Ranking behavior is intentionally frozen for this release; the final work is latency and release hygiene.

Final real-machine validation on CachyOS kept that gate at **100%** while reaching end-to-end **~359 ms p50 / ~1.29 s p95**. The optimized warm Arch package source measured ~29 ms p50 and the local semantic index ~167 ms p50 in that run.

The release closes the two source-level latency hot paths without changing the frozen ranking policy:

- **Arch synchronized package metadata:** `expac -S` is used only when the local pacman sync-database/producer fingerprint changes or the cache is missing. SQLite builds persist the synchronized catalog as an FTS5 database and retrieve a bounded package recall set instead of reparsing/scanning the full repository catalog in every CLI process. No-SQLite builds retain the versioned TSV implementation. Acclorite never refreshes repositories and never calls `pacman -Sy`; live read-only `expac -Ss` / `pacman -Ss` remain failure fallbacks.
- **Local semantic index:** the earlier cheap freshness sentinels remain, and the conservative OSA fuzzy matcher now reuses three row buffers rather than allocating a full edit-distance matrix for every word comparison. An exact length-difference bound skips OSA entirely when the configured similarity threshold is mathematically unreachable. The distance definition and scoring thresholds are unchanged.
- **Cache integrity:** the Arch snapshot format advances to v3. Old snapshot formats are rejected automatically, and the fingerprint includes the resolved `expac` executable identity so PATH-shadowed test fixtures cannot poison a production-valid cache.

Controlled same-container measurements on a 25,000-package fixture reduced warm Arch lookup from ~283 ms with the TSV scan to ~11 ms with FTS5. A 200,000-pair randomized OSA microbenchmark produced the exact same aggregate similarity checksum before and after the row-buffer rewrite while reducing runtime from ~1.09-1.16 s to ~82-86 ms. These are engineering fixtures; the frozen real-machine benchmark remains the release authority.

CMake's project version is now the single source of truth for `acclorite --version`; the previously drifted CMake/CLI version strings can no longer diverge. Search JSON remains schema 13 and benchmark report schema remains 3.

The frozen `corpus-v1.json` is the v0.1.0 regression gate. Future ranking experiments must preserve it while new coverage moves to a separate corpus rather than tuning against these same 25 cases forever.


## What changed in 0.0.29-dev

The final current-corpus correctness pass changes strategy after the real `0.0.28-dev` CachyOS benchmark restored stable performance and left exactly one genuine failure: `compress a folder -> fsarchiver`. The failure persisted because specialization penalties only demoted wrong-role tools; terse documentation for legitimate general archive front doors could still miss the explicit `folder` object and remain in a weaker semantic tier.

Query-relative semantic policy now supports **positive front-door role evidence** before specialization penalties. For explicit folder/file compression, metadata that identifies a candidate as a generic archive/archiving utility, archiver, archive manager, or archive-creation tool can receive a semantic floor just above the strong tier. This does not whitelist command names. Filesystem/partition scope, backup/deployment, comparison-role, development-library, and other specialization constraints are applied after the floor, so a filesystem archiver cannot use the positive role evidence to escape its narrower scope.

The benchmark harness also advances to report schema 3. Query timeouts/errors now retain every declared assertion in the denominator instead of silently reducing the assertion count, and failed non-timeout cases automatically receive one forensic `--explain-ranking` rerun whose top candidates, semantic tiers, utilities, sources, and semantic adjustments are embedded in terminal/Markdown/JSON reports. The 25-case corpus and its expectations are unchanged. Search JSON remains schema 13.

## What changed in 0.0.28-dev

The second dedicated performance pass follows the real `0.0.27-dev` CachyOS profile instead of guessing. That run showed `pkgfile` enrichment had fallen to roughly 62 ms p50 after the parallel batch work, while `ranking-finalize` still reached roughly 3.47 s p95 / 4.12 s max and two long queries still crossed the 20-second harness timeout. The root cause was repeated query-relative semantic analysis inside sorting: every comparator call could rebuild merged descriptive text, tokenize it, evaluate specialization/role constraints, and then repeat the same work for later comparisons and preference scoring.

SearchEngine now computes a query-relative semantic ordering key once per candidate per ranking phase and caches only that numeric fit/tier for internal ordering. Pre-enrichment selection uses those cached keys plus `std::partial_sort` because only the top 24 candidates need expensive enrichment; final sorting also compares cached numeric keys rather than rerunning text analysis. Enrichment invalidates the cache before the final merge, so package fan-out and new descriptive evidence are always incorporated before final ranking. The public `ranking::effective_semantic_fit(query, candidate)` API remains uncached/query-correct, preventing a candidate ranked for one query from leaking a stale fit into another query.

Preference scoring also reuses one specialization scan for both semantic-fit and utility adjustments instead of executing the detector twice per candidate. Profiling is split further into `ranking-prefilter`, `ranking-remerge`, `ranking-preferences`, and `ranking-sort` while retaining the aggregate `ranking-finalize` stage. In a same-container side-by-side run, `search inside files recursively` fell from about 57 ms internal search to about 18 ms, `convert images` from about 81 ms to about 20 ms, and `full text search` from about 33 ms to about 11 ms. The benchmark corpus and ranking expectations are unchanged; this release is intended to reduce latency without changing recommendation behavior.

## What changed in 0.0.27-dev

The first dedicated performance pass attacks the live Arch enrichment fan-out instead of changing ranking behavior. The real `0.0.26-dev` CachyOS benchmark reported 22/25 cases only because two previously-correct queries hit the benchmark's 20-second timeout; the sole actual ranking failure remained `compress a folder`. The performance pass therefore freezes ranking and instruments the hot path.

`pkgfile` candidate enrichment is now batched through a generic `CandidateEnricher::enrich_many` API. The pkgfile implementation uses a bounded worker pool (4 workers by default, configurable with `ACCLORITE_ENRICH_WORKERS`, clamped to 8) so the existing top-24 package enrichment budget is no longer executed as 24 serial subprocesses. The subprocess helper now uses `posix_spawnp` rather than `fork` followed by C++ work in the child, making concurrent helper execution safe. A controlled 24-candidate fixture measured ~2.45s with one worker versus ~0.62s with four workers while preserving identical enrichment logic.

Search profiling is opt-in through `--profile`. Search JSON advances to schema 13 and exposes `timing.total_ms` plus per-stage timings such as `source:index`, `source:arch-packages`, `enricher:pkgfile`, `ranking-finalize`, and `confidence`; normal searches emit `timing: null`. The benchmark harness requests profiling by default and reports internal-search and per-stage p50/p95/max timings, allowing later optimization to be driven by measured source cost rather than guesswork. Profiling is observational and must not alter candidate count, ordering, scores, confidence, or retrieval behavior.

## What changed in 0.0.26-dev

The fifth benchmark-driven tuning pass adds an **operation-direction role guardrail**. The real `0.0.25-dev` CachyOS benchmark remained at 24/25 cases and 64/65 assertions, but the sole failing `compress a folder` query mutated from a filesystem backup tool to `diffoscope`. That result is lexically understandable—diffoscope explicitly handles files, archives, and directories—but operationally wrong: its front-door action is comparison, not archive creation.

For explicit archive-transform requests (`compress`, `extract`, `pack`, `unpack`, and related wording), candidates whose descriptive evidence declares a comparison/diff role are downranked unless the user explicitly requested comparison/inspection. The rule is command-agnostic; its regression uses a synthetic `deep-compare` candidate with stronger raw semantic fit than `tar`, requires the archive creator to win `compress a folder`, and separately proves that explicit `compare two folders` intent removes the operation-role mismatch. The 25-case / 65-assertion benchmark corpus and JSON schema 12 remain unchanged.

## What changed in 0.0.25-dev

The fourth benchmark-driven tuning pass closes the remaining `compress a folder -> fsarchiver` gap by distinguishing **filesystem/partition-scope archiving** from ordinary file/directory compression. `0.0.24-dev` correctly preserved alternate package descriptions, but the front-door detector still depended too heavily on literal `backup` / `deployment` wording. A local synopsis such as `filesystem archiver - save a filesystem to a compressed archive` already proves that the candidate operates at filesystem scope, even when richer repository wording is unavailable at ranking time.

Query-relative role detection now treats filesystem/partition-level archive/save/compress/image operations as a narrower storage-management role for generic folder/file requests. Explicit `filesystem` or `partition` intent removes the constraint. This is not an `fsarchiver` command rule; the regression uses a synthetic generic filesystem archiver name and requires the same behavior. The 25-case / 65-assertion benchmark corpus is unchanged from the corrected `0.0.24-dev` gate. JSON remains schema 12.

## What changed in 0.0.24-dev

The third benchmark-driven pass fixes a **lossy merged-metadata bug** in front-door suitability. A merged candidate previously kept one preferred display `summary`; alternate source descriptions were discarded even when they carried ranking-critical role information. On the real CachyOS corpus this let `fsarchiver` keep a local man synopsis while losing the repository description that identified it as a filesystem backup/deployment tool, so the backup-role specialization could not fire.

Candidates now preserve non-display `descriptive_evidence` from merged sources. The terminal/JSON summary remains unchanged, but query-relative domain/role detection evaluates the combined descriptive evidence. A regression reproduces the exact local-man + repository-package merge and requires generic `compress a folder` to prefer a normal archive front door.

The `0.0.23-dev` benchmark still reported 23/25 cases (92.0%) and 63/65 assertions (96.9%). One failure was a stale corpus expectation: after development-library demotion, `full text search` correctly moved from a tie to a clear user-facing winner (`recoll`), so that ambiguity expectation is updated from `competitive` to `clear` without changing acceptable winners. The only genuine ranking failure was `compress a folder -> fsarchiver`. Search JSON remains schema 12.

## What changed in 0.0.23-dev

The second benchmark-driven tuning pass adds **tool-role / front-door suitability** to query-relative specialization. A package can describe an operation accurately while still be the wrong kind of thing to recommend directly: filesystem backup/deployment tools are discounted for generic compression requests, and development-library packages are discounted for generic executable-tool discovery. Explicit backup/filesystem or programming/library intent removes those constraints. This targets the final two failures from the real `0.0.22-dev` CachyOS benchmark without command-specific winner rules.

The measured `0.0.22-dev` baseline is 23/25 cases (92.0%) and 63/65 assertions (96.9%). Latency remains a separate optimization target after correctness reaches the benchmark gate.

## What changed in 0.0.22-dev

- used the first real 25-query benchmark report as the ranking/query-language input instead of adding another speculative subsystem;
- inspection phrasing now preserves relational `using`/`uses` as the `usage` concept, fixing human requests such as `what is using all my disk space` and strengthening port-owner queries;
- quantity/filler words such as `all`, `two`, `lots`, `many`, and `multiple` no longer become semantic subjects; this removes accidental evidence such as `sfddiff` matching the word `two` in `compare two folders`;
- task comparison (`compare two folders`) now remains Discover/tool-discovery intent, while entity comparison (`rg vs grep`, `compare rg and grep`, `difference between rg and grep`) remains the entity-first Compare frame;
- `tar.gz`, `tgz`, and `tarball` are recognized as an explicit `tar` format concept, and extraction semantics can use archive/archiving evidence, so format intent beats generic extractors;
- compressing files/folders infers a low-weight archive/container intent, distinguishing general archive creation from unrelated domain-specific operations that happen to use the verb `compress`;
- query-relative specialization now also covers Netlink/libnl helpers, hardware-topology tools, font comparators, audio comparators, and filesystem-format-specific diagnostics; those penalties disappear when the human explicitly requests the specialized domain;
- the benchmark now asserts that `compare two folders` stays in the Discover frame;
- regressions encode the real benchmark failure classes: human disk consumption, port ownership, tar.gz extraction, task-vs-entity comparison, and subsystem-specialization leakage.

The first real `0.0.21-dev` Arch/CachyOS benchmark baseline was 19/25 queries (76.0%) and 54/64 assertions (84.4%). `0.0.22-dev` is the first release whose changes are directly driven by those measured failures.

## What changed in 0.0.21-dev

- added the first repeatable deterministic benchmark harness under `benchmarks/`; it is evaluation tooling and is not linked into the C++ runtime;
- added a versioned 25-query Arch-focused corpus covering generic search, specialization, package discovery, storage, networking, process inspection, archive ambiguity, query frames, Compare, and Locate;
- benchmark expectations support equivalent acceptable winners rather than brittle exact-score assertions;
- reports measure query-level and assertion-level pass rates, top-1 accuracy, top-3 hit rate, query-frame accuracy, ambiguity accuracy, and end-to-end CLI latency p50/p95/max;
- benchmark reports can be emitted as terminal text, machine-readable JSON, and Markdown;
- `--strict` makes benchmark failures return non-zero for release/CI gates, while exploratory runs report failures without turning tuning sessions into build failures;
- one discarded warm-up query runs by default so stale-index auto-refresh does not contaminate normal latency measurements;
- when Python is available, CMake exposes an `acclorite_benchmark` convenience target and adds only the harness unit tests to `ctest`; the real query corpus is never part of normal unit tests;
- Python remains optional and is used only as the experimentation/evaluation layer described by the architecture.

Run the corpus with:

```bash
cmake --build build --target acclorite_benchmark
```

or write reproducible report artifacts directly:

```bash
python3 benchmarks/run.py \
  --binary ./build/acclorite \
  --json-out benchmark-report.json \
  --markdown-out benchmark-report.md
```

## What changed in 0.0.20-dev

- added first-class confidence and ambiguity assessment to every search result without changing ranking order or candidate scores;
- confidence separates top-candidate trust, interpretation confidence, candidate separation, semantic #1/#2 gap, and ranking-utility #1/#2 gap;
- ambiguity states distinguish `clear`, `competitive`, `ambiguous`, `low-confidence`, and `no-result`;
- near-equivalent tools such as `fdupes` / `rdfind` are reported as competitive rather than pretending the query itself was misunderstood;
- genuinely underspecified `archive files` queries are reported as ambiguous with deterministic clarification options for create/compress, extract/unpack, and browse/manage;
- explicit/named entity frames remain clear because multiple Compare operands are expected targets rather than competing interpretations;
- terminal output shows a compact confidence summary, while `--explain-ranking` adds confidence diagnostics alongside ranking diagnostics;
- JSON search schema advances to version 12 with `confidence` and `clarifications` fields.

Confidence is observational. It consumes the already-ranked result and must never reorder candidates, mutate scores, or trigger additional retrieval.

## What changed in 0.0.19-dev

- added query-relative specialization awareness before semantic-fit tiering: PDF-only, compressed/archive, VCS, indexing/search-engine, and translation/catalog constraints are discounted when the user did not request that domain;
- specialization penalties disappear when the domain is explicit (`search PDF text`, `search compressed files`, `full text search`), preserving dedicated-tool discovery;
- replaced the compressed-wrapper-only ranking rule with the generic specialization model;
- ranking diagnostics now expose raw semantic fit plus semantic-domain adjustments; JSON search schema is v11.

- added an explicit command-specific `semantic_fit` signal that records the strongest pure intent match before BM25/source confidence, installation friction, common-tool priors, and other recommendation preferences;
- final ordering now uses deterministic semantic-fit tiers first and recommendation `ranking_utility` second within a tier;
- the tiers intentionally separate precise full canonical intent matches from synonym-heavy but still plausible matches, preventing convenience from overriding clearly better task specificity;
- this fixes the real `duplicate files` regression where installed `hardlink` outranked dedicated repository tools even though `hardlink` only matched `duplicate` indirectly through `copies`;
- package-to-binary projection also applies to semantic fit, so broad suite metadata cannot enter a stronger semantic tier merely because pkgfile mapped the suite to one helper executable;
- fixed light stemming so singular words ending in `-ss` such as `process` are not corrupted to `proces`; `processes` now counts as a canonical morphological match for `process`;
- `--explain-ranking` now prints semantic fit and its tier alongside base/display utility;
- JSON search schema advances to version 10 with `semantic_fit` and `semantic_tier` inside opt-in ranking diagnostics;
- regressions prove a precise dedicated tool can outrank a more convenient generic tool, while diagnostics remain observational and recommendation utility still decides among candidates in the same semantic tier.

The ranking contract is now:

```text
semantic fit tier        -> primary intent guardrail
ranking utility          -> recommendation ordering within the tier
public score [0,1]       -> bounded client/display value
```

## What changed in 0.0.17-dev

- fixed duplicate relevance support when the same semantic source emits repository variants of the same package; same-source variants now keep the strongest score/evidence instead of earning `+ 0.08 × support` against themselves;
- `pkgfile` remains visible provenance/capability metadata but no longer counts as an independent semantic evidence source for the multi-source confidence bonus;
- candidates now preserve an internal unbounded `ranking_utility` separately from the existing bounded public `score`;
- source merges keep raw ranking headroom above 1.0 and final sorting uses that utility, so candidates that both display `1.00` no longer collapse into an arbitrary tie;
- recommendation priors operate on ranking utility and clamp only the public display score;
- `--explain-ranking` shows base/final ranking utility alongside bounded display score, making saturation explicit;
- JSON search schema advances to version 9 with `base_utility` and `final_utility` inside the opt-in ranking diagnostics object;
- added regressions proving same-source evidence cannot double-vote, pkgfile cannot impersonate semantic corroboration, and unbounded utility preserves ordering when public scores saturate.

The core rule is now:

```text
same evidence twice       != independent support
independent evidence      -> may add bounded corroboration
ranking utility           -> ordering, may exceed 1.0
public score              -> stable bounded 0..1 contract
```

## What changed in 0.0.16-dev

- `--explain-ranking` now decomposes the previously opaque `Base relevance/evidence` score instead of starting only at the recommendation-prior boundary;
- semantic diagnostics expose every weighted query concept, role, implicit-vs-explicit status, match quality, canonical-vs-alternative match, and whether the evidence came from the candidate name or description;
- the semantic trace reports weighted coverage, average match quality, formula score, source ceiling, and frame-bias effect;
- source-specific scoring is traced separately, including FTS/BM25 recall bonuses, man-section command bonuses, and repository-metadata support;
- candidate merging records the exact `strongest + 0.08 × supporting` arithmetic, including the raw value before the existing 1.0 base-score clamp;
- this makes base-score headroom loss visible for saturated candidates without changing ranking behavior yet;
- diagnostics remain opt-in and observational: enabling them must not alter candidate count, ordering, scores, source queries, or system mutation behavior;
- JSON search schema advances to version 8 with structured `base_evidence`, semantic concept traces, and `base_merges`;
- normal non-diagnostic queries still avoid carrying semantic trace payloads.

Example:

```bash
./build/acclorite --explain-ranking "archive files"
./build/acclorite --json --explain-ranking "search text"
```

## What changed in 0.0.15-dev

- added opt-in ranking diagnostics with `--explain-ranking`;
- ranking diagnostics are emitted by the real recommendation-preference pipeline rather than reconstructed after the fact;
- each candidate can expose a structured base score, ordered score adjustments, operation type, before/after values, and final score;
- terminal diagnostics show the top five candidate traces so close ranking decisions can be compared directly;
- `--json --explain-ranking` exposes the same structured trace for automated analysis and future Realmheart/dev tooling;
- normal searches remain uncluttered and do not attach ranking traces unless explicitly requested;
- JSON search schema advances to version 7 and emits `ranking: null` when diagnostics were not requested;
- the documented `Base relevance/evidence` boundary is the score after semantic/source retrieval and merge support but before recommendation priors;
- current trace events cover PATH-only confidence, direct man evidence, multi-source confidence, installed-tool preference, package-description projection specificity, specialization penalties, canonical front-door priors, score clamps, and entity-target promotions/floors;
- ranking diagnostics never execute commands or mutate system/index state beyond the normal query path already required for retrieval.

Example:

```bash
./build/acclorite --explain-ranking "network connections"
./build/acclorite --json --explain-ranking "network connections"
```

## What changed in 0.0.14-dev

- persistent indexes now store source-state fingerprints for PATH, manual metadata, and desktop application metadata;
- `IndexSource::probe()` reports whether a readable index is fingerprinted, fresh, or stale and identifies which source family changed;
- normal indexed searches automatically rebuild a stale cache once, while a matching fingerprint avoids rebuilding on every invocation;
- `acclorite doctor` remains read-only and reports stale indexes as warnings with the changed source families;
- indexes created before 0.0.14 remain schema-readable but are treated as stale until rebuilt once to gain freshness metadata;
- PATH fingerprints include PATH configuration and directory state; desktop fingerprints include `.desktop` file metadata; manual fingerprints track man roots/cache state and the active `apropos`/`man` helper;
- package repository metadata is intentionally not part of the SQLite fingerprint yet because Arch package discovery is currently queried live through expac/pacman rather than persisted in the local FTS index;
- no daemon, timer, background worker, package refresh, or automatic system mutation is introduced.


## What changed in 0.0.13-dev

- added `acclorite doctor` as a read-only capability/health report;
- added `acclorite --json doctor` with an independent `doctor_schema_version`;
- doctor reports PATH discovery, SQLite/FTS build support, persistent-index health, manual lookup, desktop metadata, expac, pacman fallback, Arch repository search, pkgfile, and pkgfile metadata readiness;
- persistent-index probing is non-mutating: doctor never creates or rebuilds a missing/invalid cache;
- `pkgfile` executable presence and `.files` metadata readiness are reported separately;
- installed-but-unusable optional integrations are warnings, while completely absent optional distro integrations remain informational;
- package database probes are read-only and bounded; doctor never refreshes package databases or installs software;
- JSON explicitly reports `mutated_system: false` for diagnostics consumers and future bug-report tooling.


## Arch package enrichment in 0.0.10-dev

On Arch-family systems Acclorite keeps `pacman` as the baseline repository-search backend,
uses `expac` when available for stable machine-formatted ALPM metadata, and uses `pkgfile`
when available to map repository packages to the executable names they actually provide.
All three integrations are read-only during normal queries; Acclorite never updates package
databases or installs software automatically. Missing optional helpers simply reduce metadata
quality and never prevent the generic Linux core from working.

## What changed in 0.0.10-dev

- added a bounded post-retrieval `CandidateEnricher` stage;
- optional `expac` is preferred over pacman presentation output when available, using a caller-defined tab-separated ALPM format;
- `pacman -Ss` remains the baseline Arch fallback with zero new mandatory dependencies;
- optional `pkgfile` maps repository packages to the executable names they actually provide (`ripgrep` → `rg`);
- mapped package candidates gain CLI capability even before installation;
- package identity remains separate from command identity, so `command=rg` can retain `package=ripgrep`;
- mapped repository candidates re-merge with local PATH/man/index candidates after enrichment;
- pkgfile enrichment is limited to the highest-ranked candidate set rather than querying every package result;
- repo-qualified pkgfile lookup falls back to package-name lookup for Arch derivatives whose `.files` databases use different repo naming;
- source provenance now uses exact `+`-separated tokens, fixing the delightful `pacman` accidentally-counts-as-`man` bug;
- JSON schema version 6 adds `provided_commands`;
- normal queries never run `pkgfile --update`, refresh package databases, or install software.

## What changed in 0.0.9-dev

- Query Frames now change retrieval behavior instead of acting only as metadata/ranking bias;
- explicit frame targets/entities are extracted (`what is rg` → `rg`, `difference between rg and grep` → `rg` + `grep`);
- Explain resolves an exact named entity first and suppresses substring/package noise when resolution succeeds;
- Compare resolves named tools in target order instead of literally searching for words such as `difference` or the ImageMagick `compare` command;
- Diagnose promotes a clearly named affected tool while preserving the rest of the query as context;
- Locate has its first real resource-result path through a bounded filesystem config locator;
- config location probing checks existing conventional user/XDG/system paths only and never recursively scans the home directory;
- Inspect relation inference understands omitted process ownership in `what is using port 8080` and process/resource intent in `what is eating RAM`;
- placeholder PATH-only candidates receive a small ranking penalty unless they are exact command-name matches, allowing documented tools to win close ties;
- terminal output has specialized Explain, Compare, Locate, and Diagnose presentation;
- JSON schema version 5 adds `targets` and `locations`;
- regression coverage locks down the real-machine failures exposed by 0.0.8.

## What changed in 0.0.8-dev

- deterministic Query Frame Recognizer added before retrieval/ranking;
- initial frames: `Discover`, `Explain`, `Locate`, `Inspect`, `Modify`, `Compare`, `Diagnose`, and `Unknown`;
- frame output carries confidence, explicit-vs-inferred state, and the linguistic signals that produced the decision;
- generic question words do not override stronger actions (`what is using ...` → Inspect, `where do I configure ...` → Modify);
- unframed human wording safely defaults to low-confidence Discover rather than forcing clarification;
- existing implicit read-only inspection is now restricted to state-oriented concepts instead of every multi-noun query;
- recognized Explain/Locate/etc. frames suppress contradictory implicit Inspect defaults;
- Inspect vs Modify frames provide a small ranking bias while never hard-filtering candidates;
- terminal output exposes non-default request types for development/debugging;
- JSON schema version 4 adds a structured `query_frame` object for future Realmheart and other clients;
- implementation plan now treats Query Frames as a permanent core architecture component;
- regression coverage includes normal questions, malformed/casual wording, question-word override cases, and explicit/inferred frame behavior.

Example machine metadata:

```json
"query_frame": {
  "type": "Inspect",
  "confidence": 0.99,
  "explicit": true,
  "signals": ["where do i see", "see", "implicit inspect"]
}
```

Query Frames currently guide discovery; specialized Explain/Locate/Compare/Diagnose response modes are intentionally future work. Acclorite is still a tool-discovery engine first.

## What changed in 0.0.7-dev

- first-class Arch repository discovery through a dedicated `PacmanSource`;
- synchronized pacman package names/descriptions can surface tools that are not installed;
- repository candidates retain package name, repository, and version metadata;
- installed and repository-available state are independent;
- package evidence merges with PATH/man/desktop evidence when the package/tool name aligns;
- human output distinguishes `Installed` from `Available`;
- JSON schema version 3 adds package/repository/version fields;
- the pacman backend uses read-only local sync metadata and never refreshes databases or installs software;
- strict multi-concept package search falls back to the strongest concept when repository descriptions are sparse;
- regression coverage verifies uninstalled package discovery and evidence merging.

### Current Arch limitation

Executable mapping depends on optional local `pkgfile` metadata. If `pkgfile` is absent or its `.files` cache has not been synchronized, Acclorite simply keeps the package-level candidate and reports an unknown interface when it lacks other evidence. Package-owned config/resource location discovery and deeper bulk repository indexing are still future work.

## What changed in 0.0.6-dev

- noun-heavy state queries can infer a low-level read-only inspection action;
- `network connections` therefore favors tools that inspect sockets/connections over tools whose purpose is editing them;
- explicit mutating verbs such as `edit`, `manage`, and `configure` suppress that read-only default;
- `duplicate` / typo-normalized `duplcate` receives a low-weight implicit file context when no object was supplied, preventing unrelated duplicate-message tools from dominating the ranking;
- inferred concepts affect retrieval and scoring but are not presented as if the user explicitly typed them;
- the deterministic vocabulary now understands common inspection and configuration verbs;
- `IndexSource::search()` returns its own results in semantic score order instead of leaking SQLite/BM25 row order to direct callers;
- regression coverage now locks down network inspection vs editing and duplicate-file intent in both direct scoring and the persistent FTS path.

## What changed in 0.0.5-dev

- deterministic typo recovery for query concepts and half-remembered command names;
- canonical typo normalization happens before FTS/man retrieval;
- weighted concept roles distinguish actions, subjects, and low-information context words;
- generic words such as `files` no longer carry the same ranking weight as operations such as `archive`;
- disk-specific evidence now beats unrelated tools that merely mention `usage`;
- fuzzy matching remains conservative and subordinate to exact/documented evidence;
- CMake supports both the newer `SQLite3::SQLite3` imported target and the older `SQLite::SQLite3` target;
- regression coverage for transposed command names, typo intent queries, operation/context weighting, and indexed typo retrieval.

## Current limitations

The persistent index is deliberately simple in this development build:

- persistent PATH/man/desktop staleness detection is implemented; package database fingerprints are unnecessary while Arch package discovery remains live rather than persisted;
- full man-page bodies are not indexed yet;
- desktop-entry parsing is intentionally conservative;
- the current fuzzy matcher is an internal conservative OSA/edit-distance implementation; RapidFuzz remains a candidate if benchmarks justify replacing it;
- phrase/entity handling is still small;
- specialized frame routing exists, but Locate currently covers only bounded conventional config paths and Compare does not yet produce capability-by-capability differences;
- Arch package discovery uses a fingerprinted local synchronized-metadata snapshot (FTS5 on SQLite builds, TSV fallback without SQLite), with live expac/pacman fallback and optional pkgfile package-to-binary enrichment; package-owned config/resource location discovery is still future work;
- confidence/ambiguity now exists, but clarification families are intentionally small and the numeric confidence model still needs corpus calibration;
- no common-tool/simplicity prior exists yet.

The next Arch/location work can reuse pkgfile metadata for package-owned config/resource discovery. A larger ranking benchmark and confidence calibration remain higher-priority quality work before any optional LLM fallback.


## Evidence specificity in 0.0.12-dev

Repository enrichment now distinguishes package-level evidence from command-level evidence.
A package description is discounted when `pkgfile` projects it onto one executable out of a
large multi-binary package, preventing broad suite/library metadata from overpowering direct
command documentation. Installed tools receive a small zero-friction tie-breaker, and a bare
`search text` query gains a low-weight implicit file-content context so normal grep-style
search tools outrank unrelated full-text indexing engines. Strong task-specific semantics still
beat all of these priors.

## Ranking sanity in 0.0.11-dev

Acclorite now applies a conservative recommendation-preference layer after semantic retrieval and package enrichment. Semantic relevance remains authoritative; preference priors only break close ties.

The layer currently:

- penalizes PATH-only candidates when richer documented evidence exists;
- gives a tiny confidence bonus to independent evidence such as man/package metadata;
- applies a deliberately small prior to established front-door utilities;
- demotes format-specific grep wrappers for generic text-search queries, but removes that penalty when the query explicitly concerns archives/compression.

This is intended to stop technically-correct but obscure helpers from beating the normal Linux tool a human would reasonably expect, without turning popularity into truth.
