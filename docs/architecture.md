# Acclorite Architecture — Current Implementation

This file tracks the architecture that exists in code today. The full product design lives in `specification.md`; the staged engineering roadmap lives in `implementation-plan.md`.

## Current pipeline

```text
CLI input
   ↓
Query::parse
   ↓
Query Frame Recognizer
   │  Discover / Explain / Locate / Inspect / Modify / Compare / Diagnose
   ↓
Frame target/entity extraction
   │
   ├── Explain/Compare → entity-first resolution
   ├── Locate → bounded LocationSource(s)
   └── Inspect → relation inference where useful
   ↓
IndexSource (preferred when SQLite/FTS5 is available)
   │
   ├── PATH catalog
   ├── man/apropos catalog (sections 1 + 8)
   └── desktop-entry catalog
   │
   ▼
SQLite FTS5 index
   ↓
BM25 recall
   ↓
concept-aware relevance scoring
   │
   ├──────────────┐
   │              │
   │         PackageBackend (selected from local distro/package state)
   │         ├── PacmanSource (Arch; cached sync metadata)
   │         │   expac snapshot preferred / live expac/pacman fallback
   │         ├── AptSource (Debian family; local apt-cache + dpkg-query)
   │         ├── DnfSource (Fedora/RHEL family; cache-only DNF + rpm)
   │         ├── ZypperSource (openSUSE family; no-refresh XML Zypper + rpm)
   │         └── XbpsSource (Void family; synchronized local XBPS indexes)
   │              │
   └──────┬───────┘
          ↓
 initial ranking
          ↓
 CandidateEnricher(s)
   └── PkgfileEnricher (optional)
          ↓
 re-merge by command identity
          ↓
 final ranking + confidence
          ↓
 bounded GuidanceProvider(s)
   ├── ManGuidanceProvider (best match; Compare targets)
   ├── InfoGuidanceProvider (verified local fallback)
   ├── TldrGuidanceProvider (exact local cache fallback)
   └── CuratedGuidanceProvider (reviewed bundled fallback)
          ↓
SearchResult
   ├── TerminalRenderer
   └── JsonRenderer

Separate diagnostic path:

CLI `doctor`
   ↓
Doctor (read-only probes)
   ├── core/index capability checks
   ├── local knowledge-source checks
   └── active package-backend + metadata readiness checks
   ↓
DoctorTerminalRenderer / DoctorJsonRenderer
```

If the indexed source cannot be built or SQLite support was unavailable at compile time:

```text
SearchEngine
   ├── PathSource
   ├── ManSource
   ├── DesktopSource
   └── selected PackageBackend
       ├── PacmanSource
       ├── AptSource
       ├── DnfSource
       ├── ZypperSource
       └── XbpsSource
```

The indexed path is therefore an optimization and retrieval upgrade, not a hard condition for Acclorite to function.


## Terminal presentation

Human terminal rendering is intentionally separate from the frozen JSON contracts. `TerminalRenderer` and `DoctorTerminalRenderer` default to plain ANSI-free text when constructed by tests or embedding code; the CLI opts into styling only when the corresponding stdout/stderr file descriptor is a TTY. `NO_COLOR` and `TERM=dumb` suppress ANSI entirely, so pipes, redirected output, test capture, and minimal terminals remain clean. Color never changes semantic content or exit status.

Search presentation uses a scan-first hierarchy rather than repeating each field as its own vertical block. Explicit query-frame/target context is placed first, followed by the primary candidate, compact aligned metadata, verified example/resources, confidence/clarification, and then bounded alternatives. Detailed ranking and timing sections remain opt-in diagnostics. No-result terminal output is actionable but does not affect schema-15 JSON or the stable `no_result` exit code.

Doctor presentation summarizes overall health and state counts before subsystem groups. Individual details/hints remain subordinate to the check label and state glyph. The renderer explicitly communicates that Doctor is read-only. This is a human UX contract only; downstream clients must continue to consume `DoctorJsonRenderer` rather than parse terminal text.

## Doctor diagnostics

`acclorite doctor` is intentionally outside normal query retrieval. It reports the state of Acclorite's own dependencies and caches without repairing or mutating them. The persistent index is probed read-only; missing or incompatible indexes produce a `acclorite --reindex` hint instead of being rebuilt automatically by the diagnostic command. Package probes similarly never refresh metadata or install anything.

The package-backend safety rules are explicit and backend-specific:

- Arch: never `pacman -Sy`, never `pkgfile --update`, never install/remove packages;
- Debian: never `apt update`, `apt-get`, install/remove commands, or metadata refresh;
- Fedora/RHEL: every DNF/DNF5 query is cache-only and limited to search/repoquery; never `makecache` or transaction verbs;
- openSUSE: Zypper is non-interactive, XML, `--no-refresh`, and uses Acclorite's bundled config with `runSearchPackages = never`; no refresh or transaction verbs;
- Void: never `-M`/`--memory-sync`, never explicit remote repository overrides, and never `xbps-install`.

An unavailable or stale package cache degrades discovery and Doctor readiness; it does not authorize Acclorite to synchronize the system.

Optional integrations have three useful states: ready, optional/unavailable, and warning when the helper exists but its required metadata is unusable. This distinction is important for cases such as an installed `pkgfile` binary whose `.files` databases have never been synchronized. Machine output uses a dedicated doctor schema rather than overloading search-result JSON.

