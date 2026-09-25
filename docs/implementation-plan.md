# Acclorite — Implementation Plan

## Status

**Project:** Acclorite
**Artifact type:** Implementation plan / engineering roadmap
**Primary implementation language:** C++20
**Experimental / evaluation language:** Python
**Primary platform:** Arch Linux family (first-class integration)
**Portable baseline:** Generic Linux
**Primary interface:** Standalone CLI
**Future first-party integration:** Realmheart widgets / visual frontend
**Core rule:** The non-LLM engine must be strong enough that ordinary tool-discovery queries do not need AI.

---

# 1. Purpose of This Document

The main Acclorite specification defines **what the product is**.

This document defines **how we are going to build it**.

Its job is to preserve the implementation decisions made after the original specification, especially:

- Acclorite will be implemented directly in C++, not built as a full Python application first.
- Python will be used as an experimentation and evaluation laboratory.
- The deterministic, non-LLM retrieval engine is the real product.
- A deterministic Query Frame Recognizer classifies request shape before retrieval and exposes explicit/inferred confidence.
- Fuzzy matching, full-text search, synonym expansion, local documentation, package metadata, command discovery, and ranking should solve the overwhelming majority of queries.
- LLM support is optional and should only handle unusually vague, ambiguous, slang-heavy, or otherwise difficult natural-language intent.
- Arch Linux receives first-class integration.
- Generic Linux functionality must remain useful even on unsupported distributions.
- Distribution-specific features live behind adapters and must not leak into the core.
- Realmheart is a future visual client of Acclorite, not Acclorite's owner or runtime dependency.

This plan intentionally separates stable architecture from experimental implementation details.

---

# 2. Product Boundary

The product remains:

> "I know what I want to do — just tell me what Linux tool I should be looking at."

Acclorite is not:

- an autonomous shell agent;
- a package manager;
- a command executor;
- a system optimizer;
- a generic chatbot;
- a daemon;
- a replacement for `man`;
- a Realmheart-only component.

The deterministic discovery engine should be useful even if:

- no LLM is installed;
- there is no internet connection;
- the distribution does not have a dedicated Acclorite backend yet;
- optional utilities such as TLDR, `fzf`, `expac`, or `pkgfile` are absent.

---

# 3. Hard Engineering Principles

These are implementation-level rules, not suggestions.

## 3.1 No-AI quality is the benchmark

If an ordinary Linux-tool-discovery query requires an LLM to produce useful candidates, the search/retrieval engine should be treated as incomplete.

Examples that should normally work without AI:

```text
find duplicate files
what is using all my disk space
search inside files recursively
which process is using port 8080
compress a folder
extract a tar.gz
compare two folders
find large files
watch CPU usage
rename lots of files
convert images
show open network connections
```

## 3.2 AI is an intent fallback, not a knowledge source

If AI support is later enabled:

```text
Human query
    ↓
LLM intent normalization
    ↓
Acclorite deterministic retrieval engine
    ↓
Acclorite ranking
    ↓
Verified result
```

Not:

```text
Human query
    ↓
LLM invents command
    ↓
Display hallucinated command
```

## 3.3 Local evidence wins

Ranking and examples should prefer evidence from:

1. installed command metadata;
2. local man pages;
3. local info pages;
4. distribution package metadata;
5. package file indexes;
6. trusted local supplementary sources;
7. curated Acclorite metadata.

AI-generated command syntax is not part of the normal path.

## 3.4 Missing capabilities degrade gracefully

Optional capability absent:

```text
skip that source
```

Not:

```text
Acclorite fails to start
```

## 3.5 No background daemon

Acclorite should remain a normal CLI utility.

Indexes may be persisted to disk, but no permanently running Acclorite process should be required.

## 3.6 Arch-specific code stays isolated

Arch gets the best integration.

The core must not become an Arch-specific codebase.

## 3.7 Query frames are weighted evidence, not prefix rules

Acclorite should recognize the *kind of request* the user is making, but must never reduce natural language to brittle checks such as:

```text
starts with "what is" → Explain
starts with "where"   → Locate
```

Question words are weak evidence. Actions, entities, phrase structure, and the rest of the query can override them.

Examples:

```text
what is ripgrep
    → Explain

what do I use to search text
    → Discover

where is ssh config
    → Locate

where do I see network connections
    → Inspect

what is using port 8080
    → Inspect, not Explain
```

Query-frame recognition should bias interpretation and ranking, never imprison retrieval. If frame confidence is weak, Acclorite safely defaults to **Discover**.

---

# 4. Platform Strategy

Acclorite has three support layers.

## Tier A — Generic Linux Core

This must work on any reasonably normal Linux environment.

Capabilities:

- CLI parsing;
- query normalization;
- internal synonym / phrase vocabulary;
- fuzzy matching;
- full-text index;
- ranking;
- PATH executable discovery;
- generic local documentation discovery;
- JSON output;
- terminal output;
- capability detection.

This is the minimum useful Acclorite.

## Tier B — Arch Linux First-Class Integration

Target family:

- Arch Linux;
- CachyOS;
- EndeavourOS;
- other pacman-based Arch derivatives where compatible.

Additional capabilities may include:

- pacman package metadata;
- repository package discovery;
- package ownership information;
- `expac` enrichment when available;
- `pkgfile` repository-file / executable discovery when available;
- Arch-specific package availability confidence;
- richer installed/non-installed tool mapping.

The Arch backend is the reference distribution backend.

## Tier C — Other Distribution Backends

Implemented in Milestone 9:

- Debian / Ubuntu / derivatives through `AptSource`;
- Fedora / RHEL family through `DnfSource`;
- openSUSE / SUSE family through `ZypperSource`;
- Void Linux through `XbpsSource`.

Additional package backends are demand-driven rather than completeness-driven. Generic Tier-A behavior remains the fallback on distributions without a dedicated backend. Alpine/APK-specific integration is intentionally not planned.

An unsupported distro should still receive Tier A functionality.

The user should never see:

```text
Unsupported distribution. Exiting.
```

unless the underlying Linux environment is too incomplete to provide even generic functionality.

---

# 5. Language and Toolchain

## 5.1 Main application

Use **C++20** as the initial baseline.

Why C++20 rather than immediately requiring C++23:

- excellent performance;
- native binary;
- no interpreter startup;
- broad compiler availability;
- enough language features for the architecture;
- fewer packaging headaches on distributions with older compilers.

C++23 may be adopted later if a concrete feature justifies raising the minimum compiler version.

## 5.2 Build system

Use **CMake**.

Initial target:

```text
cmake -S . -B build
cmake --build build
```

Later packaging can wrap this for AUR and other distributions.

## 5.3 Python's role

Python is not the production Acclorite implementation.

Use Python for:

- ranking experiments;
- benchmark scripts;
- generating evaluation reports;
- query-corpus analysis;
- testing synonym strategies;
- testing candidate scoring weights;
- testing embeddings;
- testing local models;
- plotting latency / accuracy results;
- one-off data preparation.

Rule:

> If code belongs in the final Acclorite runtime, prefer C++. If code exists to answer an engineering question, Python is allowed to be disposable.

---

# 6. Dependency Strategy

The runtime should have as few hard external dependencies as practical.

## 6.1 Bundled / compile-time candidates

### SQLite + FTS5

Use SQLite as the local index database.

Strong candidate strategy:

- vendor or build the official SQLite amalgamation;
- explicitly enable FTS5;
- avoid relying on a distro SQLite build having identical compile-time options.

Use cases:

- command index;
- package descriptions;
- documentation text;
- aliases;
- tags;
- BM25 full-text ranking;
- persisted source metadata.

### RapidFuzz C++

Use RapidFuzz C++ for fuzzy matching / edit-distance style scoring.

Prefer build-time integration so the user does not need to install a separate fuzzy-matching runtime package.

### JSON

Need a safe JSON serializer for machine output.

Candidate approaches:

1. vendored header-only JSON library;
2. tiny internal serializer with rigorous escaping tests.

Do not hand-roll fragile JSON without tests.

Decision can be finalized during project skeleton work.

## 6.2 Optional external capabilities

Never make these unconditional runtime requirements:

- `man`;
- `apropos`;
- `whatis`;
- `info`;
- `tldr` / tealdeer;
- `fzf`;
- `pacman`;
- `expac`;
- `pkgfile`;
- `apt`;
- `dnf`;
- `zypper`;
- `xbps-query`;
- Ollama.

Acclorite detects available capabilities and uses them opportunistically.

---

# 7. High-Level Architecture

```text
                         ┌─────────────────────┐
                         │      Interfaces     │
                         │                     │
                         │ CLI / JSON / future │
                         │ Realmheart client   │
                         └──────────┬──────────┘
                                    │
                                    ▼
                         ┌─────────────────────┐
                         │    Acclorite Core   │
                         │                     │
                         │ Query → Retrieval   │
                         │ → Ranking → Result  │
                         └──────────┬──────────┘
                                    │
              ┌─────────────────────┼─────────────────────┐
              │                     │                     │
              ▼                     ▼                     ▼
       Query Processing      Knowledge Sources      Capability System
              │                     │                     │
      normalize / fuzzy       docs / packages         detect features
      frames / synonyms       PATH / metadata         select adapters
      phrases / entities
              │                     │
              └──────────────┬──────┘
                             ▼
                        Local Index
                             │
                          FTS/BM25
                             │
                             ▼
                           Ranker
                             │
                             ▼
                        Result Model
                             │
              ┌──────────────┴──────────────┐
              ▼                             ▼
       Terminal Renderer               JSON Renderer
                                              │
                                              ▼
                                      Realmheart later
```

---

# 7.1 Candidate Enrichment Pipeline

Some knowledge sources answer *which package/tool is relevant*, while other helpers answer
questions about an already-discovered candidate. These must remain separate architectural jobs.

Acclorite therefore supports a post-retrieval **Candidate Enricher** stage:

```text
Knowledge sources
    ↓
initial candidates
    ↓
initial ranking
    ↓
CandidateEnricher(s) on a bounded top set
    ↓
re-merge by canonical command identity
    ↓
final ranking
```

An enricher may add metadata or refine identity, but should not become a second independent
search engine. The first implementation is Arch `pkgfile` enrichment:

```text
package candidate: ripgrep
    ↓ pkgfile --list --binaries
provided executable: /usr/bin/rg
    ↓
command = rg
package = ripgrep
interface = CLI
    ↓
merge with local PATH/man `rg` candidate if present
```

Enrichment is deliberately bounded to the highest-ranked candidates so a normal query does not
spawn package-file lookups for dozens or hundreds of irrelevant packages. Missing enrichers
never make a query fail.

# 8. Core Modules

The exact file names may change, but these boundaries should remain.

## 8.1 Query module

Responsibilities:

- raw input storage;
- tokenization;
- normalization;
- punctuation handling;
- lowercasing / case-folding where appropriate;
- stopword reduction;
- phrase detection;
- query-frame recognition;
- explicit-vs-inferred frame metadata;
- synonym expansion;
- typo-tolerant expansion;
- entity extraction;
- query variants;
- query confidence metadata.

Conceptual model:

```cpp
struct Query {
    std::string raw;
    std::vector<std::string> tokens;
    QueryFrameResult frame;
    std::vector<std::string> concepts;
    std::vector<std::string> expanded_terms;
    std::vector<Entity> entities;
};
```

## 8.2 Candidate module

One canonical candidate representation for every source.

Conceptually:

```cpp
struct Candidate {
    std::string command;
    std::string package;
    std::string summary;

    bool installed = false;
    bool repository_available = false;

    std::vector<SourceEvidence> evidence;
    std::vector<std::string> matched_terms;

    double lexical_score = 0.0;
    double fuzzy_score = 0.0;
    double package_score = 0.0;
    double installed_score = 0.0;
    double specificity_score = 0.0;
    double final_score = 0.0;
};
```

The ranker should never need to know whether the candidate came from pacman, apt, man, TLDR, or PATH.

## 8.3 Knowledge source interface

Conceptually:

```cpp
class KnowledgeSource {
public:
    virtual bool available() const = 0;
    virtual void index(IndexWriter&) = 0;
    virtual std::vector<Candidate> search(const Query&) = 0;
    virtual ~KnowledgeSource() = default;
};
```

Potential sources:

```text
PathSource
ManSource
WhatisSource
InfoSource
LocalDocsSource
CompletionSource
TldrSource
PackageSource
CuratedMetadataSource
```

Not all need to exist in the first milestone.

## 8.4 Package backend interface

Distribution-specific package logic goes here.

Conceptually:

```cpp
class PackageBackend {
public:
    virtual bool available() const = 0;

    virtual std::vector<PackageRecord>
        search(std::string_view query) = 0;

    virtual std::optional<PackageRecord>
        owner_of(const std::filesystem::path&) = 0;

    virtual std::vector<std::string>
        executables_for(const PackageRecord&) = 0;

    virtual ~PackageBackend() = default;
};
```

Current implementations:

```text
PacmanSource         ← Arch reference backend
AptSource            ← Debian family, v0.3.0
DnfSource            ← Fedora/RHEL family, v0.3.1
ZypperSource         ← openSUSE family, v0.3.2
XbpsSource           ← Void family, v0.3.3
```

The concrete class names currently follow the `*Source` naming used by the retrieval layer while implementing the common package-backend contract.

## 8.5 Index module

Owns:

- SQLite database;
- schema migrations;
- FTS tables;
- source timestamps;
- incremental refresh logic;
- database health;
- index versioning.

## 8.6 Ranker

Combines retrieval signals and produces final ordering.

## 8.7 Result model

The result is UI-independent.

Conceptually:

```cpp
struct SearchResult {
    QueryInterpretation interpretation;
    std::vector<Candidate> ranked_candidates;
    Confidence confidence;
    std::vector<ClarificationOption> clarifications;
};
```

## 8.8 Renderers

Initial:

- TerminalRenderer
- JsonRenderer

Later:

- interactive terminal renderer;
- Realmheart visual renderer/client.

The core never prints directly.

---

# 9. Local Tool Index

Proposed location:

```text
~/.cache/acclorite/index.db
```

Exact XDG path handling should use the appropriate XDG cache directory.

The database should be rebuildable.

Nothing critical should exist only in the cache.

## 9.1 Candidate schema

Conceptual tables:

```text
tools
    id
    command
    package
    summary
    installed
    repository_available
    source_confidence
    updated_at

tool_aliases
    tool_id
    alias

tool_tags
    tool_id
    tag

tool_examples
    tool_id
    example
    source_type
    source_reference
    verified

tool_sources
    tool_id
    source_type
    source_reference
    source_version
    updated_at
```

## 9.2 FTS table

Index fields may include:

- command name;
- aliases;
- package name;
- short description;
- man NAME section;
- man DESCRIPTION excerpt;
- package summary;
- tags;
- curated concepts.

Different columns can receive different BM25 weights.

Do not dump unlimited documentation text into the hot ranking path until benchmarks justify it.

---

# 10. Query Processing Pipeline

The deterministic query processor should become unusually strong.

Example:

```text
"bro what thing tells me which fucker stole port 8080"
```