## Query Frame Recognizer

Before sources search, `SearchEngine` attaches a deterministic `QueryFrameResult` to the query. The frame is derived from weighted whole-query evidence rather than first-word rules.

Current frames:

```text
Discover
Explain
Locate
Inspect
Modify
Compare
Diagnose
Unknown
```

The result also carries:

```text
confidence
explicit_frame
signals[]
```

Examples:

```text
what is rg
    → Explain

what do I use to find duplicates
    → Discover

where is ssh config
    → Locate

where do I see network connections
    → Inspect

what is using port 8080
    → Inspect

edit network connections
    → Modify
```

Frames currently influence deterministic interpretation and provide a small Inspect-vs-Modify ranking bias. They do **not** hard-filter candidates. Queries with weak frame evidence safely fall back to inferred Discover.

The existing noun-heavy read-only inspection default is now limited to state-oriented subjects such as network connections, processes, memory, CPU, ports, disk, and storage. This prevents unrelated noun phrases such as `fuzzy directory jumping thing` from being mislabeled as Inspect.

Search JSON schema 15 exposes structured frame metadata, resolved `targets`, resource `locations`, package metadata, confidence, guidance, and ranking diagnostics so clients such as Realmheart can react without re-parsing human terminal output or package-manager text.

## Frame targets and specialized routing

Query Frames now influence the retrieval path, not merely ranking. `SearchEngine` extracts explicit targets after frame recognition.

```text
what is rg
    → Explain
    → target: rg
    → exact entity resolution

difference between rg and grep
    → Compare
    → targets: rg, grep
    → resolve both in target order

why is ssh refusing my key
    → Diagnose
    → affected target: ssh
```

Explain and Compare fall back to ordinary discovery when exact resolution fails. Diagnose keeps generic context retrieval but promotes the named affected tool when it can be resolved.

Locate additionally queries `FilesystemLocationSource`. The first implementation only probes bounded conventional configuration locations and returns paths that actually exist; it never recursively scans the home directory. Location results are separate from tool `Candidate`s.

Inspect relation inference currently handles two common omitted-subject forms:

```text
what is using port 8080 → implicit process ownership / identify
what is eating RAM      → implicit process/resource monitoring
```


## Persistent index

Default path:

```text
$XDG_CACHE_HOME/acclorite/index.db
```

Fallback:

```text
~/.cache/acclorite/index.db
```

The database remains a rebuildable cache, but the promoted live snapshot is treated as last-known-good state. `acclorite --reindex` builds a sibling staging database under an exclusive rebuild lock, checkpoints/validates that staging snapshot, and atomically promotes it over the live path only after validation succeeds. Failed or interrupted rebuilds clean staging artifacts and preserve the previous promoted database.

Normal search does not perform the full source-tree fingerprint on every process start. A cheap fingerprint watches PATH directories, desktop application directories, and the immediate manual/cache directory structure; mismatch triggers automatic refresh. The deeper fingerprint remains used by `doctor`, one-time migration of older indexes, and a periodic safety verification. This keeps automatic staleness recovery without paying the expensive directory walk on every query.

The current FTS table indexes:

- command name;
- display summary;
- merged searchable metadata.

It stores but does not full-text-index:

- executable path;
- source provenance;
- installed state;
- repository availability;
- CLI capability;
- GUI capability.

FTS/BM25 is used for candidate recall and tie-breaking. Acclorite's deterministic concept scorer remains the stronger semantic guardrail.

## Candidate interface capability

A candidate does not store a guessed single type. It stores independent capabilities:

```text
cli_capable
gui_capable
```

The UI derives:

```text
CLI      = CLI only
GUI      = GUI only
Hybrid   = both
Unknown  = neither known
```

Examples of evidence:

- `PathSource` → CLI capability;
- `DesktopSource` → GUI capability;
- both sources identifying the same executable → Hybrid.

This keeps the model extensible and avoids special-casing individual applications.

## Current knowledge sources

### PathSource

Discovers executable files in the user's PATH.

### ManSource

Uses `apropos` when available, with `man -k` as a fallback. Search is restricted to manual sections 1 and 8 and candidates must correspond to executable tools.

For index creation, the source enumerates the local command-description catalog in one broad manual-database query.

### DesktopSource

Scans standard freedesktop application directories plus the user's XDG data directory. It reads stable non-localized fields such as:

```text
Type
Name
GenericName
Comment
Exec
TryExec
```

Desktop metadata is both searchable evidence and GUI-capability evidence.

### PackageBackend

Distribution-specific repository discovery implements a common `PackageBackend` contract layered on top of `KnowledgeSource`. Backends expose a stable backend ID and package family but project their findings into the normal `Candidate` model, so `SearchEngine`, candidate merging, ranking, confidence, guidance, terminal output, and schema-15 JSON remain distribution-agnostic.

`system::detect_distro()` reads local `os-release` data without subprocesses. When multiple supported package managers coexist, the detected family chooses the active backend; if exactly one supported backend exists, it remains usable in minimal chroots/containers with incomplete distro metadata. `ACCLORITE_OS_RELEASE` is a test/controlled-environment override for the os-release path.

### PacmanSource