Possible stages:

```text
raw query
    ↓
normalize punctuation / casing
    ↓
identify low-value conversational filler
    ↓
recognize query frame
    ↓
extract important phrases
    ↓
detect entity: 8080 = likely port
    ↓
expand concept: port → network socket / listener
    ↓
expand phrase: "stole port" → process using / owning port
    ↓
generate search variants
    ↓
FTS candidate retrieval
    ↓
fuzzy rescoring
    ↓
source-aware ranking
```

No LLM required.

## 10.1 Normalization

Handle:

- casing;
- repeated punctuation;
- obvious filler;
- common contractions;
- simple spelling errors;
- hyphenation variants;
- singular/plural variants where useful.

Do not aggressively destroy technical tokens such as:

```text
tar.gz
systemd
8080
SIGKILL
C++
eth0
```

## 10.2 Built-in concept vocabulary

Acclorite should ship with a compact curated vocabulary.

Example domains:

```text
search
files
text
storage
filesystem
network
ports
processes
memory
CPU
hardware
services
logs
permissions
users
archive
compression
conversion
images
video
audio
comparison
synchronization
mounting
packages
downloads
monitoring
profiling
shell
navigation
```

Example synonym groups:

```text
storage:
    disk
    drive
    filesystem
    space
    SSD
    HDD

large:
    huge
    biggest
    hogging
    consuming
    eating
    taking up

process:
    app
    program
    task
    PID

search:
    find
    locate
    look for
    lookup
```

This vocabulary should be treated as data, not scattered hard-coded conditionals.

## 10.3 Phrase mappings

Certain phrases encode intent better than individual words.

Examples:

```text
"eating my disk"
    → disk usage / large files / large directories

"using port"
    → network socket / process ownership

"fuzzy find"
    → fuzzy search / interactive filter

"jump between directories"
    → shell navigation / directory history / frecency

"what changed between folders"
    → directory comparison / diff
```

Phrase mappings should carry weights and can evolve from evaluation failures.

## 10.4 Entities

Useful deterministic entities may include:

- port numbers;
- file extensions;
- paths;
- process IDs;
- package-like names;
- command-like tokens;
- archive formats;
- protocols;
- filesystem units / sizes.

Entity recognition should be rules-first.

## 10.5 Query Frame Recognition

Acclorite should deterministically recognize the broad *request frame* surrounding the user's concepts. This is not a literal question-word parser. It is weighted evidence over phrasing, actions, entities, and query structure.

Initial frames:

| Frame | Typical meaning | Example |
| --- | --- | --- |
| **Discover** | Find a tool/command/way to do something | `what do I use to compare folders` |
| **Explain** | Explain an already named thing | `what is ripgrep` |
| **Locate** | Find a path/config/resource location | `where is ssh config` |
| **Inspect** | Read/show/monitor current state | `where do I see network connections` |
| **Modify** | Edit/configure/manage state | `edit network connections` |
| **Compare** | Compare named things or capabilities | `difference between rg and grep` |
| **Diagnose** | Explain a failure/problem | `why is ssh refusing my key` |
| **Unknown** | No usable evidence | internal fallback state |

### 10.5.1 Discover is the safe default

A query without a strong recognizable frame should behave like ordinary Acclorite discovery.

```text
fuzzy directory jumping thing
    → Discover (inferred / low confidence)
```

The recognizer should not force a clarification merely because the user failed to write a grammatically clean question.

### 10.5.2 Question words are weak signals

The system must consider whole-query evidence:

```text
what is rg
    → Explain

what is using port 8080
    → Inspect

where is ssh config
    → Locate

where do I configure Wi-Fi
    → Modify
```

The action phrase wins when it conflicts with a generic `what` / `where` surface form.

### 10.5.3 Explicit vs inferred frames

Frame output should preserve whether the user supplied clear linguistic evidence or Acclorite inferred a safe default.

Conceptually:

```cpp
struct QueryFrameResult {
    QueryFrame frame;
    double confidence;
    bool explicit_frame;
    std::vector<std::string> signals;
};
```

This distinction matters for:

- confidence handling;
- debugging rankings;
- future clarification UX;
- Realmheart rendering;
- deciding whether an optional LLM fallback is justified.

### 10.5.4 Frames bias; they do not filter absolutely

Frame-aware ranking may prefer inspection-oriented descriptions for an Inspect request and editor/configuration descriptions for a Modify request. However, a frame must never hard-filter the candidate universe.

Example:

```text
network connections
    → Inspect (inferred)
    → `ss`-style socket inspection should beat connection editors

edit network connections
    → Modify (explicit)
    → NetworkManager editors/configuration tools should beat `ss`
```

This is a ranking bias, not command-name special casing.

### 10.5.5 Relationship to the future LLM

If the deterministic frame recognizer fails on truly absurd language, a future local LLM should return the same structured representation rather than directly recommending commands:

```text
frame
concepts
entities
constraints
alternative interpretations
```

The result then re-enters the exact same deterministic retrieval and ranking engine.

## 10.6 Frame Targets and Specialized Routing

A recognized frame is not merely display metadata. Frames that explicitly name concrete things should extract **targets/entities** and may select a specialized retrieval path.

Examples:

```text
what is rg
    → frame: Explain
    → targets: [rg]

difference between rg and grep
    → frame: Compare
    → targets: [rg, grep]

why is ssh refusing my key
    → frame: Diagnose
    → targets: [ssh]

where is ssh config
    → frame: Locate
    → targets: [ssh, config]
```

Targets are different from semantic concepts. `difference`, `between`, and `what is` describe the request shape; they should not become literal tool-search concepts when the user has already named the relevant entities.

### 10.6.1 Entity-first frames

For **Explain** and **Compare**, resolve named entities before running broad discovery:

```text
Explain(`rg`)
    → resolve exact rg entity
    → show its verified local/package metadata
    → do not bury it beneath substring-related packages

Compare(`rg`, `grep`)
    → resolve rg
    → resolve grep
    → preserve target order
    → comparison renderer / metadata
```

If exact resolution fails, Acclorite may fall back to ordinary deterministic discovery.

### 10.6.2 Diagnose targets

Diagnose remains advisory/tool-oriented in the core product. When a named tool is clearly affected, preserve that entity as a high-priority target rather than letting a secondary noun dominate retrieval.

```text
why is ssh refusing my key
    → affected target: ssh
    → key/authentication remain context
```

Actual troubleshooting advice remains a later subsystem.

### 10.6.3 Locate uses resource results, not fake command answers

Locate requires a result type beyond `Candidate`. Introduce a resource/location result that can carry:

```text
path
kind
source
score
```

The first generic filesystem locator should be deliberately bounded and existing-path-only. For configuration queries it may inspect conventional locations such as:

```text
~/.<tool>/
$XDG_CONFIG_HOME/<tool>/
/etc/<tool>/
/etc/<tool>.conf
```

It must not recursively crawl the user's home directory. Later distro/package-file backends (for example `pkgfile`) can provide additional authoritative location evidence through the same result model.

### 10.6.4 Relationship inference

Some important subjects are omitted in ordinary human language and should be inferred from relations:

```text
what is using port 8080
    → Inspect
    → port
    → implicit process ownership / identify action

what is eating RAM
    → Inspect
    → memory
    → implicit process/resource monitoring
```

These inferred concepts must remain marked implicit and must not be displayed as though the user typed them.

### 10.6.5 Specialized routing must degrade gracefully

Frame routing is an optimization over the deterministic core, not a second product. If a target cannot be resolved or a dedicated source is unavailable, fall back to normal discovery rather than failing the query.

---

# 11. Candidate Retrieval

Use multiple retrieval paths simultaneously.

## 11.1 Full-text search

SQLite FTS5 retrieves candidates from indexed textual metadata.

Primary benefits:

- fast;
- local;
- deterministic;
- BM25 ranking;
- independent of an LLM.

## 11.2 Fuzzy retrieval

RapidFuzz rescoring handles:

- typos;
- misspellings;
- approximate command names;
- near-matching descriptions;
- half-remembered tool names.

Fuzzy search should complement FTS, not replace it.

## 11.3 Exact / structural signals

Examples:

- user typed an existing command name;
- query contains a recognizable file extension;
- query names a package;
- query names a known operation;
- exact phrase matches a tool's documented purpose.

## 11.4 Source-specific retrieval

Some sources may return candidates directly.

Example:

```text
PackageBackend.search()
ManSource.search()
```

These candidates merge into the common candidate pool before final ranking.

---

# 12. Ranking Strategy

Ranking quality is one of the project's most important engineering problems.

The initial ranker can be weighted and imperfect.

Example signals:

```text
FTS relevance
fuzzy command-name similarity
fuzzy description similarity
exact phrase match
concept overlap
installed status
repository availability
task specificity
documentation quality
source confidence
query-frame compatibility
tool simplicity
command popularity / commonness (carefully)
```

Do not prematurely hard-code one permanent formula.

Initial conceptual formula:

```text
final_score =
    lexical_relevance
  + fuzzy_relevance
  + concept_match
  + exactness_bonus
  + installed_bonus
  + source_quality_bonus
  + availability_bonus
  + frame_compatibility
  - ambiguity_penalty
```

Weights must be benchmark-driven.

## 12.1 Installed status

Installed tools should generally receive a bonus, not absolute priority.

Example:

```text
query: "browse disk usage interactively"
```

If `du` is installed but `ncdu` is not:

- `ncdu` may still deserve #1 if it is a dramatically better semantic fit;
- `du` may be #2 with an Installed badge.

## 12.2 Specificity

A dedicated tool should often beat a generic tool capable of technically performing the task.

## 12.3 Recommendation Preference Layer

Semantic relevance remains authoritative, but Linux often contains several technically-correct tools for the same broad intent. Acclorite may therefore apply a **small deterministic recommendation prior** after retrieval/enrichment to break close ties in favor of a better human-facing front door.

The prior must remain weak. It should never rescue a semantically irrelevant command merely because that command is popular.

Initial signals may include:

- richer independent evidence (`PATH + man`, package metadata, desktop metadata) over name-only PATH coincidence;
- **evidence specificity**: command-level documentation outranks a broad package description projected onto one helper binary from a multi-binary package;
- a small installed-tool bonus for zero-friction use when semantic quality is close;
- a small curated prior for established general-purpose front-door utilities;
- specialization penalties when a generic query accidentally matches a format-specific wrapper;
- removal of those specialization penalties when the user explicitly requests that specialized context.

Example:

```text
search text
    rg / grep       → normal front doors
    zipgrep / xzgrep → useful alternatives, but specialized

search compressed files
    specialized wrappers are no longer penalized
```

Another example:

```text
network connections
    ss               → documented general socket-inspection front door
    nl-list-sockets  → valid candidate, but broad libnl package metadata must not masquerade as
                       command-specific documentation merely because pkgfile mapped the package
                       to that helper binary
```

Package-to-command enrichment therefore carries an evidence-scope caveat:

```text
ripgrep package → rg
provided binaries: [rg]
package description is highly specific to the mapped command

libnl package → nl-list-sockets
provided binaries: [many helpers ...]
package description describes the suite/library and receives a projection penalty
```

A similar deterministic default applies to extremely underspecified content search:

```text
search text       → weak implicit file/content context
full text search  → explicit domain term suppresses that default
```

This helps ordinary grep-style discovery without hard-coding `rg` or banning desktop/full-text search engines.

Rules:

1. Preference adjustments happen **after semantic retrieval**.
2. Priors are capped to small score changes.
3. Strong task-specific semantic evidence always beats commonness.
4. The preference layer should be benchmarked independently so curated priors do not silently grow into a hidden command whitelist.
5. Specialized tools remain fully eligible and should win when the query contains their specialization.
6. Installed status is a small friction signal, never an absolute priority.
7. Package metadata projected onto a binary must be weighted by how specifically that metadata describes the binary.

## 12.4 Evidence identity and ranking headroom

Evidence provenance must distinguish **independent corroboration** from repeated views of the same underlying source. A source may emit multiple repository variants, package aliases, or enrichment passes, but those variants do not become additional semantic votes merely because they re-enter the candidate merge pipeline.

Rules:

1. Two candidates from the same relevance source family merge by taking the stronger evidence; they do **not** receive the cross-source support bonus.
2. Package-file mapping (`pkgfile`) is capability/provenance evidence, not semantic evidence. It may establish which executable a package provides and therefore affect interface/package projection logic, but it does not by itself increase query relevance.
3. Distinct relevance sources may corroborate each other. Example: indexed local documentation plus ALPM package metadata are meaningfully independent enough to contribute a small support term.
4. Diagnostic evidence lists deduplicate the same source/method pair, retaining the stronger trace.
5. Repository priority/availability metadata may still preserve the preferred installable package variant without turning mirrored/override repositories into extra relevance confidence.

Acclorite also separates **ranking utility** from the bounded public score.

```text
source relevance scores
        ↓
independent-source merge
        ↓
base ranking utility        may exceed 1.0
        ↓
recommendation priors
        ↓
final ranking utility       used for ordering
        ↓
public score clamp [0,1]    stable user/client score
```

This prevents score saturation from destroying ordering information. If two candidates both display `1.00`, the candidate with stronger underlying utility must still sort first. Raw utility is diagnostic/internal ranking state, not yet a calibrated probability or confidence value.

The JSON/terminal diagnostic contract should expose `base_utility`, `final_utility`, and the bounded score separately when ranking diagnostics are requested. Normal clients may continue consuming the bounded score without needing to understand ranking headroom.

## 12.5 Confidence and ambiguity

Confidence is a **separate result signal**, not a renamed candidate score. The bounded public `score` remains a relevance/recommendation value; confidence answers whether Acclorite trusts its interpretation and whether the top recommendation is meaningfully determined.

The implemented result model reports:

- top-candidate confidence;
- interpretation confidence;
- candidate-separation strength;
- semantic-fit gap between #1 and #2;
- ranking-utility gap between #1 and #2;
- ambiguity state;
- deterministic confidence signals;
- optional clarification choices;
- query-frame confidence and whether the frame was explicit or inferred.

Ambiguity states are intentionally distinct:

```text
clear          interpretation is coherent and the winner is meaningfully supported
competitive    interpretation is coherent but multiple tools are near-equivalent
ambiguous      the query itself leaves a material intent choice unresolved
low-confidence best semantic fit is weak
no-result      no candidate exists to assess
```

A tiny #1/#2 gap alone must **not** imply that the query was misunderstood. `duplicate files` may legitimately produce nearly tied `fdupes` and `rdfind`; this is `competitive`, not query ambiguity. Conversely, `archive files` is under-specified because create/compress, extract/unpack, and browse/manage are materially different operations. That query receives deterministic clarification options.

The first clarification family is deliberately small and benchmark-driven. Do not create a giant hand-maintained ambiguity ontology. Add families only when real regressions demonstrate that a broad human phrase has multiple materially different interpretations that ranking alone cannot resolve.

Confidence is observational by contract:

```text
rank candidates
      ↓
freeze order/scores
      ↓
assess confidence + ambiguity
      ↓
render terminal / JSON
```

Enabling ranking diagnostics or confidence output must not alter candidate count, ordering, score, or retrieval behavior.

JSON exposes a dedicated top-level `confidence` object plus `clarifications`; search schema version 12 introduces this contract. Numeric confidence is initially heuristic and must be calibrated against the Milestone 5 benchmark corpus before being treated as a probability.

---

# 13. Knowledge Sources

## 13.1 PATH source — early and generic

Index executable names available through the user's PATH.

Benefits:

- works on nearly every Linux system;
- tells Acclorite what can actually run;
- no package-manager dependency.

Potential future additions:

- shell built-ins;
- aliases, with caution;
- user-local binaries.

## 13.2 Man / apropos / whatis

Use when available.

Potential information:

- command name;
- one-line summary;
- NAME section;
- DESCRIPTION;
- SYNOPSIS;
- examples if explicitly present.

Never assume man-db exists.

## 13.3 Info pages

Optional source.

Useful for GNU tools with richer info documentation.

## 13.4 `/usr/share/doc`

Later source.

Need strong filtering to avoid indexing junk or enormous unrelated documents.

## 13.5 Shell completions

Promoted into Milestone 10 as a high-value deterministic **command syntax** source. Completion metadata can describe subcommands, flags, option descriptions, and accepted argument shapes that ordinary package/man summaries do not expose cleanly.

Potential locations:

```text
/usr/share/bash-completion/
/usr/share/fish/vendor_completions.d/
/usr/share/zsh/site-functions/
```

Potential data:

- subcommands;
- flags;
- descriptions;
- accepted argument types.

Treat completion parsing as optional, shell-specific, and static-only. Acclorite must never source or execute a completion file. Parsers accept only syntax they can understand and prove; dynamic shell code and unknown constructs are skipped.

## 13.6 TLDR / tealdeer

Optional example enrichment.

Use local cache when present.

Do not make community examples more authoritative than local official documentation.

## 13.7 Curated Acclorite metadata

A small bundled data set may contain:

- synonyms;
- aliases;
- categories;
- known phrase mappings;
- carefully verified tool tags.

This is not intended to become a giant hand-maintained Linux encyclopedia.

---

# 14. Arch Linux Backend

Arch receives first-class integration.

## 14.1 Baseline Arch source

Start with pacman-compatible metadata.

Goals:

- know installed packages;
- know configured repository packages;
- retrieve package descriptions;
- map installed binaries to package owners where useful;
- determine repository availability.

## 14.2 `expac`

`expac` is the preferred optional Arch sync-metadata reader when available because it allows
Acclorite to request a stable machine-defined output format directly from ALPM databases.
`pacman -Ss` remains the mandatory-free fallback; Acclorite must behave correctly when `expac`
is absent. Normal Acclorite queries never modify package databases.


Use opportunistically when present.

Potential benefits:

- efficient ALPM metadata extraction;
- richer package fields;
- easier package indexing.

Not a hard dependency.

## 14.3 `pkgfile`

`pkgfile` is an optional **candidate enricher**, not the primary semantic search backend. It maps
repository package identity to files/executables provided by that package and may later provide
package-owned config/resource locations to Locate queries. Acclorite does not run `pkgfile --update`;
metadata synchronization remains under the user's/package-manager's control. If pkgfile metadata is
missing or stale, enrichment is skipped without affecting core discovery.


Use opportunistically when present.

Potential benefits:

- discover which repository package provides a binary;
- map package names to executable names;
- discover commands not currently installed.

This is especially valuable because:

```text
package name != executable name
```

in many real packages.

Not a hard dependency.

## 14.4 Future libalpm experiment

Do not make the generic Acclorite core link directly against libalpm.

Later, benchmark whether a dedicated Arch backend using libalpm is worth the complexity.

Possible strategies:

- separate optional backend library;
- dynamically loaded plugin;
- compile-time Arch package variant.

Do not solve this before evidence says the shell-out / metadata approach is insufficient.

---

# 15. Capability Detection

Acclorite must understand and explain the state of its own local brain. The implemented entry point is:

```bash
acclorite doctor
acclorite --json doctor
```

`doctor` is **read-only by contract**. Diagnostics may probe files, executables, local databases, and bounded command output, but must never rebuild the index, refresh package databases, install packages, start services, or otherwise repair the system implicitly. Repair actions are hints for the user to invoke explicitly.

Current checks include:

```text
Core
  PATH discovery
  SQLite / FTS build capability
  persistent index existence + compatible schema

Knowledge
  apropos / man lookup availability
  XDG desktop metadata directories

Arch integration
  expac executable + readable sync metadata
  pacman fallback + readable sync metadata
  repository package search backend readiness
  pkgfile executable availability
  pkgfile .files metadata readiness
```

The status model distinguishes:

- **Ready** — the capability is usable now;
- **Warning** — Acclorite can still run, but an installed/configured capability is degraded or its metadata is unusable;
- **Optional** — the integration is absent and is not required for the current portable core;
- **Error** — a fundamental capability required for basic operation is broken.

This distinction matters. A non-Arch machine without `pkgfile` is not unhealthy. An Arch machine with `/usr/bin/pkgfile` present but no usable `.files` database is degraded and should receive an explicit `sudo pkgfile --update` hint.

The persistent-index check must never call `IndexSource::available()` because that path is allowed to auto-build the cache. Diagnostics instead use a non-mutating index probe that reports:

```text
sqlite_supported
exists
ready
path
size_bytes
```

Arch metadata probes are similarly bounded and read-only. `expac`/`pacman` may query already synchronized package databases, and `pkgfile` may query already stored `.files` metadata. Doctor never calls `pacman -Sy`, `pkgfile --update`, or installation commands.

Human output should be concise and grouped by subsystem. Machine output has its own stable diagnostic contract rather than pretending to be a search result:

```json
{
  "doctor_schema_version": 1,
  "status": "healthy",
  "mutated_system": false,
  "checks": [
    {
      "id": "pkgfile-metadata",
      "section": "Arch integration",
      "state": "ready",
      "detail": "Repository .files metadata is usable",
      "hint": ""
    }
  ]
}
```

This is useful for:

- debugging and issue reports;
- packaging validation;
- diagnosing silent fallback behavior;
- Realmheart integration;
- users on unusual systems;
- future automated support bundles.

Tests must prove that doctor is non-mutating, especially when the index is missing, and must distinguish helper presence from metadata readiness.

---

# 16. Index Lifecycle

No daemon.

## 16.1 First run

If no index exists:

```text
build generic index
    ↓
detect documentation sources
    ↓
detect distribution backend
    ↓
index available metadata
    ↓
save source timestamps
```

First-run behavior should remain reasonably fast.

## 16.2 Manual rebuild

Provide:

```bash
acclorite --reindex
```

or equivalent finalized syntax.

## 16.3 Automatic staleness detection

Later inspect timestamps / fingerprints for:

- package database changes;
- PATH changes;
- documentation database changes;
- source configuration changes;
- Acclorite schema version changes.

Refresh incrementally when practical.

Avoid reindexing everything on every invocation.

## 16.4 Corruption recovery

The index is a cache.

If corrupted:

```text
detect
    ↓
discard/rebuild safely
```

Never make the user manually repair a cache database.

---

# 17. CLI Design

Initial human interface:

```bash
acclorite "find duplicate files"
```

Possible future forms:

```bash
acclorite find duplicate files
acclorite --json "find duplicate files"
acclorite doctor
acclorite --reindex
acclorite --learn "search text"
acclorite --interactive "search files"
```

Exact syntax remains open until the first CLI prototype.

## 17.1 Human output

Compact, readable, terminal-friendly.

Example:

```text
Best Match
────────────────────────────

fdupes
Find duplicate files by comparing file contents.

Installed
✓ yes

Why this matched
duplicate files · comparison

Alternatives
rdfind
czkawka-cli

Learn
man fdupes
```

## 17.2 Machine output

Stable JSON is mandatory early.

Example shape:

```json
{
  "schema_version": 1,
  "query": "find duplicate files",
  "interpretation": {
    "concepts": ["duplicate files", "file comparison"],
    "confidence": 0.96
  },
  "results": [
    {
      "command": "fdupes",
      "summary": "Find duplicate files by comparing file contents.",
      "installed": true,
      "repository_available": true,
      "score": 0.91,
      "documentation": [
        {
          "type": "man",
          "target": "fdupes"
        }
      ]
    }
  ]
}
```

JSON fields should be versioned deliberately because Realmheart may eventually depend on them.

---

# 18. Realmheart Integration Strategy

Realmheart integration is future work, but the contract should be designed now.

## 18.1 Rule

Realmheart is a client.

Acclorite does not depend on Realmheart.

## 18.2 Initial integration path

Realmheart invokes:

```bash
acclorite --json "query"
```

Then renders the structured response using native Realmheart widgets.

Advantages:

- no shared-process coupling;
- Acclorite stays independently testable;
- zero Realmheart dependency in Acclorite;
- easy development;
- easy debugging.

## 18.3 Possible future library

Only if process startup or tighter integration becomes important:

```text
libacclorite
    ├── acclorite CLI
    └── Realmheart
```

Do not build `libacclorite` before there is a demonstrated need.

## 18.4 Future widget ideas

Possible Realmheart visualizations:

- compact best-match card;
- expandable alternatives;
- installed / available badges;
- documentation button;
- learning mode panel;
- Acclorite search inside Realm Action;
- animated confidence / category transitions;
- native result history.

None of these belong in the core CLI implementation.

---

# 19. LLM Strategy — Much Later

The LLM is not an early milestone.

## 19.1 Default behavior

A normal Acclorite installation works with AI disabled.

Merely detecting Ollama should not silently route normal queries through it.

## 19.2 Possible modes later

```text
off
fallback
explicit
```

Potential semantics:

### off

Never use an LLM.

### fallback

Only use an enabled local model when deterministic confidence is below a threshold.

### explicit

Only use AI when the user explicitly requests it for a query.

Exact UX can be decided later.

## 19.3 LLM responsibilities

Allowed:

- infer the same structured query frame used by the deterministic recognizer;
- normalize extreme slang;
- produce concepts;
- identify alternative interpretations;
- convert a long vague request into structured search terms.

Not allowed:

- become the authoritative tool database;
- invent flags;
- invent package names;
- decide installation commands from memory;
- bypass the deterministic retrieval engine.

## 19.4 Ollama

Potential optional backend.

Acclorite should communicate with a local Ollama instance if the user has configured AI support.

Model choice must be benchmarked.

Do not hard-code a model solely because it is small.

---

# 20. Evaluation Corpus

This should be created very early.

Example file:

```text
tests/data/queries.json
```

Each case contains:

- raw query;
- expected query frame;
- expected concepts;
- acceptable top candidates;
- unacceptable candidates;
- ambiguity expectation;
- tags;
- difficulty.

Example:

```json
{
  "query": "find duplicate files",
  "frame": "Discover",
  "acceptable_top3": ["fdupes", "rdfind", "czkawka-cli"],
  "concepts": ["duplicate files", "file comparison"],
  "ambiguous": false
}
```

Variants should include:

```text
find duplicate files
find identical files
same file twice
remove duplicate shit
which files are copies
duplcate fls
I think I downloaded the same stuff multiple times
```

## 20.1 Query categories

Build coverage for:

- filesystem;
- text search;
- storage;
- networking;
- processes;
- system monitoring;
- permissions;
- archives;
- compression;
- package discovery;
- image conversion;
- video/audio utilities;
- comparison;
- synchronization;
- shell navigation;
- logs;
- services;
- hardware inspection.

## 20.2 Difficulty tiers

### Easy

Direct documented terms.

```text
find duplicate files
```

### Medium

Synonyms / informal wording.

```text
what is eating my disk
```

### Hard

Vague but deterministic intent may still succeed.

```text
that terminal thing that learns which folders I go to
```

### Absurd

Intended future LLM fallback territory.

```text
there was this weird shit I used months ago where I typed part
of something and it sort of knew where I wanted to jump based
on where I'd been before
```

This corpus tells us whether the deterministic engine is actually earning its existence.

---

# 21. Metrics

Track at minimum:

## Retrieval quality

- Top-1 relevance;
- Top-3 relevance;
- Mean reciprocal rank;
- incorrect confident answer rate;
- correct ambiguity detection;
- query-frame accuracy;
- false confident frame rate;
- query coverage without AI.

## Performance

- cold startup time;
- warm startup time;
- query latency;
- per-stage query latency (index, live package source, enrichment, ranking, confidence);
- index build time;
- incremental refresh time;
- peak RAM;
- index size.

## Robustness

- missing man-db;
- empty PATH entries;
- package backend unavailable;
- stale index;
- corrupted index;
- malformed UTF-8 input;
- weird terminal environments;
- no network;
- minimal container.

## AI later

- percentage of queries escalated;
- improvement after escalation;
- unnecessary escalation rate;
- local model startup latency;
- model RAM usage.

Primary long-term bragging metric:

> Percentage of evaluation queries correctly solved without invoking an LLM.

---

# 22. Testing Strategy

## 22.1 Unit tests

Target:

- tokenizer;
- normalizer;
- synonym expansion;
- phrase matching;
- query-frame recognition;
- question-word override cases (`what is using`, `where do I edit`, etc.);
- explicit-vs-inferred frame metadata;
- entity extraction;
- fuzzy scoring;
- score combination;
- candidate merging;
- JSON serialization;
- capability detection.

## 22.2 Fixture tests

Use frozen example outputs from:

- man pages;
- package metadata;
- PATH listings;
- Arch package queries.

Do not make every test depend on the host machine.

## 22.3 Integration tests

Run against a real Arch/CachyOS environment for the first-class backend.

## 22.4 Portable tests

Use containers / CI images later for:

- Arch;
- Ubuntu/Debian;
- Fedora;
- minimal generic Linux cases.

Other distro backends are added only when implemented.

## 22.5 Regression corpus

Every embarrassing wrong result should become a permanent test case.

This is critical.

If:

```text
"find what uses port 8080"
```

once ranks an unrelated command at #1, that failure joins the corpus forever.

---

# 23. Repository Structure

Proposed initial layout:

```text
acclorite/
├── CMakeLists.txt
├── LICENSE
├── README.md
├── docs/
│   ├── specification.md
│   ├── implementation-plan.md
│   ├── architecture.md
│   └── ranking.md
│
├── include/
│   └── acclorite/
│       ├── core/
│       │   ├── query.hpp
│       │   ├── candidate.hpp
│       │   ├── result.hpp
│       │   └── capability.hpp
│       ├── query/
│       │   ├── normalizer.hpp
│       │   ├── frame.hpp
│       │   ├── vocabulary.hpp
│       │   └── entities.hpp
│       ├── index/
│       │   └── index.hpp
│       ├── sources/
│       │   ├── source.hpp
│       │   ├── path_source.hpp
│       │   └── man_source.hpp
│       ├── packages/
│       │   ├── backend.hpp
│       │   └── pacman_backend.hpp
│       ├── ranking/
│       │   └── ranker.hpp
│       └── output/
│           ├── terminal.hpp
│           └── json.hpp
│
├── src/
│   ├── main.cpp
│   ├── core/
│   ├── query/
│   ├── index/
│   ├── sources/
│   ├── packages/
│   ├── ranking/
│   └── output/
│
├── data/
│   ├── vocabulary.json
│   ├── phrases.json
│   └── quotes.tsv
│
├── tests/
│   ├── unit/
│   ├── fixtures/
│   ├── integration/
│   └── data/
│       └── queries.json
│
└── experiments/
    ├── ranking/
    ├── retrieval/
    ├── embeddings/
    └── models/
```