First-class Arch-family repository discovery. When `expac` is available Acclorite enumerates the synchronized ALPM catalog into a disposable local cache and fingerprints pacman's local `sync/*.db` files. Fresh queries search the cached snapshot in-process; a changed sync database invalidates the snapshot. If the cache cannot be used, the source falls back to the previous read-only live `expac -Ss` path and then `pacman -Ss`.

Important properties:

- never invokes `pacman -Sy`;
- never refreshes package metadata; the cache is derived only from the user's already-synchronized local ALPM databases;
- the package-cache hot path fingerprints sync DB files by metadata and atomically replaces its snapshot on rebuild;
- never installs or removes anything;
- `expac` and `pkgfile` remain optional;
- repository availability and installed state are independent candidate fields;
- package name, repository, and version are preserved;
- multiple concept regexes are passed separately as boolean-AND package searches;
- when strict multi-concept lookup returns nothing, it retries the strongest concept and lets Acclorite's semantic scorer filter noise.

### AptSource

First Debian-family repository backend. It is intentionally read-only and local-cache-only:

- `apt-cache search` provides package-name/description discovery from the already-populated local APT cache;
- `dpkg-query` provides installed-package state and installed version when available;
- one bounded `apt-cache show --no-all-versions` call enriches the surviving candidate set with locally cached package versions;
- no `apt-get`, `apt update`, install, remove, or metadata-refresh command is ever invoked;
- if the local APT cache is unavailable or stale, the backend simply contributes fewer/no repository candidates and Doctor reports the readiness problem.

APT findings use the same package fields (`package`, `package_version`, `installed`, `repository_available`) and the same semantic package scoring policy as the Arch backend. Package-to-executable mapping beyond same-name binaries is intentionally deferred to a future Debian enricher rather than being guessed.

### DnfSource

First Fedora/RHEL-family repository backend. `DnfSource` prefers `dnf5` and falls back to `dnf`, but both paths are forced through `--cacheonly` so expired local metadata is accepted without network synchronization.

The retrieval path is deliberately bounded:

- cached `dnf[5] --cacheonly search --all` supplies package-name/summary/description discovery;
- only semantically surviving package names are sent to one cache-only `repoquery` call for EVR, repository ID, and summary metadata;
- one bounded local `rpm -q` call supplies installed-package state/version for those survivors;
- executable names that equal package names are resolved through PATH and naturally merge with local evidence.

No DNF transaction or metadata-refresh operation is permitted. Acclorite never invokes `makecache`, `refresh`, `install`, `upgrade`, `remove`, or their equivalents. If cache-only DNF metadata is unavailable, the backend contributes fewer/no repository candidates and Doctor reports the Fedora integration as degraded.

### ZypperSource

First openSUSE-family repository backend. `ZypperSource` uses Zypper's machine-readable XML search output and treats the package manager strictly as a local metadata reader. Every invocation includes `--no-refresh`, `--non-interactive`, `--xmlout`, and `--ignore-unknown`. Acclorite also ships a tiny read-only Zypper configuration under its data directory with `[search] runSearchPackages = never`; this explicitly disables the optional `zypper-search-packages` extended lookup rather than inheriting a user configuration that could request it.

The retrieval path is bounded:

- normal `search --search-descriptions --type package` supplies package-name/summary discovery from already-local repository metadata;
- surviving package names are enriched by one `search --details --type package --match-exact` call, preserving package edition and repository identity;
- `@System`/`(System Packages)` rows are treated as installed-system provenance, while a repository-backed instance is preferred when one is available;
- one bounded local `rpm -q` call supplies installed-package state/version for survivors;
- same-name executables found on PATH merge naturally with package evidence.

No `refresh`, install, update, remove, distribution-upgrade, or other transaction command is permitted. If cached Zypper metadata is unusable, the backend contributes fewer/no repository candidates and Doctor reports the openSUSE integration as degraded.

### XbpsSource

Void Linux repository backend. `XbpsSource` treats XBPS as a read-only local metadata source: repository search is performed with `xbps-query --regex -Rs` against synchronized on-disk indexes. Acclorite never passes `-M`/`--memory-sync`, never invokes `xbps-install`, and therefore never asks XBPS to synchronize repositories or execute a package transaction during search or Doctor.

The retrieval path stays bounded:

- `xbps-query -L` is used only by Doctor to determine whether registered on-disk repository indexes are readable;
- `xbps-query -l` is used only by Doctor to verify the local installed-package database;
- `xbps-query --regex -Rs` supplies package-version strings, descriptions, and the `[*]` installed marker for semantic repository discovery;
- two bounded `xbps-uhelper` calls (`getpkgname` and `getpkgversion`) decode the surviving XBPS `pkgver` values robustly instead of guessing where the package name ends;
- same-name executables found on PATH naturally merge with repository evidence.

The backend intentionally does not perform per-candidate repository-name lookups, because the XBPS query interface accepts one package/pattern per show/search operation and an N-subprocess enrichment pass would violate Acclorite's bounded retrieval design. Repository availability and package version are retained without fabricating repository identity. If synchronized XBPS metadata is missing or stale, discovery simply contributes fewer/no package candidates and Doctor reports the Void integration as degraded.

### PkgfileEnricher

Optional Arch package-file enrichment. It runs only after initial retrieval/ranking and only on a bounded candidate set. For repository package candidates it asks `pkgfile --list --binaries` which executable files the package provides.

This allows identity refinement such as:

```text
package = ripgrep
provided executable = /usr/bin/rg
→ command = rg
→ package remains ripgrep
→ interface becomes CLI
```