Do not create every directory before it is needed merely to satisfy the diagram.

### 23.1 Hidden lore/quote system status

The specification's hidden lore feature is implemented as a presentation-only side channel. A bundled local quote corpus is installed with Acclorite; ordinary interactive human terminal queries have a sparse random chance of receiving one quote after the normal answer, while the undocumented standalone `--lore` command selects one on demand. Machine JSON, capability output, Doctor, maintenance commands, ranking, and exit semantics are unaffected. The feature performs no network access and does not explain its thematic origin.

The shipped corpus is derived from the maintainer's larger Volume 1-10 master dataset rather than vendoring that master file. `tools/build_lore_corpus.py` validates the master, preserves quote text and attribution exactly, selects entries independently present in both source datasets with balanced volume/type coverage, retains the cross-confirmed Regis set, and appends explicitly declared manual project overrides without pretending they came from the master dataset. The generated runtime TSV is therefore small, auditable, and reproducible while the full research/collection dataset stays outside the release payload.

---

# 24. Implementation Milestones

The milestone numbers are intentionally small and practical.

# Milestone 0 — Skeleton

Goal:

> Produce a clean native executable and establish architectural boundaries.

Implement:

- repository;
- CMake;
- C++20 target;
- basic CLI;
- internal result structures;
- terminal renderer shell;
- JSON renderer shell;
- test framework;
- version command.

Success:

```bash
./acclorite "hello"
```

parses the query and returns a placeholder structured result through the real rendering pipeline.

No search engine yet.

---

# Milestone 1 — Generic command discovery

Goal:

> Acclorite knows which commands exist on the current machine.

Implement:

- PATH scanning;
- executable deduplication;
- basic metadata model;
- basic index creation;
- SQLite/FTS5 integration;
- exact command search;
- primitive lexical ranking.

Queries:

```text
grep
tar
find
systemctl
```

should find their commands reliably.

---

# Milestone 2 — Local documentation

Goal:

> Acclorite understands what installed commands do.

Implement:

- capability detection for `man`, `apropos`, `whatis`;
- man metadata extraction;
- descriptions;
- source provenance;
- FTS indexing;
- documentation references.

Queries begin becoming intent-oriented:

```text
archive files
search text
list processes
show disk usage
```

Still no custom synonym system required for success.

---

# Milestone 3 — Deterministic natural-language engine

Goal:

> Make ordinary human wording work without AI.

Implement:

- normalization;
- stopword handling;
- fuzzy matching;
- RapidFuzz integration;
- curated synonym vocabulary;
- phrase expansion;
- deterministic Query Frame Recognizer;
- Discover / Explain / Locate / Inspect / Modify / Compare / Diagnose frames;
- explicit-vs-inferred frame confidence;
- frame-aware ranking biases;
- frame-target/entity extraction;
- entity-first Explain/Compare routing;
- bounded filesystem location results for Locate;
- relation inference for process ownership/resource-hog queries;
- simple entity detection;
- query variants;
- multi-signal ranking.

Target queries:

```text
what is eating my disk
find stuff inside files
which app uses this port
what is using port 8080
where do I see network connections
where is ssh config
what is ripgrep
make a tar thing
see what process is hogging RAM
```

This is the first milestone where Acclorite starts feeling like Acclorite.

---

# Milestone 4 — Arch first-class backend

Goal:

> Make Acclorite substantially better on Arch.

Implement:

- distro detection;
- package backend interface;
- PacmanBackend;
- package descriptions;
- installed package state;
- repository availability;
- candidate merging with package metadata.

Optional enrichment if present:

- expac;
- pkgfile.

Target capability:

```text
discover useful tools that are not currently installed
```

Example:

```text
"interactive disk usage explorer"
```

may discover `ncdu` even when absent.

---

# Milestone 5 — Serious ranking

Goal:

> Stop relying on vibes.

Implement:

- expanded query corpus; **initial v1 corpus implemented in 0.0.21-dev (25 Arch-focused cases)**;
- benchmark harness; **implemented in 0.0.21-dev as optional Python evaluation tooling over the real JSON CLI**;
- ranking diagnostics; **implemented**;
- candidate score breakdown; **implemented**;
- query-frame diagnostics; **implemented**;
- confidence model; **implemented, calibration remains benchmark-driven**;
- ambiguity detection; **implemented, clarification families remain intentionally small**;
- regression tests; **implemented continuously; benchmark failures are separate from unit-test failures**;
- Python ranking experiments; **evaluation boundary established; additional weight experiments remain future work**.

Tune:

- BM25 field weights;
- fuzzy weights;
- installed bonus;
- exactness bonus;
- package/source quality;
- phrase confidence;
- candidate merging;
- recommendation-prior strength;
- evidence-quality bonuses;
- specialization penalties and their context-sensitive removal.

Produce a repeatable benchmark report.

### 0.0.21 benchmark contract

The first benchmark harness lives outside the runtime in `benchmarks/` and invokes the compiled
Acclorite executable through `--json`. This deliberately tests the same public behavior a future
Realmheart client or external integration observes instead of linking benchmark code into ranking
internals.

Corpus assertions are behavioral and may specify acceptable-equivalent winners, top-3 presence,
forbidden winners, Query Frame, ambiguity state, minimum confidence, extracted targets, Locate
results, and deterministic clarification IDs. Do **not** benchmark exact floating-point scores;
those are tunable implementation details.

The report must expose at least:

```text
query-level pass rate
assertion-level pass rate
top-1 accuracy
top-3 hit rate
query-frame accuracy
ambiguity-state accuracy
end-to-end latency p50/p95/max
internal-search latency p50/p95/max (when profiling is enabled)
per-stage latency p50/p95/max (when profiling is enabled)
```

A warm-up query is discarded by default so automatic stale-index refresh is not counted as normal
hot-query latency. Cold-start/index-rebuild performance should be measured separately.

Benchmark failures are **not unit-test failures** during tuning. `--strict` is the explicit release/CI
gate that converts corpus failures into a non-zero exit code. This keeps architectural invariants in
C++/harness unit tests while allowing the corpus to honestly expose known ranking weaknesses.

The initial corpus is intentionally modest and versioned. Grow it from real failure reports rather
than generating thousands of low-quality synthetic paraphrases. Record corpus changes alongside
ranking changes so accuracy improvements cannot be manufactured by silently weakening expectations.

### v0.1.0 release gate / source-cache latency stabilization

The real Arch/CachyOS `0.0.29-dev` run reached the first frozen correctness gate: **25/25 query cases and 65/65 assertions (100%)** on `acclorite-core-arch-v1`. End-to-end latency was p50 ~1.55 s, p95 ~2.81 s, max ~3.10 s. Profiling showed that ranking and pkgfile were no longer dominant: `source:arch-packages` remained ~711 ms p50 and `source:index` ~404 ms p50. The user chose this stabilized engine as the basis for **v0.1.0**, with subsequent feature work moving to the v0.2.x line.

For v0.1.0, freeze ranking behavior and optimize only those knowledge-source hot paths:

- `PacmanSource` persists a local snapshot of already-synchronized ALPM package metadata when `expac` is available. Cache freshness is keyed to cheap metadata fingerprints of pacman's local `sync/*.db` files. Missing/stale cache performs one read-only `expac -S` enumeration and atomically replaces the snapshot; fresh queries filter the local snapshot without launching expac/pacman. Cache failure never removes discovery: live `expac -Ss`, then `pacman -Ss`, remain fallbacks. No repository refresh or install operation is introduced.
- `IndexSource` gains a two-tier freshness strategy. Every-query readiness checks use cheap fingerprints of PATH directories, desktop roots, and the immediate manual/cache directory structure. The existing deeper fingerprint remains the diagnostic/safety authority and is run by `doctor`, during migration from old indexes, and periodically. Older indexes with valid deep fingerprints are seeded with quick-fingerprint metadata in place instead of being rebuilt.
- CMake project version becomes the single source of truth for the CLI version string.

A synthetic 25,000-package cache fixture with a deliberately 700 ms expac enumeration measured `source:arch-packages` at ~740 ms cold and ~27 ms warm while preserving the same source provenance/scoring path. Real-machine v0.1.0 validation must rerun the unchanged `corpus-v1.json` gate and compare source timings.

Release-candidate validation exposed a cache-integrity failure in the first v0.1.0 attempt. A non-hermetic expac parser unit test could run against the host's real pacman sync fingerprint while `PATH` pointed at a fake expac fixture, allowing the fixture's tiny package catalog to be written to the user's production `~/.cache/acclorite/arch-packages.tsv`. The subsequent test-isolation hotfix prevented future contamination but could not invalidate an already-poisoned cache, which caused repository-backed benchmark candidates such as `pdfgrep`, `recoll`, `rdfind`, `fclones`, and `jdupes` to disappear while producing deceptively fast package-source timings.

The v0.1.0 cache-integrity hotfix therefore advances the Arch snapshot format to `acclorite-arch-cache-v2` and includes the resolved expac executable identity (path, mtime, and size) in the catalog fingerprint in addition to pacman's sync database metadata. Old v1 snapshots are rejected automatically, so users do not need manual cache deletion, and a cache produced by a PATH-shadowed/fake expac cannot later be trusted by the real executable. Dedicated regressions verify both v1 schema rejection and invalidation when the expac producer changes while pacman's sync metadata remains unchanged. Unit tests keep persistent Arch caching disabled globally except for isolated cache-specific fixtures.

After the cache-integrity hotfix, the real CachyOS v0.1.0 benchmark restored the frozen gate to **25/25 cases and 65/65 assertions**, with end-to-end p50 ~1.15 s / p95 ~2.83 s. The result also showed that correctness alone was not enough to close v0.1.0: `source:arch-packages` still cost ~506 ms p50 because every process reparsed and scanned the entire TSV catalog, while `source:index` still cost ~460 ms p50.

The final v0.1.0 source-latency pass therefore changes implementation, not ranking:

- On SQLite-enabled builds, the Arch snapshot becomes an FTS5 database (cache format v3) with the same pacman-sync + expac-producer fingerprint. Warm queries execute bounded indexed retrieval and semantic rescoring only on matching package rows. The no-SQLite build retains the versioned TSV path. Cache rebuilds use the same indexed retrieval immediately after writing so cold and warm query semantics do not diverge.
- OSA fuzzy similarity keeps the exact same distance definition but replaces the per-comparison `(m+1)*(n+1)` allocation with three reusable row buffers. Relevance scoring also rejects a fuzzy comparison before OSA when the word-length difference mathematically makes the configured threshold unreachable.

Controlled same-container fixtures measured a warm 25,000-package Arch lookup at a median ~282.6 ms on the v2 TSV implementation versus ~11.0 ms with the v3 FTS5 snapshot. A 200,000-pair randomized old-vs-new OSA microbenchmark produced the exact same aggregate similarity checksum (`63431.978042940049`) while reducing runtime from ~1.09-1.16 s to ~81.8-85.5 ms. These are engineering fixtures, not promises for CachyOS absolute latency; the unchanged real-machine corpus remains the release authority.

Final real-machine v0.1.0 validation on CachyOS preserved **25/25 cases and 65/65 assertions** while reaching end-to-end **~359 ms p50 / ~1.29 s p95 / ~2.71 s max**. Internal search measured ~350 ms p50. The optimized Arch package source fell to ~29 ms p50 and the local semantic index to ~167 ms p50. This closes the v0.1.0 release gate: ranking correctness and source-level latency are frozen before v0.2.x feature work.

`corpus-v1.json` is frozen at v0.1.0. Future v0.2.x ranking work must add a separate versioned/adversarial corpus instead of rewriting v1 to fit new behavior.

### 0.0.27 performance profiling / bounded enrichment concurrency

The real Arch/CachyOS `0.0.27-dev` profiling benchmark confirmed that the first performance pass successfully removed `pkgfile` as the main bottleneck: `enricher:pkgfile` fell to about 62 ms p50 / 201 ms p95. The dominant measured ranking bucket remained `ranking-finalize` at about 517 ms p50, 3.47 s p95, and 4.12 s max, while two long queries still hit the 20-second harness timeout. Code audit showed that query-relative specialization analysis was being executed from the sort comparator itself, causing repeated descriptive-evidence lowercasing/tokenization and role checks O(N log N) times; preference scoring then repeated the same detector again.

`0.0.29-dev` resumes correctness work only after the real `0.0.28-dev` CachyOS benchmark restored all timeout cases and measured 24/25 cases, 64/65 assertions with p50 ~1.73 s and p95 ~2.57 s. The sole remaining failure is generic folder compression selecting `fsarchiver`. Rather than stacking another negative exception, semantic policy now recognizes positive **generic archive front-door role evidence**: a terse synopsis such as `archiving utility` or `file archiver` can satisfy the otherwise implicit file/directory compatibility of an explicit folder-compression task and receive a strong-tier floor. The floor is applied before all query-relative specialization penalties, so filesystem/partition scope, backup/deployment, comparison, and development-library roles still demote narrower candidates. The regression intentionally gives a filesystem archiver stronger raw fit than a generic archive front door and requires the generic role to win without naming any real command.

The benchmark harness simultaneously advances to report schema 3 for benchmark integrity and failure forensics. Timeouts/errors now count every assertion declared by the corpus instead of shrinking the denominator, and failed non-timeout cases automatically receive an untimed explain-ranking rerun with top-candidate semantic fit/tier, final utility, provenance, and semantic-adjustment IDs. The corpus itself is unchanged.

`0.0.28-dev` makes ranking classification O(N) per phase and sorting numeric. Each candidate gets an internal query-relative ordering fit/tier cache populated before pre-enrichment selection and repopulated after enrichment/final merge. Pre-enrichment selection uses `partial_sort` because only the first 24 candidates are enriched. The cache is deliberately SearchEngine-internal in semantics: public query-aware fit computation remains uncached so a candidate can safely be evaluated against a different query. Preference scoring reuses one specialization scan for both semantic and utility effects. Profiling now separates `ranking-prefilter`, `ranking-remerge`, `ranking-preferences`, and `ranking-sort` in addition to the aggregate finalize stage. A same-container v0.0.27-vs-v0.0.28 comparison reduced internal latency for representative long queries by roughly 3-4x while preserving tests and ranking outputs.

The real Arch/CachyOS `0.0.26-dev` benchmark reported **22/25 cases (88.0%)**, but the apparent regression was dominated by two benchmark timeouts: `search inside files recursively` and `convert images` both exceeded the 20-second harness limit and therefore produced no result. The sole actual ranking failure remained generic folder compression. Latency had become high enough to contaminate correctness measurements, so `0.0.27-dev` freezes ranking changes and makes performance observable.