After enrichment, candidates are re-merged by command name so an uninstalled repository candidate can naturally join an installed/local candidate for the same executable. Acclorite never runs `pkgfile --update`; stale or missing pkgfile metadata simply means enrichment is skipped.

## Source provenance

Candidate evidence sources are stored as exact `+`-separated tokens (`path+man+expac+pkgfile`). Source merging compares complete tokens rather than substrings, so `pacman` can no longer accidentally masquerade as `man`.

## Process safety

Known helper programs are invoked through `posix_spawnp()` with argument vectors. User queries are not interpolated into shell command strings.

## SQLite portability behavior

CMake probes for SQLite3 development support.

If found:

```text
ACCLORITE_HAS_SQLITE=1
persistent FTS source enabled
```

If absent:

```text
ACCLORITE_HAS_SQLITE=0
index stub compiled
live sources used instead
```

Both configurations are built and tested during development.

## Current test coverage

The suite now covers both algorithmic behavior and the public/system boundaries around it.

Core/query/ranking coverage includes:

- query normalization, phrase/concept expansion, typo recovery, and weighted relevance;
- deterministic Query Frame recognition, explicit-vs-inferred metadata, target extraction, and specialized Explain/Compare/Locate/Diagnose routing;
- relation inference for port ownership and resource-hog queries;
- evidence-preserving source merge, ranking headroom, preference rules, confidence, ambiguity, and clarification behavior;
- PATH, man/apropos, desktop metadata, SQLite FTS retrieval, no-SQLite live-source fallback, and CLI/GUI/Hybrid capability behavior;
- persistent-index fingerprints, stale detection, staged rebuild, rebuild serialization, last-known-good preservation, and recovery after forced rebuild failure;
- post-ranking man, Info, TLDR, and curated guidance precedence/provenance.

Distribution/backend coverage is hermetic and mutually exclusive:

- Pacman/expac/pkgfile Arch fixtures;
- Debian APT + dpkg fixtures with an `apt-get` mutation trap;
- Fedora DNF5/DNF4 + RPM fixtures with mandatory `--cacheonly` and transaction traps;
- openSUSE Zypper + RPM fixtures with `--no-refresh`, plugin-disabled configuration, XML parsing, and transaction traps;
- Void XBPS fixtures with `--memory-sync`, remote-repository, and `xbps-install` traps;
- distro-family `os-release` detection, single-backend chroot fallback, and multi-backend tie-breaking/refusal-to-guess behavior.

Public-interface/release coverage includes:

- stable CLI exit classes and usage parsing;
- search/Doctor/capability JSON structural contract tests;
- schema enums, nullability, additive-field compatibility, and status/exit-code pairing;
- pseudo-terminal ANSI behavior, `NO_COLOR`, `TERM=dumb`, and JSON remaining ANSI-free even on a TTY;
- staged `/usr` installation, relocation-safe bundled data lookup, man/docs payload, and package-layout checks;
- the benchmark harness plus the unchanged real Arch/CachyOS reference corpus (25/25 cases, 65/65 assertions at the v0.3.3 Milestone-9 gate).

Fixtures keep tests independent of the host's installed tools and deliberately fail if a backend crosses its read-only/offline boundary.

## Milestone 10 architecture — deterministic actionable answers

The next engineering phase intentionally happens **before optional AI**. Acclorite already answers “which tool is relevant?”; the next goal is to answer “what should I do, why is that the answer, and what verified syntax applies?” without turning the product into a command executor or a generative model.

The first Milestone-10 slices now implement the architectural seam without changing
generic ranking: `CommandGrammar`/syntax-provenance models, a separate
`CommandSyntaxProvider` interface, conservative local-man `SYNOPSIS`/`* OPTIONS`, static packaged/admin Fish completion options, plus
`COMMANDS`/`SUBCOMMANDS` parsing, structured `ActionTarget` query intent, deterministic
`AnswerComposer`, additive `action_target` + `actionable_answer` JSON, and terminal
rendering for verified options/subcommands. Explicit option syntax preserves raw case
(`-L` != `-l`) and punctuation-valued short options (`-.`); natural flag/subcommand
questions carry a parent command and capability terms, then match those terms only
against locally proven names/descriptions with ambiguity rejection. Direct syntax
questions use exact parent-entity resolution but do not modify ranker weights or the
ordinary discovery path. The man parser treats deeper-indented `--flag` text as prose,
removes U+2010 groff line-wrap hyphen artifacts, and conservatively recognizes command
entries such as `git-branch(1)` or `status [PATTERN...]`. Simple representable command tails are now
parsed into provenance-bearing positional slots and the first `ArgumentBinder` slice can bind
unambiguous user literals into them. Same-position slot unions with identical shape, such as
`status [PATTERN...|PID...]`, may collapse into one provenance-bearing bindable slot because either arm
occupies the same argv position; literal/mixed alternatives remain unbound. Practical root-command binding now
also consumes conservative independent `SYNOPSIS` alternatives such as `[OPTION]... SOURCE DEST`,
`[OPTIONS] PATTERN [PATH...]`, or `DIRECTORY...`. Binding entities retain lightweight relation/kind evidence
(source/destination/name/location and path/URL/number/general) so multiple user literals can be assigned without
turning filler-word removal into command-specific syntax. Required values may become placeholders, but independent
alternatives are never flattened together and opaque required grammar is rejected. A selected global option may
compose with a root synopsis generic `[OPTIONS]` allowance only when the option's own value shape is independently
proven. Incomplete option-only templates remain preferred over root variants that add only invented placeholders.
`CommandInvocation` is structured data with literal/placeholder segments and a completeness bit; terminal rendering uses a
dedicated shell-quoting layer. Parent-proven child-command manual inspection is now available as a bounded
lazy enrichment path: only subcommands already proven by root man grammar may derive `<parent>-<child>` manual
lookups, and deeper syntax is accepted only when it explains more of a compound operation than root grammar.
Child-option bindings may now become complete only when a compatible child `SYNOPSIS` alternative explicitly
proves the selected option/value path and contains no additional unmet mandatory piece. Consecutive same-indent
manual synopsis forms are retained as independent alternatives; generic `[<options>]` allowance alone never
proves a selected option complete. Static Fish completion parsing is live for conservative option/subcommand
metadata, and syntax evidence is now merged across providers before answer composition. Evidence-driven safety
classification is now live as the final Milestone-10 layer: only concrete, complete, placeholder-free invocations
are eligible, and a label is emitted only when requested/selected operation semantics agree with source-backed
command/option/subcommand descriptions. Incomplete templates and conflicting or weak evidence remain `Unknown`.