The largest obvious hot-path multiplier is Arch binary enrichment: `SearchEngine` may enrich up to 24 realistic repository candidates, and `PkgfileEnricher` previously launched every `pkgfile --list --binaries` lookup serially. Derivative-repository fallback may launch a second lookup for a candidate. `CandidateEnricher` therefore gains a batch API; the default remains deterministic sequential behavior, while `PkgfileEnricher` implements bounded concurrency with four workers by default (`ACCLORITE_ENRICH_WORKERS`, clamped to 8). Candidate slots are independent, and sorting/merging still occurs only after every enrichment completes, so scheduling cannot change result order. A controlled 24-candidate fixture measured approximately **2.45 s with one worker vs 0.62 s with four workers** using identical pkgfile semantics.

Concurrent subprocess execution uses `posix_spawnp`; do not fork from multiple C++ threads and then perform heap/vector work in the child before `exec`. The generic process helper retains the same captured-stdout/quiet-stderr contract. Threads are an implementation detail of bounded enrichment only; Acclorite remains a short-lived CLI with no daemon/background service.

`--profile` is an observational search diagnostic. Search JSON schema **13** adds an optional `timing` object with `total_ms` and named per-stage timings. Without profiling, the field is `null`. The benchmark harness requests profiling by default and benchmark report schema **3** adds internal-search plus stage p50/p95/max summaries, preserves declared assertions across timeout/error cases, and attaches an untimed explain-ranking forensic snapshot to failed non-timeout cases. Expected stage names include `source:index`, `source:arch-packages`, `enricher:pkgfile`, `ranking-finalize`, `confidence`, and Locate source timing when applicable. This instrumentation must never change candidate count, ordering, scores, confidence, source queries, or system mutation behavior.

The next real-machine report should be used to answer two questions separately:

1. Did the two 20-second timeout cases return under the existing 20-second gate?
2. Which measured stage now dominates p50/p95 on CachyOS?

Do not resume ranking calibration until performance is stable enough that benchmark failures represent ranking behavior rather than process timeout noise.

### 0.0.24 merged descriptive evidence / role-classification integrity

The real Arch/CachyOS `0.0.23-dev` benchmark remained at **23/25 cases (92.0%)** and **63/65 assertions (96.9%)**, but the two reported failures were not equivalent. `full text search` now chose the user-facing `recoll` with clear confidence after development-library demotion; the corpus still expected the old competitive state, so that expectation is corrected to `clear` while the acceptable-winner set remains unchanged. The sole genuine ranking failure remained generic folder compression selecting `fsarchiver`.

The `0.0.24-dev` real CachyOS benchmark reached **24/25 cases (96.0%) and 64/65 assertions (98.5%)**. The sole remaining failure was still `compress a folder -> fsarchiver`. Investigation showed the preserved-description architecture was necessary but not sufficient: role classification still depended too heavily on literal `backup` / `deployment` vocabulary. `0.0.25-dev` adds a generic **filesystem/partition-scope role constraint**. Metadata such as `filesystem archiver - save a filesystem to a compressed archive` is enough to identify a storage-management scope broader than ordinary folder compression. Explicit filesystem/partition intent removes the constraint. The rule is command-agnostic and keeps the benchmark corpus unchanged.

The `0.0.25-dev` real benchmark again measured **24/25 cases and 64/65 assertions**, but the final failure changed from `fsarchiver` to `diffoscope`. That mutation exposed a distinct issue: semantic coverage of the same objects (files/directories/archives) does not imply compatibility with the requested **operation direction**. `0.0.26-dev` adds a query-relative operation-role guardrail: explicit archive-transform intent downranks candidates whose evidence identifies comparison/diff as their front-door operation, while explicit comparison/inspection intent removes that mismatch. This remains independent from domain specialization and filesystem-scope constraints, keeps the benchmark corpus unchanged, and advances the ranker toward predicate/role semantics without command-specific rules.

The `fsarchiver` failure exposed a data-model issue: candidate merging retained only one preferred display summary. A local man/PATH synopsis could therefore replace or overshadow repository text containing role-critical phrases such as `backup` / `deployment`, even though both evidence sources remained present in provenance. `0.0.24-dev` separates **display summary** from **descriptive ranking evidence**. Candidates retain unique descriptions from merged sources in internal `descriptive_evidence`; query-relative specialization and front-door role classification inspect that combined text, while terminal/JSON output continues to show one concise summary.

This is a general source-merge invariant, not an `fsarchiver` exception: ranking-relevant descriptive metadata must not be destroyed merely because another source supplies a better display synopsis. A regression reproduces the local-man + package-description merge and requires the package role constraint to survive it. Search JSON remains schema 12 because the retained descriptions are internal ranking evidence, not a new public result field.

### 0.0.23 front-door suitability / tool-role constraints

The real Arch/CachyOS `0.0.22-dev` rerun improved the corpus from **19/25 (76.0%) to 23/25 (92.0%)** and from **54/64 (84.4%) to 63/65 (96.9%)** assertions. Only two archive cases remained: generic folder compression selected `fsarchiver`, and `tar.gz` extraction selected the repository package `haskell-tar-conduit`.

Both failures share a higher-level distinction that plain semantic text matching cannot express: **describing an operation is not the same as being the user-facing front door for that operation**. `0.0.23-dev` therefore extends query-relative specialization with tool-role constraints. Backup/deployment-oriented candidates are discounted for generic compression/extraction discovery unless backup/filesystem intent is explicit. Development-library packages are discounted for generic tool discovery unless programming/library intent is explicit. Exact entity lookup continues to bypass all specialization penalties.

This remains a general ranking rule, not a list of command-specific demotions. The benchmark corpus is unchanged so the next real-machine rerun measures the engine against the same 25 cases / 65 assertions.

### 0.0.22 first benchmark-driven tuning pass

The first real Arch/CachyOS `0.0.21-dev` benchmark established a baseline of **19/25 query cases
(76.0%)** and **54/64 assertions (84.4%)**. The six failing cases were not treated as six command
whitelists; they were reduced to reusable language/ranking causes:

- relational `using` had been discarded as a stopword in consumption/ownership inspection queries;
- quantity words (`all`, `two`, etc.) were incorrectly becoming semantic subjects;
- task comparison (`compare two folders`) was being confused with entity-first Compare;
- compound archive formats such as `tar.gz` were opaque to the concept layer;
- generic requests were vulnerable to subsystem-specific helpers whose metadata happened to contain
  attractive verbs (`netlink`, `hwloc` topology, font/audio comparators, filesystem-format tools);
- folder compression needed a weak archive/container relation so generic archive creation could be
  distinguished from unrelated domain-specific compression.

`0.0.22-dev` therefore adds relation-preserving query terms, quantity filtering, task-vs-entity
Compare routing, archive-format concepts, low-weight composition for compression/container intent,
and additional **query-relative** specialization families. Specialized tools are never globally
banned: naming the tool or requesting its domain removes the specialization penalty.

The corpus itself is strengthened rather than weakened: `compare two folders` now also asserts the
Discover frame. Rerun the same corpus after this pass and compare against the 76.0%/84.4% baseline.

Latency from the first real report (roughly 2.8 s p50 and 9.8 s p95) is recorded as a separate
performance problem. Correctness tuning remains the focus of `0.0.22`; performance gets a dedicated
measured pass so ranking fixes and latency work are not conflated.

---

# Milestone 6 — Verified examples and learning links

Goal:

> Results become immediately useful without becoming command hallucinations.

### v0.2.0 first slice — post-ranking local man guidance

The real CachyOS v0.1.0 release gate closed at **25/25 cases, 65/65 assertions**, with end-to-end p50 ~359 ms / p95 ~1.29 s after the final source-latency pass. That release is now frozen; v0.2.x begins Milestone 6 without changing ranking.

`v0.2.0` introduces a `GuidanceProvider` boundary that runs only after ranking and result truncation. Guidance returns structured, source-backed `UsageExample` and `LearningResource` records and is explicitly observational: it must not influence candidate recall, semantic fit, score, confidence, or ordering. Ordinary discovery enriches only the best match; Compare may enrich both exact targets. Profiling records provider cost separately.

The first provider is `ManGuidanceProvider`. It only acts when candidate provenance already proves local man-page evidence, always exposes `man <tool>` as a verified learning resource, and conservatively extracts up to two command-looking lines from an explicit EXAMPLE / EXAMPLES section. Terminal overstrike/ANSI formatting and a leading shell prompt may be removed, but syntax is otherwise source text. If no verified command line is found, the UI reports that absence instead of fabricating one. Search JSON advances to schema 14 with per-candidate `examples[]` and `learning_resources[]` plus provenance.

Later v0.2.x slices can add Info, local TLDR/tealdeer caches, and curated verified metadata through this same provider boundary.

### v0.2.1 second slice — local Info fallback guidance

`v0.2.1` adds `InfoGuidanceProvider` after man guidance. It verifies an exact local topic with `info --where <tool>` before exposing `info <tool>` as a learning resource. This existence probe remains inside the post-ranking guidance layer and is never appended to candidate ranking provenance.

Info is lower priority than already-verified man examples. When the candidate already carries a verified example from an earlier provider, Info records only the learning resource and skips full manual rendering. Otherwise it may render the selected local Info node with `info --output=- <tool>` and conservatively extract at most two command-looking lines from an explicit Example / Examples section. No inferred or synthesized syntax is allowed. Profiling records the provider separately as `guidance:info`; search JSON remains schema 14 because the existing structured guidance contract already represents Info provenance.

### v0.2.2 Info miss fast path

Real CachyOS validation kept the frozen corpus at **25/25 cases and 65/65 assertions**, but exposed `guidance:info` as the largest median stage at roughly **231 ms p50**, with end-to-end latency rising to roughly **812 ms p50**. The one-off `ls` hit was much cheaper, indicating the dominant cost was negative topic lookup rather than successful Info rendering.

`v0.2.2` therefore adds a cheap exact-topic plausibility filter before `info --where`. Standard and `INFOPATH` Info directories are inspected for an exact command-labelled `dir` menu entry or an exact dedicated `<command>.info*` file. Obvious misses return immediately; plausible positives still require the existing `info --where` verification and retain the same source-priority example extraction. No ranking or JSON semantics change. Follow-up real CachyOS validation kept **25/25 cases and 65/65 assertions** while reducing `guidance:info` to roughly **2.7 ms p50 / 16.1 ms p95** and end-to-end latency to roughly **569 ms p50 / 1.63 s p95**.


### v0.2.3 local TLDR/tealdeer cache guidance

`v0.2.3` adds `TldrGuidanceProvider` below man and Info without adding another subprocess to the common path. The provider never calls `tldr` and never performs an update/network operation. It locates conventional tealdeer and TLDR cache roots directly, honors tealdeer's configured `directories.cache_dir`, and performs exact `<command>.md` lookups only for local Linux/common pages. An exact filename is followed by an exact first-heading identity check before the page can emit guidance.

If a page is verified, Acclorite records a `tldr` learning resource and uses the local page as a source for at most two inline-code command examples only when higher-priority providers found none. Placeholders are kept exactly as authored. The provider may guide repository candidates that are not installed because a local TLDR cache can legitimately document them; it still requires CLI capability and never feeds that page into retrieval/ranking. Profiling records `guidance:tldr`. Search JSON remains schema 14.

For this provider, `verified` means the emitted text is traceable to the exact local cache file. It does not elevate community-maintained TLDR content to the authority level of upstream manuals and is not a safety endorsement of the documented command. Tealdeer custom pages and patches are excluded from `tldr` provenance in this slice because user-authored extensions need their own provenance semantics.

### v0.2.4 curated verified metadata

`v0.2.4` adds `CuratedGuidanceProvider` as the final Milestone-6 fallback. Curated guidance is a small bundled, data-driven corpus in `data/curated-guidance.tsv`; it is not hard-coded into ranking logic and is never used as semantic evidence. Provider construction loads and validates the bounded file once, and query-time lookup is exact by command identity with no subprocess or network activity.

Every accepted row carries a stable id, an upstream HTTP(S) source reference, and an ISO verification date. Malformed/undated records are ignored. Learning resources may be added even when a higher-priority example already exists, while curated examples are emitted only when man, Info, and TLDR produced none. The initial corpus is deliberately tiny and must not become a manually maintained Linux encyclopedia. Version-scoped syntax is deferred until applicability can be represented explicitly; unstable syntax should be omitted rather than guessed.

Search JSON advances to schema 15 by adding `verified_by` and `verified_on` to examples/resources. Existing runtime-verified local providers leave those fields empty. Bundled curated records set `verified_by = acclorite-project` and expose the review date; override corpora use a distinct local provenance identity, allowing downstream clients to distinguish “verified from a local source now” from “reviewed and bundled by Acclorite on a known date.” Profiling records this provider as `guidance:curated`.

Implement source-priority extraction from:

1. local docs;
2. man examples when available;
3. info pages;
4. optional TLDR cache;
5. curated verified examples.

Display provenance internally.

Never fabricate a command example merely to fill the UI.

---

# Milestone 7 — Stable machine interface

Goal:

> Freeze enough JSON semantics for external clients.

Implement:

- schema version;
- documented JSON fields;
- non-interactive behavior;
- stable exit codes;
- capability JSON;
- ranking confidence output.

This unlocks safe Realmheart experimentation later.

### v0.2.5 machine-interface foundation

`v0.2.5` establishes the first Milestone-7 compatibility baseline without changing retrieval, ranking, confidence, or guidance policy. Search stays at schema 15 and Doctor at schema 1, but schema constants and process exit-code semantics move into a shared machine-interface contract.

Stable process status is: `0` success, `1` valid search with no result, `2` invocation/usage error, `3` Acclorite operational failure, and `4` Doctor completed and found a fundamental error. This deliberately separates “nothing matched” from “Acclorite failed,” and separates a broken diagnosed environment from failure to run Doctor itself.

`--capabilities` introduces capability schema 1. It is JSON-first, non-interactive, and local-only; it publishes the Acclorite version, current search/Doctor schema versions, offline-runtime guarantee, command/output features, SQLite build support, cheap local integration detection, and the exit-code mapping. Detection may inspect executable presence or bounded local cache/data existence but must not launch subprocesses, rebuild indexes, update metadata, or touch the network. Deep readiness belongs to `doctor`.

CLI parsing is hardened before clients depend on it: unknown options return usage error, `--reindex` is standalone, and `--` explicitly terminates option parsing. A Python end-to-end contract test invokes the compiled executable itself and verifies capability JSON, usage failures, successful/no-result search status, Doctor diagnostic status, and forced operational failure. Compatibility rules are documented in `docs/machine-interface.md`; clients must ignore unknown fields, while removal/rename/type/meaning changes require the relevant schema version to advance.

### v0.2.6 schema-contract freeze

`v0.2.6` completes Milestone 7 without changing search schema 15, Doctor schema 1, or capability schema 1. Instead it freezes the already-published semantics with a second end-to-end Python contract suite that validates required fields, JSON types, enum domains, nullable fields, and process-status pairing against the compiled executable.