Planned pipeline:

```text
ranked SearchResult
      ↓
selected verified Candidate
      ↓
CommandSyntaxProvider(s)
      │
      ├── local man SYNOPSIS / OPTIONS
      ├── local Info syntax/option sections
      ├── statically parsed shell-completion metadata
      └── existing TLDR/curated examples as example evidence only
      ↓
CommandGrammar
      │
      ├── command
      ├── global flags/options
      ├── subcommands
      ├── positionals / accepted argument shapes
      └── provenance per syntax fact
      ↓
ArgumentBinder
      │
      ├── query entities / paths / patterns / ports / names
      ├── bind only when the grammar proves the slot
      └── leave placeholders when information is missing
      ↓
CommandInvocation (structured argv/argument model, not a shell string)
      ↓
AnswerComposer
      │
      ├── concise frame-aware explanation
      ├── verified command/template
      ├── relevant flags/subcommands
      ├── source-backed examples/documentation
      └── safety classification
      ↓
TerminalRenderer / JsonRenderer
```

### Structured action targets

Query semantics now distinguish the selected tool from the thing being asked about
inside that tool. `ActionTarget` is additive query/result structure with target kind,
optional parent command, optional exact literal syntax, capability terms, and an
`explicit_syntax` marker. Exact literals come from the raw query; normalized search
text is never allowed to rewrite command syntax.

Current target kinds are `option`, `subcommand`, `positional`, and `operation`. Option
and first-stage subcommand selection are live end-to-end. `operation` is now also the internal
bridge for hostile/conversational wording when the human addresses a command but does not know
whether the requested behavior is implemented by a flag or subcommand. Strong operation phrasing with concrete
bindable data may also preserve a command-agnostic `operation` target; ranking still selects the parent tool first,
and only then may verified root grammar construct an invocation. This does not add syntax terms to ranking or make
vague discovery queries pay grammar cost. Grammar resolution may promote an operation to the proven syntactic kind;
it never guesses between two plausible kinds. Semantic option answers prefer
a proven descriptive long alias for presentation, while explicit syntax inspection
preserves the exact literal spelling from the query. Subcommand questions are framed
explicitly as Explain requests and keep target nouns out of capability terms; composition
occurs only when the selected syntax provider has proven a matching subcommand identity
and description. Selection remains distinct from binding: only representable proven positional tails may
feed the binder, and Explain-only questions still stop at grammar facts. Ordinary discovery without an
action target performs no syntax-provider work.

Human-intent recovery is deterministic and deliberately upstream of syntax composition. Strong
command-address shapes such as `rg is ...`, `curl keeps ...`, `ffmpeg start ...`, or `systemctl show ...`
anchor the first token as the parent command while phrase recovery emits a small capability signature
(e.g. dotfiles → hidden/files, redirect-following → follow/redirect, time-offset reading → seek/position).
Conversational scaffolding remains ordinary language and is not promoted into command syntax.

Human-language recovery feeds a query-relative **role-specific ranking policy**. Lexical overlap alone is not enough when the candidate performs the wrong kind of work in the same vocabulary domain. The policy can therefore add traceable positive front-door evidence or traceable contradictory-role penalties using merged local metadata. Examples include extraction versus searching inside an archive, filesystem comparison versus image comparison, image-format conversion versus text/terminal rendering, and filesystem usage versus unrelated executables named `size`.

Explicit-entity bypasses are based on resolved structure rather than raw token coincidence. A sentence such as `compare these directories` does not count as an explicit request for an executable named `compare`; an exact command query or resolved parent-command target does. Output-format modifiers may remain available in raw query evidence while being withheld from parent-tool semantic scoring when they would otherwise drown the core operation. All role adjustments remain visible in ranking diagnostics and must preserve the frozen discovery gate.