The schema-contract suite is structural rather than byte-for-byte: additive optional fields remain legal, while required field removal/rename, type or nullability drift, enum drift, and schema/exit-code mismatches fail tests. Hermetic fixtures cover success/no-result search, profiled timing, ranking explanations, guidance records, locations, Discover/Explain/Locate/Inspect/Modify/Compare/Diagnose/Unknown frames plus an explicit ambiguous-result fixture, and healthy/broken Doctor documents. Unstable values such as exact scores, timing magnitudes, temporary paths, and ranking weights are deliberately not frozen.

With this slice, search schema 15 is the first frozen public search contract for downstream clients. Future breaking changes must be intentional and accompanied by the relevant schema-version increment; compatible additive fields remain allowed because clients are required to ignore unknown fields.

---

# Milestone 8 — First useful public release

Tentative version:

```text
v0.1
```

Required:

- strong generic core;
- strong Arch backend;
- useful no-AI results;
- index rebuild/recovery;
- terminal UX;
- JSON output;
- tests;
- benchmark corpus;
- documentation;
- AUR packaging.

### v0.2.7 first slice — atomic index rebuild and last-known-good recovery

The first Milestone-8 release-hardening slice closes the destructive-cache-rebuild gap. The previous implementation removed `index.db` before collecting replacement records, which meant an empty/failed refresh could destroy a perfectly usable prior snapshot. `v0.2.7` instead serializes rebuilders with a persistent sibling lock file, builds into `index.db.rebuild`, writes and commits the complete snapshot there, checkpoints the staging WAL and returns the staged database to a self-contained rollback-journal form, validates schema/content through a read-only reopen, and only then atomically renames the staged database over the live path. A failed rebuild cleans staging artifacts and never removes the prior promoted database.

Failed refresh is non-destructive: if a replacement cannot be produced, the previous promoted database remains in place for diagnosis and later recovery rather than being erased first. `probe()` / Doctor continue to expose its stale state after source drift. Regression coverage forces a rebuild failure after a valid snapshot exists and requires the live database to remain structurally valid and byte-identical. Search/Doctor/capability schemas and ranking behavior do not change.

### v0.2.8 second slice — release-grade terminal UX

`v0.2.8` keeps the frozen machine interface untouched and hardens the human-facing terminal surface. Search output is reorganized into a compact hierarchy: explicit frame/target context, one primary candidate card with aligned metadata, source-backed guidance, confidence/clarification, and bounded alternatives. No-result output explains the miss and offers concrete recovery/query suggestions rather than only stating that nothing matched. Raw ranking internals remain opt-in through `--explain-ranking`.

Doctor output becomes a health dashboard rather than a flat dump. It leads with `healthy` / `attention needed` / `broken`, summarizes ready/warning/optional/error check counts, groups checks by subsystem, makes read-only behavior explicit, and visually subordinates details and hints. Reindex success is concise; reindex failure explicitly tells the user that the last usable index was preserved and points to Doctor. `--help` is expanded into usage, search/general options, examples, and the offline/local-first operating model.

ANSI is presentation-only and terminal-aware: color is enabled only for TTY output, disabled for redirected/captured output and `TERM=dumb`, and must honor `NO_COLOR`. Default renderer construction remains plain so tests/embedding callers never receive escape sequences accidentally. A dedicated pseudo-terminal end-to-end test freezes the color policy and core human hierarchy without turning exact prose/layout into a machine compatibility contract. Search schema 15, Doctor schema 1, capabilities schema 1, exit codes, ranking, and guidance policy are unchanged.


### v0.2.9 third slice — license, install layout, and AUR packaging

`v0.2.9` closes Milestone 8's remaining public-release distribution work. The project adopts `GPL-3.0-or-later`, installs its binary, bundled curated guidance, user documentation, and `acclorite(1)` manual through CMake, and ships an AUR-ready stable `PKGBUILD`/`.SRCINFO` under `packaging/aur/`. The Arch recipe follows the fixed `v0.2.9` tag, builds with SQLite support, runs the full CTest suite during `check()`, and keeps `man-db`, `expac`, `pkgfile`, and `tealdeer` as explicit optional integrations.

Bundled curated metadata becomes relocation-safe for packaging: Linux installs first resolve `../share/acclorite/curated-guidance.tsv` relative to `/proc/self/exe`, then fall back to configured install/build paths. This preserves the build-tree workflow while making `/usr`, `/usr/local`, `~/.local`, and staged `DESTDIR` installs self-contained. An end-to-end staged-install test hides the build-tree guidance copy, validates the installed payload, executes the staged binary, and requires capability discovery to find bundled curated guidance from the staged prefix.

The release checklist documents the real-machine benchmark gate, staged installation validation, fixed upstream tag order, and AUR publication flow. Normal Acclorite runtime remains offline; source retrieval and AUR build downloads are distribution concerns only. Search schema 15, Doctor schema 1, capability schema 1, exit codes, ranking, confidence, and guidance precedence remain unchanged. This completes the required Milestone 8 release surface.

Explicitly not required:

- Ollama;
- cloud AI;
- Debian backend;
- Fedora backend;
- Realmheart widget;
- embeddings.

The public v0.1 should be worth using without any of those.

---

# Milestone 9 — Other distro backends

### v0.3.0 first slice — package backend contract + Debian/APT discovery

`v0.3.0` begins the cross-distro phase by extracting the package-family boundary into a common `PackageBackend` interface. `PacmanSource` remains the Arch implementation and must preserve the frozen Arch benchmark behavior; `AptSource` is the first second-distro implementation. The ranker and result model remain unaware of which package manager produced a candidate.

Backend selection is local and deterministic. Acclorite reads `/etc/os-release` without spawning a process, recognizes Arch and Debian families (including derivative `ID`/`ID_LIKE` values), and uses distro identity to break ties when multiple supported package managers coexist. If only one supported backend is available, it may be used as a fallback in minimal chroots/containers. `ACCLORITE_OS_RELEASE` exists only as a controlled/test path override.

The initial APT backend is read-only and offline at runtime. It uses `apt-cache search` against already-local package metadata, `dpkg-query` for installed state when present, and one bounded `apt-cache show --no-all-versions` call for surviving package versions. It never calls `apt-get`, `apt update`, installs/removes packages, or refreshes metadata. Doctor reports Debian-specific package-cache and installed-state readiness only when the Debian backend is active; Arch systems retain their existing Arch Doctor checks. Capability schema 1 gains the backward-compatible optional `apt_packages` boolean. Search schema 15, Doctor schema 1, ranking, confidence, guidance precedence, and exit codes remain unchanged.

Success gates:

- hermetic Ubuntu/Debian fixture selects `AptSource`;
- hermetic APT search discovers an uninstalled repository tool;
- local PATH + dpkg metadata merge into one candidate;
- an `apt-get` trap proves query execution never attempts network/update/install behavior;
- Debian Doctor checks are active without Arch-check clutter;
- all existing Arch/CachyOS benchmark assertions remain unchanged on the reference machine.

### v0.3.1 second slice — Fedora/RHEL DNF discovery

`v0.3.1` adds `DnfSource` as the third implementation of `PackageBackend`. Distro/backend selection is generalized from two booleans to an explicit availability set for Arch, Debian, and Fedora families so adding more backends does not create nested package-manager guesses. `os-release` still breaks ties; a sole backend remains usable in minimal chroots, while unknown environments with multiple supported managers remain unselected.

The DNF path is local-cache-only. It prefers `dnf5`, falls back to `dnf`, and always passes `--cacheonly`. Cached `search --all` performs semantic package discovery, one bounded `repoquery` enriches surviving package names with EVR/repository/summary metadata, and one bounded `rpm -q` lookup supplies installed state. No `makecache`, metadata refresh, install, upgrade, remove, or other package transaction is allowed. Fedora Doctor checks are active only when the Fedora backend is selected. Capability schema 1 gains the backward-compatible optional `dnf_packages` boolean; all existing search/Doctor schemas, ranking, confidence, guidance, and exit-code contracts remain unchanged.

Success gates:

- hermetic Fedora fixture selects `DnfSource`;
- DNF5 is preferred when both `dnf5` and `dnf` exist; DNF4 fallback works when DNF5 is absent;
- cached DNF search discovers an uninstalled repository tool and preserves EVR/repository metadata;
- local PATH + RPM installed state merge with repository evidence into one candidate;
- a mutation trap proves every DNF invocation is cache-only and limited to search/repoquery;
- Fedora Doctor checks appear without Arch/Debian clutter;
- Arch/CachyOS benchmark assertions and Debian machine-contract fixtures remain unchanged.

### v0.3.2 third slice — openSUSE/Zypper discovery

`v0.3.2` adds `ZypperSource` as the fourth `PackageBackend` and enables the already-recognized SUSE/openSUSE distro family in backend selection. The same selection policy remains in force across four families: `os-release` breaks ties, a sole available backend is usable in minimal environments, and an unknown distro with several package managers is never guessed.

The Zypper path is explicitly no-refresh and query-only. Every invocation uses `--no-refresh`, `--non-interactive`, `--xmlout`, and an Acclorite-owned read-only configuration that sets `[search] runSearchPackages = never`. The latter prevents the optional `zypper-search-packages` extended search hook from introducing network behavior through user/system Zypper configuration. XML search supplies semantic package discovery, one bounded detailed XML search preserves edition/repository metadata, and one bounded local `rpm -q` supplies installed state. Capability schema 1 gains the backward-compatible optional `zypper_packages` boolean; search schema 15, Doctor schema 1, ranking, confidence, guidance precedence, and exit-code semantics remain unchanged.

Success gates:

- hermetic openSUSE fixture selects `ZypperSource`, including a four-backend tie;
- Zypper XML search discovers an uninstalled repository tool and decodes XML metadata;
- local PATH + RPM installed state merge with repository-backed Zypper edition/repository evidence;
- a hostile fixture proves every Zypper invocation uses the bundled plugin-disabled configuration, `--no-refresh`, and search-only command paths;
- openSUSE Doctor checks appear without Arch/Debian/Fedora clutter;
- both SQLite and no-SQLite builds pass the full test suite;
- Arch/CachyOS benchmark assertions and the Debian/Fedora contract fixtures remain unchanged.

### v0.3.3 fourth slice — Void/XBPS discovery

`v0.3.3` adds `XbpsSource` as the fifth `PackageBackend` and enables the already-recognized Void family in backend selection. The same policy now spans five package families: a recognized `os-release` family wins ties, a sole available backend may serve a minimal container/chroot, and an unknown environment with several supported managers is never guessed.

The XBPS path consumes synchronized on-disk metadata only. Search uses `xbps-query --regex -Rs` without `-M`/`--memory-sync`; Acclorite never invokes `xbps-install`, so repository synchronization and package transactions remain outside Acclorite. The search stream supplies package-version strings, descriptions, and XBPS installed markers. Two bounded `xbps-uhelper` calls decode package names and versions for the survivor set, and PATH evidence merges same-name installed executables. Doctor checks registered repository indexes and the installed package database only when Void is selected. Capability schema 1 gains the backward-compatible optional `xbps_packages` boolean; search schema 15, Doctor schema 1, ranking, confidence, guidance precedence, and exit-code semantics remain unchanged.

Success gates:

- hermetic Void fixture selects `XbpsSource`, including a five-backend tie;
- synchronized repository search discovers an uninstalled tool and decodes XBPS package/version tuples;
- `[*]` repository status and local PATH evidence merge into one installed candidate;
- hostile fixtures prove `-M`/`--memory-sync`, explicit repository overrides, and `xbps-install` are never used;
- Void Doctor checks appear without Arch/Debian/Fedora/openSUSE clutter;
- both SQLite and no-SQLite builds pass the full test suite;
- Arch/CachyOS benchmark assertions and all earlier distro contract fixtures remain unchanged.

With v0.3.3, the planned Milestone-9 package-backend set is complete. The real CachyOS reference gate passed unchanged at **25/25 query cases, 65/65 assertions, 22/22 Top-1, 22/22 Top-3, 6/6 frame accuracy, and 6/6 ambiguity accuracy**. The five-backend architecture is therefore frozen unless later real-world failures justify a targeted change.

Current project scope intentionally does not include an Alpine/APK backend. Generic Tier-A behavior should continue to work there where the environment provides ordinary Linux sources Acclorite understands; package-manager-specific Alpine integration is not planned.

Do not compromise generic behavior to accommodate one distro.

---

# Milestone 10 — Deterministic actionable answers

**Current implementation status:** Milestone-10 implementation complete pending the final real-CachyOS v5 acceptance gate. Foundation, human-intent recovery, verified man/child-man/Fish grammar, cross-provider evidence merging, deterministic root/subcommand/option binding, complete synopsis-path proof, shell-safe rendering, and evidence-driven safety classification are implemented. The current
working tree has first-class actionable/syntax models, `CommandSyntaxProvider`,
a conservative local-man syntax provider, exact case-sensitive option inspection,
structured `ActionTarget` query intent, deterministic natural-language option and
subcommand capability matching, additive terminal/JSON output, capability reporting,
and hermetic non-execution / unsupported-syntax tests. Direct syntax questions resolve
the named parent command entity-first without modifying generic ranking. The man parser
distinguishes indented option-looking prose from declarations, rejoins groff U+2010 wrap
hyphens, and now conservatively extracts subcommand identity/description from recognized
`COMMANDS`/`SUBCOMMANDS` sections (including parent-prefixed man references such as
`git-branch(1)`) and normalizes groff/man presentation ellipsis in compact command signatures. Semantic option answers prefer a readable long alias when one is locally
proven, while explicit syntax questions preserve the exact spelling the user typed,
including punctuation-valued short options such as `-.`. Subcommand questions use an
explicit Explain frame, keep the structural `subcommand` noun out of capability terms,
and may now return a verified subcommand when local man grammar proves one. The first binder slice now
conservatively parses simple positional tails from proven man command declarations, maps unambiguous
user literals into those slots, and emits structured `CommandInvocation` objects. Required option
values may also be bound or represented as placeholders, but option-derived invocations remain
explicitly incomplete until root synopsis grammar proves the rest of argv. Human rendering goes
through a dedicated shell-quoting layer; JSON preserves the legacy argument string array and adds
per-segment placeholder metadata. The next grammar slice now performs bounded **parent-proven child-man**
inspection: a child page such as `git-switch(1)` is eligible only after the root man grammar has already proven
`switch` as a subcommand, and only a few semantically plausible children are inspected lazily. Child options may
therefore participate in compound-operation resolution and value binding while retaining child-page provenance.
The first static Fish-completion option slice is now implemented as a lower-priority syntax fallback. It reads exact packaged/admin completion files as data, never sources Fish, and accepts only literal single-line `complete` declarations from a conservative understood subset. Conditional/dynamic/unknown constructs are skipped. Completion-derived options preserve `value_shape_known=false` when the completion declaration proves the option spelling/description but does not prove whether the option consumes a value. The tree still deliberately does **not** parse dynamic completion conditions into subcommand state or merge incompatible synopsis
branches. Child synopsis forms are preserved as independent alternatives and used only to prove an already
selected nested option path; unsupported/mixed forms remain incomplete rather than being flattened. A narrow exception now handles same-position man slot unions such as
`[PATTERN...|PID...]`: when every arm is a conservative value slot with identical repetition shape, the
parser may collapse the union into one bindable slot while retaining man provenance. Existing
ordinary discovery still bypasses syntax providers entirely. The human-intent slice adds deterministic
recovery for conversational scaffolding, command-addressed sentences, and implied operations. A
command-addressed request is first represented as syntax-agnostic `Operation`; only verified grammar
may resolve that operation into an `option` or `subcommand` result. Query/frame vocabulary was also
expanded from the frozen adversarial human corpus, with the original 25-case discovery corpus retained
as the non-regression gate.

Goal:

> Move from “this is the right tool” to “this is the right tool, this is why, and this is the verified command/flag/subcommand shape you actually need” — without AI and without executing anything.

This milestone is intentionally placed **before optional AI**. Acclorite should first exhaust the value available from deterministic intent parsing, local documentation, command grammar, shell-completion metadata, and verified examples.

## 10.1 Structured answer/result model

Add a first-class actionable-answer layer rather than overloading `GuidanceProvider` indefinitely.

Conceptual model:

```cpp
struct CommandOption {
    std::vector<std::string> names;      // e.g. -i, --interactive
    std::string description;
    std::optional<std::string> value_name;
    bool takes_value = false;
    bool value_required = false;
    Provenance provenance;
};

struct SubcommandSpec {
    std::string name;
    std::string description;
    std::vector<CommandOption> options;
    std::vector<ArgumentSpec> positionals;
    Provenance provenance;
};

struct CommandGrammar {
    std::string command;
    std::vector<CommandOption> global_options;
    std::vector<SubcommandSpec> subcommands;
    std::vector<ArgumentSpec> positionals;
    std::vector<SynopsisAlternative> synopsis;
};

struct ActionableAnswer {
    std::string explanation;
    std::optional<CommandInvocation> invocation;
    std::vector<CommandOption> relevant_options;
    std::vector<SubcommandSpec> relevant_subcommands;
    std::vector<UsageExample> examples;
    std::vector<LearningResource> resources;
    ActionSafety safety;
};
```

Exact data types may evolve, but the architectural separation should remain: retrieval/ranking chooses the tool; syntax providers prove command grammar; binding maps the query into that grammar; answer composition explains the result.

## 10.2 `CommandSyntaxProvider` boundary

Introduce a deterministic post-ranking syntax interface. Initial evidence priority:

1. local man `SYNOPSIS`, `OPTIONS`, `COMMANDS`/subcommand sections where conservatively parseable;
2. local Info usage/option sections;
3. statically parsed shell-completion metadata;
4. existing verified TLDR/curated records for examples/learning links, but **not** as unproven grammar authority.

Every accepted flag, subcommand, argument shape, and synopsis alternative carries provenance. If Acclorite cannot prove syntax, it leaves it out.

Do not invoke arbitrary discovered commands with `--help` in the initial implementation. Any future help-probing feature requires its own explicit safety model.

## 10.3 Flags, subcommands, and direct syntax questions

The query layer must recognize when the user's target is not merely a tool but a **flag/subcommand/capability inside a tool**.

**Implemented slice:** `ActionTarget` currently distinguishes option, subcommand,
positional, and operation target kinds. Explicit option literals are preserved from
the raw query so syntax case is never lost (`-L` remains distinct from `-l`). Natural
flag/option questions also preserve a parent command plus normalized capability terms,
for example `curl` + `{follow, redirects}`. Option and subcommand targets are composed only when a trusted syntax provider proves
the matching grammar. Human wording no longer has to name that syntax category: command-addressed
sentences such as `rg is skipping dotfiles, make it search them too` or `systemctl show me what nginx
is doing` begin as `Operation` targets with compact capability terms. The answer layer compares that
operation against verified option/subcommand grammar and exposes the resolved target kind only after
one syntactic species is proven; if both are plausible, it refuses to guess. Semantic option selection
is conservative: option names and local
man descriptions provide the evidence, required capability coverage must be met, and
near-ties return no actionable answer rather than guessing. Semantic answers prefer a
proven long-form alias for readability (`--hidden` over `-.`), while literal syntax
inspection preserves the user's exact spelling. A later ambiguity slice should decide
how to expose **multiple independently valid semantic matches** when the local docs prove
materially different options for the same broad capability (for example modern curl's
`--follow` and `--location`) instead of forcing one global canonical answer.

Target classes should eventually include:

```text
command
subcommand
option / flag
positional argument / value
operation provided by a command
```

Representative queries:

```text
what flag makes curl follow redirects
what does ffmpeg -ss do
how do I make a git branch and switch to it
which rg flag includes hidden files
show systemctl status for nginx
rename ~/uncool-shit to ~/cool-shit
```

The correct result may therefore be `curl -L`, `ffmpeg -ss`, `git switch -c`, `rg --hidden`, or `systemctl status`, not merely the parent command name.

### Implemented child-man enrichment slice

After root man grammar proves a subcommand identity, `CommandSyntaxProvider` may optionally provide deeper
syntax for that child. `ManCommandSyntaxProvider` constructs only the deterministic parent-derived page name
(`<parent>-<subcommand>`), invokes the known `man` frontend, and parses the resulting local page as data. It never
executes the parent or child target command. Answer composition inspects at most three root subcommands with
partial semantic support, and accepts a nested subcommand+option match only when it covers strictly more of the
requested operation than root grammar alone. This prevents a query such as `git create a branch` from being
upgraded to `git switch --create` merely because the deeper syntax is capable of doing more; `create branch +
switch` may earn that deeper path. Child-option value binding now consults independently preserved child `SYNOPSIS` alternatives. A nested
invocation becomes **complete** only when one explicit synopsis path contains the selected option (or one
of its proven aliases), accounts for its required value, and leaves no other mandatory syntax unmet. Generic
`[<options>]` allowance is not enough; ambiguous/incomplete paths remain verified templates.

Real child manuals also expose an alias-format edge case that the parser must preserve conservatively:
multiple aliases may repeat the same whitespace-separated value signature, for example
`-c <new-branch>, --create <new-branch>`. Such aliases are collapsed into one option fact only when the
repeated compact value grammar agrees across spellings; disagreements are rejected rather than guessed.
When two deep options cover the same requested operation, the composer prefers the option spelling with
fewer **unrequested option-name concepts** before applying score/tie rules. This keeps a plain create request
on `--create` rather than `--force-create` unless the query itself supplies force/reset intent, and generalizes
the no-unrequested-side-effects rule beyond Git.

## 10.4 Static shell-completion parsing

Shell completions become a first-class deterministic syntax source because they often encode subcommands, flags, descriptions, and accepted argument shapes.

Potential locations remain:

```text
/usr/share/fish/vendor_completions.d/
/usr/share/zsh/site-functions/
/usr/share/bash-completion/
```

Safety rule:

> Completion files are parsed as data; they are never sourced or executed.

Implement conservative, format-aware parsers for syntax constructs Acclorite can prove. Dynamic shell code, command substitution, arbitrary helper execution, and unknown constructs are skipped. Fish is likely the easiest initial source; Zsh/Bash support can grow incrementally.

This milestone covers completion-derived **command grammar**. Broader completion-derived semantic indexing/retrieval can remain advanced research.

**Implemented Fish static-option slice:** `FishCompletionSyntaxProvider` searches exact system/admin `<command>.fish` files in conventional Fish completion roots and parses only literal single-line `complete` declarations. The safe subset understands command identity, short/long option spellings, descriptions, `--require-parameter`, `--exclusive`, static argument candidate lists, compact understood switch clusters, and file-completion UI switches that do not affect argv grammar. Shell metacharacters, variable/command substitution in data fields, dynamic argument generation, line continuation, or an unknown `complete` switch still invalidate the declaration. The provider never invokes `fish`, never sources a completion file, and never executes the target command.

**Implemented condition-aware Fish slice:** condition parsing is an explicit allowlist, not a Fish interpreter. Exact `__fish_use_subcommand` conditions may promote literal static argument candidates into `SubcommandSpec`s. Exact positive `__fish_seen_subcommand_from <literal...>` conditions may attach an option only to an already-proven subcommand identity. That identity may be proven by Fish itself or supplied by a higher-priority trusted root provider through the child resolver. Variable-backed aliases such as `$subcommands`, negation, `and`/`or` chains, arbitrary helper predicates, and all other conditions are ignored. The same option may be scoped to several independently proven literal subcommands. Scoped Fish grammar is exposed through `subcommand_grammar()` so the existing deep composer can use it without weakening the parent-proof model.

**Implemented syntax-evidence merge slice:** `SearchEngine` gathers root grammar from every registered available syntax provider in registration/authority order and composes once over the merged grammar. Non-overlapping facts are unioned; earlier conflicting facts win. Child lookup similarly asks every active provider and merges the resulting `SubcommandSpec`, allowing source boundaries such as man-proven child identity + completion-proven scoped option while retaining provenance on each accepted fact. Duplicate options are not field-spliced across sources: a later duplicate can replace an arity-unknown option only when it proves a strictly stronger value shape, preserving truthful whole-fact provenance. Completion conditions still cannot create a child identity on their own. Profiling continues to expose each root and child provider cost independently.

Fish metadata frequently proves that an option exists without proving whether it consumes a value. `CommandOption` therefore exposes `value_shape_known`: man-derived options remain known, Fish options using `--require-parameter` or `--exclusive` are known required-value options, and Fish options without parameter metadata keep `value_shape_known=false`. Such an option may answer an explanation/capability question, but the binder must not manufacture an invocation from an unknown value shape. Even a bound condition-scoped Fish child option remains `complete=false` unless an independent trusted SYNOPSIS path proves the whole argv form.

## 10.5 Deterministic `ArgumentBinder`

After a candidate and grammar are known, map structured query entities into verified slots.

Examples:

```text
rename ~/uncool-shit to ~/cool-shit
    ↓
operation: rename/move
source_path: ~/uncool-shit
destination_path: ~/cool-shit
    ↓
verified mv synopsis: SOURCE DEST
    ↓
structured invocation
```

**Implemented first slice:** compact man subcommand tails such as `status [UNIT...]` are converted
into provenance-bearing `ArgumentSpec` slots when they are representable conservatively. Same-position
value-kind unions such as `status [PATTERN...|PID...]` are also bindable when every arm has the same
optional/variadic shape; literal choices such as `start|stop`, mixed-shape unions, and broader mutually
exclusive syntax remain unflattened. The binder lexes user text as data (never shell code), removes command/operation scaffolding, preserves the
remaining literal, and binds only when the slot count/role is unambiguous. Source paraphrases that have
already been canonicalized into an operation (for example `go there` -> `switch`) are removed from binding
candidates only under that proven operation, so generic data such as a literal named `new` is not globally
classified as filler. Leading-dash positional data is refused until a verified end-of-options path exists.
Explain-only questions do not manufacture invocations.

**Implemented practical root-binding slice:** the binder now consumes conservative root `SYNOPSIS`
alternatives in addition to subcommand tails. Forms such as `[OPTION]... SOURCE DEST`,
`[OPTIONS] PATTERN [PATH...]`, `DIRECTORY...`, and `[FILE]...` can produce structured root invocations
when every required slot is accounted for. Query literals carry lightweight binding metadata (relation such
as source/destination/name/location and kind such as path/URL/number/general), allowing multiple values and a
selected global option value to be separated deterministically. Optional opaque grammar is skipped; required
opaque/literal grammar, unsafe leading-dash data, ambiguous option values, and unconsumed user literals reject the
candidate path rather than being guessed. Independent synopsis alternatives remain independent. A generic
`[OPTIONS]` allowance can be combined with separately proven option arity, but option-only partial templates are
not replaced merely because a root form can manufacture additional placeholders. Strong operation phrases with
concrete user data may also survive as a command-agnostic `ActionTarget`; the already-ranked candidate supplies the
parent command before grammar/binding, so this adds actionable breadth without modifying ranking.

Rules:

- bind only when the query role and grammar slot are both sufficiently clear;
- never invent missing required values;
- if values are missing, emit a verified command template with placeholders;
- preserve mutually exclusive syntax alternatives instead of merging them;
- never fabricate a recursive/force/sudo/etc. flag because it “sounds right”;
- only emit conventional sentinels such as `--` when verified for that command/syntax path.

## 10.6 Structured invocation and shell-safe rendering

Do not construct commands with string concatenation.

Internally represent a command as structured arguments/segments, conceptually:

```text
command: mv
argv/segments:
  - ~/uncool-shit
  - ~/cool-shit
```

Human output is produced by a dedicated shell renderer with correct quoting/escaping for spaces, quotes, glob characters, leading dashes, Unicode, and other shell-sensitive values. Machine JSON exposes the structured representation so Realmheart/future clients never parse the pretty shell string.

The first implementation keeps `arguments[]` as the stable string projection and adds `segments[]` with
`value` + `placeholder` so clients can distinguish literal user data from verified template slots. A
`complete = false` invocation is a verified partial/template, not permission to execute it.

Acclorite remains a **non-executing** utility. It recommends/copies verified syntax; it does not run the command.

## 10.7 Deterministic `AnswerComposer`

Compose concise explanations from structured evidence instead of generating prose freely.

Frame-oriented answer shapes:

```text
Discover → recommend X for Y + why + verified example/template
Explain  → explain X + relevant options/subcommands + docs
Modify   → recommend verified operation/invocation + safety note
Inspect  → recommend verified read/inspect invocation
Locate   → return real paths/resources + related syntax only when useful
Compare  → explain supported differences from retrieved metadata/grammar
Diagnose → bounded tool-oriented explanation; no invented troubleshooting
```

A good answer should normally include:

- one concise statement that directly answers the question;
- why the selected command/subcommand/flag fits;
- a copyable verified invocation or a clearly marked template when enough arguments are known;
- a few relevant verified flags/subcommands, not a dump of every option;
- two or three source-backed examples when available;
- local/upstream documentation links/references;
- confidence/provenance and safety state.

## 10.8 Command/action safety metadata

**Implemented final Milestone-10 slice:** `SafetyClassifier` assigns the existing actionable safety states:

```text
ReadOnly
Mutating
Destructive
Privileged
Network
Unknown
```

Classification is deliberately downstream of command construction. An answer must contain a complete, concrete, placeholder-free `CommandInvocation` before any non-`Unknown` label is possible. The classifier compares the requested/selected operation vocabulary against the most specific source-backed semantics already accepted by the syntax pipeline: selected option/subcommand descriptions first, otherwise the candidate summary for a verified root command. Explicit privilege wording and concrete network locators are treated conservatively; executable names alone are never safety evidence.

The current single-valued model uses dominant-class precedence `Destructive` → `Privileged` → `Network` → `Mutating` → `ReadOnly`. If evidence conflicts, the invocation is incomplete, or correspondence is weak, the result remains `Unknown`. This is informational only; Acclorite still does not execute commands or become a confirmation/approval system.