Natural option selection is evidence matching, not command ranking. The parent command
is resolved as an exact entity, then capability terms are compared against the selected
command's verified option names/descriptions. Insufficient coverage or a near-tied best
match produces no answer. Multiple independently valid options for one broad capability
remain a distinct ambiguity problem; Acclorite must eventually expose that uncertainty
rather than hard-code a supposedly canonical spelling. This keeps `what flag makes curl
follow redirects` in the post-ranking syntax layer rather than turning option prose into
global retrieval/ranking evidence.

### Command grammar and syntax provenance

`CommandGrammar` is planned as a deterministic, source-backed model rather than a hand-written command encyclopedia. It should be able to represent at least:

```text
command identity
subcommands[]
options[]
  short/long spellings
  description
  whether a value is accepted/required
  value placeholder/type when known
positionals[]
usage/synopsis alternatives[]
provenance for every accepted syntax fact
```

A flag/subcommand is eligible for a user-facing answer only when Acclorite can trace it to a trusted local source or a deliberately reviewed bundled record. Missing syntax is better than guessed syntax.

Child manual pages are a second depth within the same trusted man source, not a new discovery mechanism. Root
grammar must first prove the child identity; the provider then derives exactly one page name from the parent and
child (`git` + `switch` → `git-switch`) and may parse that local page for child options. Answer composition bounds
these lookups to a few semantically plausible children and requires deeper grammar to improve requested-operation
coverage before it can override a root syntax fact. This guards against introducing an unrequested extra operation.

Shell completions are a high-value syntax source, but completion files are never sourced/executed. `FishCompletionSyntaxProvider`, registered after man syntax so local manuals remain authoritative when they can answer, performs exact `<command>.fish` lookup in system/admin Fish completion roots and parses only an understood single-line `complete` subset. Static short/long option names, descriptions, explicit `--require-parameter` / `--exclusive` facts, and a tiny allowlist of declarative condition forms become completion-proven grammar.

The condition allowlist is intentionally structural rather than shell-like. `__fish_use_subcommand` may prove literal static `-a/--arguments` values as subcommands. `__fish_seen_subcommand_from <literal...>` may scope an option only to a subcommand identity that is already proven by trusted root grammar. That proof may come from the same Fish file or from a higher-priority provider such as man; the Fish condition itself still cannot invent the hierarchy. Variable references, negation, chained conditions, arbitrary helper functions, command substitution, dynamic arguments, continuations, and unknown `complete` switches are skipped. Compact understood Fish switches such as `-xs` are decoded without invoking Fish. The provider exposes scoped options through the same parent-proven child-grammar resolver used by deeper man syntax.

Syntax-provider output is authority-ordered and merged before composition. Earlier providers keep conflicting facts; later providers may add non-overlapping options, subcommands, synopsis alternatives, or child-scoped facts. Duplicate option evidence is never field-spliced in a way that would falsify provenance: a lower-priority duplicate can replace an arity-unknown fact only when it proves a strictly stronger value shape, and the replacement carries that source's provenance as a whole fact. Subcommand identity keeps its higher-priority provenance while nested option/synopsis/argument facts retain their own provenance. This allows combinations such as a man-proven subcommand plus a Fish-proven scoped option without pretending either source proved the other source's fact.

Completion metadata can prove option identity without proving value arity. `CommandOption::value_shape_known` makes that uncertainty explicit. Unknown-shape completion options may participate in semantic option answers, but `ArgumentBinder` refuses to build an invocation from them. A condition-scoped Fish option can therefore produce a verified child template when its value shape is explicit, but completion condition evidence alone never proves full command-line completeness because it is not a SYNOPSIS path. No completion script is sourced and no `fish` process or target command is executed.

Acclorite must not probe arbitrary discovered commands with `--help` merely to obtain syntax. Any future help-probing capability requires an explicit safety design and is not part of the initial actionable-answer slice.

### Argument binding and command construction

The binder consumes structured query evidence that already exists (frame, targets, entities, paths, patterns, ports, formats, etc.) and maps it into grammar slots. Binding is deterministic and conservative:

- bind a value only when both intent and grammar make the role clear;
- preserve the user's literal data separately from syntax tokens;
- do not invent required arguments;
- if information is missing, emit a verified template/placeholder instead of guessing;
- distinguish mutually exclusive syntax alternatives rather than combining incompatible flags; same-position value-kind unions may be collapsed only when every arm is a proven slot with identical optionality/repetition shape;
- only emit an end-of-options marker such as `--` when that command's verified grammar supports it.

Internally, an actionable command is represented as structured arguments/segments, not by concatenating a shell command string. Human terminal output uses a dedicated shell renderer with correct quoting/escaping rules. Machine JSON exposes structure so clients never need to scrape the pretty command text.

The first binder implementation deliberately has a narrow completeness contract. Proven subcommand
positionals may become a complete invocation when every required slot is satisfied and no extra user
literals remain. Human paraphrases already canonicalized into operation evidence are consumed as
structural language only when that recovered operation proves the relation (for example `go there` after
`switch`, or `new` after `create + branch`); the same words remain ordinary bindable data elsewhere.
Required option values can be bound or represented as placeholders, but the result stays incomplete until
root command grammar proves the remaining argv shape. Leading-dash positional literals are refused rather
than relying on quoting, because shell quoting cannot prevent option interpretation.

Acclorite itself still **never executes the constructed command**.

### Deterministic answer composition

The answer layer is factual composition over structured results, not free-form generation. Templates vary by query frame, for example:

```text
Discover → use X for Y; why it fits; example/template
Explain  → X is Y; relevant capability/options; documentation
Modify   → use X/subcommand to perform Y; verified invocation/template
Inspect  → use X to inspect Y; verified flags/arguments when known
Locate   → show verified paths/resources; related command only when useful
Compare  → explain supported differences from retrieved metadata/grammar
Diagnose → bounded tool-oriented explanation; no invented troubleshooting steps
```

The composer should be able to answer option/subcommand questions directly, such as “what flag makes curl follow redirects?”, “what does ffmpeg -ss do?”, or “how do I create and switch to a git branch?”, when the required syntax is locally verified.

### Safety classification

Copyable syntax deserves stronger metadata than ordinary tool discovery. Planned command/action classifications include:

```text
ReadOnly
Mutating
Destructive
Privileged
Network
Unknown
```

Classification is evidence-driven and uses the most specific verified semantics available: selected option/subcommand descriptions when present, otherwise the source-backed candidate summary for a proven root invocation. Query/action-target terms supply the requested operation side of the comparison; names alone never establish safety. A concrete URL can strengthen independently source-backed network semantics. Only complete, placeholder-free invocations are classified. Dominant-class precedence is `Destructive` → `Privileged` → `Network` → `Mutating` → `ReadOnly`; this is a display contract for the current single-valued `ActionSafety` model, not a claim that the categories are mutually exclusive in the real world. `Unknown` is a valid state whenever the syntax is partial, evidence conflicts, or no strong correspondence exists. Terminal and JSON renderers expose the classification without changing Acclorite into an execution-confirmation system.

### Benchmark requirement

Milestone 10 uses two new benchmark surfaces in addition to the frozen discovery gate:

- a **human-language adversarial corpus** that asks whether Acclorite can recover the user's intent from incomplete recall, filler, slang, metaphor, typos, and requests that omit implementation words such as `flag` or `subcommand`;
- a separate **actionable-answer corpus** that asks whether the selected command grammar, option/subcommand, arguments, provenance, shell rendering, and safety metadata are actually verified.

Keeping these axes separate prevents a downstream grammar failure from hiding successful intent/tool discovery, and prevents correct tool ranking from being mistaken for a verified command answer. The human-language corpus should grow from real failure reports and should not be softened merely because current deterministic parsing cannot yet satisfy a case. Its accepted-answer sets nevertheless describe **all known semantically valid front doors**, not a single preferred executable; if real source-backed evidence proves that an omitted tool genuinely satisfies the task, the corpus should be corrected rather than forcing ranking to reject a correct answer.

The actionable-answer corpus must test:

- correct tool **and** correct subcommand/flag selection;
- argument binding and placeholder behavior;
- source/provenance correctness;
- invalid/unsupported flag rejection;
- shell rendering/quoting edge cases;
- no sourcing/execution of completion files;
- no arbitrary command execution;
- evidence-driven safety classification behavior, including deliberate `Unknown` refusal;
- unchanged 25/25 + 65/65 discovery/reference behavior.

Only after this deterministic layer and the later advanced-deterministic research phase have been evaluated should optional AI move back onto the active roadmap.

## Post-ranking guidance (v0.2.x)

Usage examples and learning resources are deliberately outside retrieval and ranking. A `GuidanceProvider` receives an already-ranked `Candidate` and may return source-backed `UsageExample` / `LearningResource` records. SearchEngine attaches guidance only after ranking is complete and result limits are known, so a documentation provider cannot become accidental semantic evidence or alter recommendation order.

`ManGuidanceProvider` is the first implementation. It activates only when existing candidate provenance already contains a local `man` source, adds `man <command>` as a verified learning resource, and reads the local manual solely to extract conservative command-looking lines from an explicit EXAMPLE(S) section. It never executes the discovered tool itself. Ordinary discovery enriches only the top result to bound latency; entity Compare can enrich each resolved target. Profiling records this separately as `guidance:man`.

`InfoGuidanceProvider` is the v0.2.1 fallback. Because Info is not a ranking source, it proves the exact local topic inside the guidance layer with `info --where <command>` before exposing `info <command>`. That verification is observational and is never copied into ranking provenance. If an earlier provider already found a verified example, Info stops after the learning link; otherwise it may render the selected local Info node and conservatively extract command-looking lines only from an explicit Example/Examples section. Profiling records this separately as `guidance:info`.

As of v0.2.2, the provider rejects obvious Info misses before spawning GNU Info. It scans standard/`INFOPATH` documentation directories for either an exact command-labelled `dir` menu entry or an exact dedicated `<command>.info*` manual. Only plausible topics pay for `info --where <command>` and optional rendering. This preserves external verification for positive guidance while preventing negative topic searches from dominating latency. The fast prefilter is not ranking evidence and is never copied into candidate provenance.


`TldrGuidanceProvider` is the v0.2.3 fallback after man and Info. It does not invoke a TLDR client at all. Instead it discovers local tealdeer/TLDR cache roots, including tealdeer's configured `directories.cache_dir`, and performs exact filesystem lookup for the current Linux/common `<command>.md` page. The page filename is not sufficient proof by itself: the first Markdown heading must also name the exact command. Missing pages therefore cost only bounded filesystem probes and cannot trigger a network-capable client.

When an exact local page is found, Acclorite records it as `tldr` provenance and may expose `tldr <command>` as the learning target when a TLDR client is installed; otherwise the verified page path itself remains the resource target. Example extraction runs only if higher-priority guidance has not already produced an example, and copies at most two command-looking inline-code lines directly from the cached page. TLDR placeholder syntax is preserved verbatim. `verified` means source identity/text was verified against the local file; community-maintained TLDR content remains semantically distinct from authoritative upstream man/Info documentation. Tealdeer custom pages/patches are intentionally not folded into `tldr` provenance in this slice because they are user-authored material and need a separate provenance policy.

`CuratedGuidanceProvider` is the v0.2.4 final fallback in this milestone. It loads a bounded bundled TSV corpus once at provider construction and performs exact command-key lookup only; there is no subprocess, network request, fuzzy match, or ranking interaction. Each accepted record must include an upstream HTTP(S) source and an ISO verification date. Curated learning resources may coexist with higher-priority guidance, but curated examples are emitted only when man, Info, and TLDR produced none. The corpus remains deliberately bounded and review-driven so this layer cannot silently grow into an unaudited hand-maintained Linux encyclopedia.

Curated metadata is shipped as data, not C++ logic. CMake copies `data/curated-guidance.tsv` into the build tree and installs it under the Acclorite data directory; `ACCLORITE_CURATED_GUIDANCE` is a test/packaging override and is labeled `local-override` rather than `acclorite-project` provenance. Invalid or undated rows are ignored. Version-scoped syntax is intentionally deferred: until applicability constraints are represented explicitly, a curated example must be stable enough to ship without a version gate or it does not belong in the corpus.

Search JSON schema 15 exposes:

```text
results[].examples[]
    text
    source_type
    source_reference
    verified
    verified_by
    verified_on

results[].learning_resources[]
    label
    target
    source_type
    source_reference
    verified
    verified_by
    verified_on
```

For man, Info, and TLDR the new audit fields are empty because Acclorite is verifying local source identity/text at query time rather than asserting a bundled review. Curated records set `verified_by = acclorite-project` and carry their review date. Missing guidance remains valid: Acclorite always prefers an empty example list over guessed syntax.


## Stable machine interface (v0.2.5+)

Milestone 7 treats the executable boundary itself as an API. Search JSON, Doctor JSON, capability discovery, stdout/stderr separation, and process exit status are versioned/documented contracts for clients such as Realmheart. Shared schema/exit-code constants live in `core/machine_interface.hpp`; renderers consume those constants rather than carrying independent magic numbers.

`--capabilities` is a local-only discovery endpoint. It reports interface support plus cheap environmental detection, while `doctor` remains the deeper readiness/degradation probe. Capability detection must never call `IndexSource::available()` (which may rebuild), spawn package/documentation subprocesses, update TLDR/package metadata, or perform network I/O.

The stable exit classes distinguish successful operation, valid search/no result, usage error, Acclorite operational failure, and a successfully completed Doctor run that found a fundamental broken state. This distinction lets callers react to behavior without parsing human text. Search schema 15 is the first Milestone-7 compatibility baseline; additive optional fields remain compatible, while removal/rename/type/meaning changes require the corresponding schema version to advance. See `docs/machine-interface.md` for the normative boundary.

`v0.2.6` freezes that baseline structurally. The schema contract tests exercise the real executable and assert required keys, value types, enum domains, nullability, schema/status pairing, and representative frame/document shapes while intentionally ignoring unstable values and permitting unknown additive fields. This makes the compatibility rules executable: a client-visible breaking change should fail CI before it can silently ship.

## Milestone 8 release hardening: index recovery

`v0.2.7` changes persistent-index rebuild from destructive replacement to staged promotion. The live SQLite index is treated as the last known-good snapshot: rebuilders serialize through a sibling lock file, construct a clean sibling staging database, write fingerprints and FTS content there, checkpoint the staging WAL back into the database file, validate the staged schema/content through a read-only reopen, and only then atomically rename the validated sibling over the live path. Failed or interrupted staging work is disposable; the previous promoted database is not removed first.

A failed refresh never deletes the previous promoted snapshot. `probe()` / Doctor can therefore continue to inspect and report that last known-good database as stale, while a later successful rebuild can replace it normally. The rebuild lock serializes competing rebuild processes, and the live path continues to name only the previously promoted database until the replacement is complete; readers never observe the partially built staging database.

## Release installation and distribution boundary

Acclorite is distributed under `GPL-3.0-or-later`. A release installation contains the executable, the bundled curated guidance TSV, the bundled read-only Zypper configuration used to disable extended/network search hooks, installed user/machine-interface documentation, and the `acclorite(1)` manual. The normal runtime remains offline: distribution may fetch source/packages, while search and guidance/package discovery only consume local state.

On Linux, bundled runtime data is discovered relative to the running executable first. Curated guidance resolves through `../share/acclorite/curated-guidance.tsv`, and the Zypper backend resolves `../share/acclorite/zypper-readonly.conf`. This keeps packaged `/usr`, source `/usr/local`, per-user `~/.local`, and staged `DESTDIR` layouts self-contained instead of coupling runtime data lookup to a configure-time absolute prefix. Build-tree lookup remains a fallback for development.

The stable Arch/AUR package follows fixed release tags and runs the public CTest contract suite before packaging. Optional local integrations (`man-db`, `expac`, `pkgfile`, `tealdeer`) remain optional dependencies; the package must not turn them into hidden runtime requirements.