## 10.9 Machine interface

Actionable answers must be available structurally in JSON. Prefer additive fields where the existing schema contract permits them; if an existing field's meaning/type must change, bump the appropriate schema deliberately.

Machine clients must never be required to parse:

- ANSI terminal text;
- rendered shell command strings;
- prose explanations;
- documentation output.

They should receive command/subcommand/options/arguments/provenance/safety as structured fields.

## 10.10 New actionable-answer benchmark corpus

Keep the existing 25-case / 65-assertion discovery corpus frozen as the non-regression gate. Milestone 10 adds **two independent new measurement axes** rather than one blended score:

1. `corpus-human-v1.json` — adversarial human-language understanding: incomplete recall, filler, slang, metaphor, typos, implied operations, and requests that do not conveniently say Acclorite's ontology words such as `flag` or `subcommand`;
2. a dedicated actionable-answer corpus — verified command grammar, argument binding, shell rendering, provenance, safety, and complete invocation behavior.

The separation is deliberate. A query such as `what is that command that can extract tar.gz files in terminal` should first prove that Acclorite understood the user's intent and discovered the correct tool. A later syntax failure must not obscure whether language understanding itself succeeded. Conversely, correct tool discovery must not be mistaken for a verified actionable answer.

The human-language corpus is adversarial by design and must not be rewritten merely to accept current failures. Add new cases from real user failures. However, accepted outcome sets are a statement about **semantic correctness**, not a preferred-brand whitelist: when a real-machine run surfaces another source-backed tool that genuinely satisfies the task (for example a batch-capable image converter or a directory disk-usage analyzer), add that equivalent answer instead of distorting ranking to force an older expected winner. The frozen v1 discovery gate remains mandatory while tuning either new corpus.

### Human-language ranking specificity

Once conversational framing and implicit operations are recovered, remaining failures should be treated as **role-specific ranking problems**, not as invitations to add command-specific aliases. The query-relative ranking policy may use source-backed metadata to distinguish:

- transformation front doors from archive/search/inspection tools that merely operate inside the same domain;
- file/directory comparison tools from image-comparison utilities whose executable name happens to be `compare`;
- image-to-image format converters from text/ASCII/terminal renderers;
- filesystem-usage front doors from unrelated binaries whose names happen to contain `size`;
- generic directory-usage tools from utilities specialized to a particular filesystem family or foreign filesystem namespace (for example FAT/MS-DOS- or F2FS-specific tooling) when that specialization was not requested.

Positive role evidence is allowed to establish a conservative semantic floor when terse local documentation omits an obvious object, while contradictory specialization applies a multiplicative penalty. Both must remain visible in ranking diagnostics. A raw token collision is **not** sufficient proof that the human explicitly named a command: explicit-entity bypasses require exact whole-query identity, a resolved frame target, or another structured parent-command resolution. This prevents ordinary English verbs/nouns such as `compare` and `size` from disabling query-relative domain constraints.

Discovery-only modifiers that matter later to invocation construction (for example `readable not raw bytes`) may be preserved in the raw query while being excluded from full-weight parent-tool discovery evidence. Do not discard them from the query representation; later argument/option binding may need them.

The actionable snapshots are versioned rather than rewritten when a deliberate historical
limitation is unlocked. `corpus-actionable-v3.json` freezes the child-synopsis completeness gate at 8/8 cases and
69/69 assertions on the real CachyOS reference. `corpus-actionable-v4.json` freezes practical root binding at
**14/14 cases and 138/138 assertions** after the hidden-files paraphrase guardrail. `corpus-actionable-v5.json` is
the final Milestone-10 gate: it carries the same 14 tasks forward and adds an exact `actionable_safety` assertion
to every case, including `Unknown` for explanation-only/incomplete answers. The oracle remains command-anchored on
purpose so the final M10 gate measures verified actionable behavior rather than reopening ranking work.

The actionable corpus should test at minimum:

- correct parent tool selection;
- correct subcommand selection;
- correct flag/option selection;
- explanation relevance;
- argument binding;
- missing-argument placeholder behavior;
- invalid/unsupported flag rejection;
- syntax provenance;
- shell quoting/escaping edge cases;
- safety classification;
- static completion parsing without sourcing/execution;
- no arbitrary command execution;
- JSON structure and terminal UX.

Representative target outcomes:

```text
"rename old-folder to new-folder"
    → mv
    → verified SOURCE DEST form

"what flag makes curl follow redirects"
    → curl
    → -L / --location, only if verified locally

"create a git branch and switch to it"
    → git switch -c <branch>, when the local grammar supports it

"search TODO in ~/Realmheart but ignore build"
    → rg
    → bind pattern/path and the verified exclusion/glob syntax
```

Success condition:

> Acclorite can answer a useful set of real “how do I do X?” and “which flag/subcommand does Y?” questions with verified, source-backed, copyable syntax while remaining deterministic, offline, non-executing, and fully backward-compatible with the existing discovery benchmark.

---

# Milestone 11 — Advanced deterministic research

Move the old advanced-research phase **before Realmheart integration and before optional AI**. There is no requirement to implement every item; each experiment must earn its place through measured benefit.

The active M11 research protocol lives in `docs/m11-research.md`. The first exploratory measurement surface is `benchmarks/corpus-m11-research-v1.json`, focused on semantically clear but lexically distant tool-discovery tasks. It is intentionally not a frozen gate: establish the real CachyOS baseline first, classify failures, and compare competing approaches before accepting any new dependency or architecture. The frozen M10 corpora remain strict non-regression contracts throughout M11.

Candidates:

- local embeddings;
- semantic retrieval/reranking;
- smarter phrase learning from benchmark failures;
- personalized ranking that remains inspectable and bounded;
- richer command comparisons using structured grammar/capability data;
- interactive learning/exploration mode;
- deeper shell-completion semantic indexing beyond the syntax extraction required by Milestone 10;
- optional `libacclorite` if embedding use cases justify a stable library boundary.

Cloud troubleshooting is removed from this milestone. It conflicts with the “push deterministic/local capability as far as useful first” goal and can remain an unnumbered future experiment if real demand appears.

Research rules:

1. the existing deterministic path stays authoritative;
2. experiments do not become dependencies merely because they are interesting;
3. every accepted feature needs benchmark or real-user evidence;
4. latency/memory costs are measured separately from correctness gains;
5. no experiment may weaken offline Tier-A behavior or the five distro backends.

Success condition:

> Adopt only research ideas that measurably improve retrieval, explanation, command intelligence, or usability enough to justify their complexity while preserving deterministic authority. M11 semantic research has accepted an optional, local pretrained **Semantic Assist** recall channel; deterministic ranking remains canonical, and production qualification is tracked in `docs/m11-semantic-assist-production.md`.

---

# Milestone 12 — Realmheart visual integration

Initial integration:

```text
Realmheart
    ↓
acclorite --json
    ↓
native Realmheart widget/client
```

Possible surfaces:

- Realm Action;
- launcher;
- dedicated Acclorite popup;
- contextual tool-discovery cards;
- actionable command cards with copy controls;
- expandable explanation/documentation;
- relevant flag/subcommand chips;
- safety/provenance indicators.

Realmheart consumes the stable machine interface and **does not** scrape terminal output. Acclorite remains independently usable and owns retrieval, ranking, grammar, explanation data, and command-construction semantics; Realmheart owns visual presentation/interactions.

---

# Milestone 13 — Optional generative/LLM fallback

M11 Semantic Assist uses a bounded local embedding model only for a separate recall channel; it is **not** this milestone. Only begin a generative/LLM fallback after Milestones 10–12 have pushed deterministic/local behavior as far as practical and a benchmark demonstrates a remaining language-understanding gap worth paying for.

Implement experimentally:

- intent-parser interface;
- deterministic/no-AI parser as the baseline implementation;
- optional Ollama parser;
- user-controlled AI mode (`off`, `fallback`, possibly explicit/manual);
- confidence-based fallback experiment;
- structured intent schema shared with the deterministic engine;
- a dedicated “Hard / Absurd” benchmark.

AI is permitted to normalize/translate intent into the **same structured representation** the deterministic pipeline uses. It is not permitted to become the source of command names, flags, package metadata, syntax, or safety claims.

Success condition:

> AI measurably improves the Hard / Absurd corpus beyond the fully developed deterministic system without becoming necessary for normal queries and without bypassing verified retrieval/grammar.

If it cannot clear that bar, do not ship it merely because the milestone exists.

---

# 25. Immediate First Coding Session

Do not start with Ollama.

Do not start with Realmheart widgets.

Do not start with package-manager abstraction for seven distros.

The first session should aim to produce a real native executable with a real architecture.

Suggested order:

```text
1. Create repository and CMake project.
2. Add C++20 executable target.
3. Define Query, Candidate, SearchResult.
4. Define renderer interface.
5. Implement terminal renderer.
6. Implement JSON renderer.
7. Implement simple CLI parsing.
8. Add PATH scanner.
9. Return PATH matches as candidates.
10. Add a tiny test corpus.
```

End-of-session target:

```bash
./acclorite "grep"
```

returns a real candidate discovered from the system.

That result can be dumb.

It just has to be real.

---

# 26. Second Coding Session

Target:

> Give candidates meaning.

Implement:

- man/apropos capability detection;
- extract command descriptions;
- SQLite index;
- FTS query;
- combine PATH status with documentation.

End target:

```bash
./acclorite "search text"
```

returns something plausibly useful from local metadata.

---

# 27. Third Coding Session

Target:

> Make human language start working.

Implement:

- RapidFuzz;
- vocabulary data file;
- phrase mappings;
- query normalization;
- first ranking formula;
- score diagnostics.

End target:

```bash
./acclorite "what thing searches inside files"
```

should rank `rg` / `grep` sensibly depending on what the system knows.

---

# 28. Fourth Coding Session

Target:

> Arch propaganda becomes software architecture.

Implement:

- PackageBackend interface;
- PacmanBackend;
- package description search;
- repository availability;
- installed package integration;
- optional expac detection.

After this session, Acclorite should begin discovering useful non-installed tools.

---

# 29. Decisions Intentionally Deferred

Do not prematurely lock:

- exact C++23 migration;
- exact JSON library;
- exact CLI flag syntax;
- exact ranking weights;
- exact vocabulary file format;
- exact index schema beyond first migration;
- embeddings;
- exact local LLM model (deferred until Milestone 13 proves one is useful);
- Ollama model name;
- libalpm integration;
- exact public `libacclorite` ABI/API (deferred until Milestone 11 proves a library boundary is useful);
- Realmheart widget design;
- cloud service;

These should be decided using prototypes, benchmarks, or real user demand.

---

# 30. Things We Must Not Accidentally Do

## Do not turn Acclorite into an LLM wrapper

The retrieval engine is the product.

## Do not make optional integrations hard dependencies

If `pkgfile` is absent, Acclorite still works.

## Do not contaminate the core with pacman assumptions

Arch-specific behavior belongs in the Arch backend.

## Do not scrape pretty terminal output for Realmheart

Realmheart consumes structured JSON.

## Do not let Realmheart-specific visual requirements distort the CLI core

Realmheart is a client.

## Do not create a daemon just to keep the index fresh

Refresh lazily / explicitly.

## Do not index arbitrary enormous directories

Knowledge sources must be narrow and controlled.

## Do not execute arbitrary commands to obtain metadata

Prefer documentation files and known-safe metadata interfaces.

If `--help` probing is added later, use a strict safety policy.

## Do not display unverifiable examples as fact

Missing example is better than invented syntax.

## Do not invent flags, subcommands, or argument shapes

A syntactically plausible command is still wrong if Acclorite cannot prove that syntax from an accepted source.

## Do not source or execute completion scripts

Completion metadata is statically parsed. Unknown/dynamic shell constructs are skipped.

## Do not build copyable commands by concatenating shell strings

Keep command structure separate from rendering. A dedicated shell renderer handles quoting; machine clients consume structured fields.

## Do not silently cross the product boundary into command execution

Actionable answers may be copyable. Acclorite itself remains non-executing.

## Do not optimize before measuring

Especially ranking, embeddings, and AI.

---

# 31. Definition of a Strong v0.1

Acclorite v0.1 should feel successful if a fresh Arch user can install it, never configure AI, and type things like:

```text
find duplicate files
what is eating my storage
search for text in this project
which process has port 3000
compare folders
compress this directory
browse disk usage
find a faster grep
fuzzy search terminal
```

and receive consistently sensible tool suggestions.

The tool should:

- start quickly;
- use little memory;
- have no resident background process;
- survive missing optional utilities;
- distinguish installed from available tools;
- explain why a result matched;
- point toward real documentation;
- expose structured JSON;
- recover from a broken cache;
- remain useful offline.

If v0.1 needs an LLM to feel good, v0.1 is not done.

---

# 32. Long-Term Architecture Summary

```text
                         USER QUERY
                             │
                             ▼
                     Query Normalizer
                             │
                             ▼
                    Query Frame Recognizer
                             │
                ┌────────────┼────────────┐
                │            │            │
                ▼            ▼            ▼
             phrases       fuzzy       entities
                │            │            │
                └────────────┼────────────┘
                             ▼
                        Search Terms
                             │
                             ▼
                       LOCAL INDEX
                             │
        ┌────────────────────┼────────────────────┐
        │                    │                    │
        ▼                    ▼                    ▼
       PATH                Docs              Packages
                        man / info       selected backend
                        TLDR/local       Arch/APT/DNF/
                                         Zypper/XBPS
        │                    │                    │
        └────────────────────┼────────────────────┘
                             ▼
                      Candidate Pool
                             │
                             ▼
                           Ranker
                             │
                             ▼
                     Confidence / Result
                             │
                             ▼
                 verified top candidate(s)
                             │
                             ▼
                  CommandSyntaxProvider(s)
                             │
                             ▼
                       CommandGrammar
                  flags / subcommands / args
                             │
                             ▼
                       ArgumentBinder
                             │
                             ▼
                 structured CommandInvocation
                             │
                             ▼
                       AnswerComposer
                 explanation / examples / docs
                    provenance / safety
                             │
                ┌────────────┴────────────┐
                ▼                         ▼
         Terminal Renderer             JSON API
                                           │
                                           ▼
                                     Realmheart client

Optional future AI sits *outside* the knowledge/syntax authority path:

             weak/absurd deterministic interpretation
                             │
                             ▼
                       AI enabled?
                        ╱       ╲
                      no         yes
                      │           │
               clarify/show      ▼
               alternatives   intent translator
                                  │
                                  ▼
                     SAME structured query model
                                  │
                                  ▼
                     SAME deterministic retrieval
                                  │
                                  ▼
                     SAME verified syntax pipeline
```

The LLM never replaces retrieval, ranking, documentation provenance, command grammar, or safety classification.

Those deterministic boxes are Acclorite.

---

# 33. One-Line Engineering Rule

> Build Acclorite so strong without AI that it can discover the tool, explain the answer, and construct verified syntax itself — leaving an LLM, if one is ever useful, as an emergency translator for users speaking absolute hieroglyphics.

Everything else follows from that.
