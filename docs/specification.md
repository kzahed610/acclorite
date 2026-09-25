# Acclorite
## Linux Tool Discovery Assistant — Specification / Design Document

**Name:** `acclorite` (its PEAK, agree or kys)  
**Status:** Concept / future project — prototype phase planned in Python, native rewrite planned in C++  
**Primary goal:** Help a Linux user discover the correct command or tool from natural-language intent.  
**Core principle:** Local-first, documentation-first, suggestion-only.  
**Cloud AI:** Optional future service, never required for the core product.  
**Command execution by AI:** Explicitly forbidden.  
**Distribution target:** AUR, intended for general Linux/Arch users — not scoped to Realmheart users only.

---

# 1. Executive Summary

This project is a terminal utility designed around a very common Linux problem:

> "I know what I want to do, but I don't know what command or tool does it."

Traditional Linux documentation usually assumes the opposite:

```text
Know the command
    ↓
Read its documentation
```

This tool reverses that workflow:

```text
Describe the goal
    ↓
Understand the user's intent
    ↓
Discover relevant tools
    ↓
Rank the best matches
    ↓
Show concise usage examples
    ↓
Link the user into the real documentation
```

The project is **not intended to replace `man`**, shell documentation, or Linux knowledge.

It is intended to become a better *front door* into those resources.

The tool should be useful to:

- New Linux users who do not know what tools exist
- Intermediate users who know Linux concepts but forgot a command
- Experienced users who remember that "there is a tool for this" but cannot remember its name
- Power users dealing with unfamiliar tools, distributions, or workflows

The first design should include a **small local language model used primarily as an intent parser**.

The model is not the authoritative Linux knowledge source. Local system documentation and package metadata are.

A much later optional cloud-AI mode may provide deeper troubleshooting when local discovery is insufficient. That service must remain advisory only and must not receive tools capable of executing, modifying, deleting, or otherwise controlling the user's machine.

---

# 2. Core Philosophy

## 2.1 The Product Is Tool Discovery

The project should not become "another AI terminal assistant."

Its primary promise is:

> Tell me what you are trying to accomplish, and I will help you discover the right Linux tool.

This is fundamentally different from an autonomous terminal agent.

The normal flow should be:

```text
Human intent
    ↓
Tool discovery
    ↓
Documentation
    ↓
Human executes command
```

Not:

```text
Human intent
    ↓
AI generates command
    ↓
AI executes command
```

The system should preserve the user's control and encourage learning.

---

## 2.2 Local Documentation Is the Source of Truth

The system should prefer information already available on the user's machine.

Potential local sources include:

- `man` pages
- `apropos`
- `man -k`
- `whatis`
- `info` documents
- command `--help` output
- shell built-in help where applicable
- package descriptions
- package metadata
- installed-program metadata
- documentation under `/usr/share/doc`
- relevant configuration documentation
- distribution-specific package databases

The project should search and rank these sources rather than attempting to memorize the entire Linux ecosystem inside the model.

---

## 2.3 The Local LLM Is a Translator, Not the Encyclopedia

The local LLM should answer a narrow question:

> "What is this human probably trying to accomplish?"

For example:

```text
"yo my disk is full, how do I see what's eating it"
```

might become:

```text
storage analysis
disk usage
large files
directory size
```

The model then passes those concepts into the local search/ranking pipeline.

The model should not be trusted to invent commands or documentation.

---

# 3. Example User Experience

## 3.1 Simple Discovery

```bash
what "find duplicate files"
```

Possible output:

```text
Best Match
────────────────────────────

fdupes

Detects duplicate files by comparing file contents.

Example:
  fdupes -r ~/Downloads

Alternatives:
  rdfind
  czkawka-cli

Learn more:
  man fdupes
```

---

## 3.2 Tool Discovery From Vague Language

```bash
what "show me what's eating all my disk space"
```

Possible result:

```text
Likely Intent
────────────────────────────

disk usage
storage analysis
large files / directories

Best Matches

1. ncdu
   Interactive disk usage explorer.

2. du
   Standard Unix disk usage utility.

3. dust
   Human-friendly disk usage viewer.

Installed:
✓ ncdu
✓ du
✗ dust

Try:
  ncdu ~

Learn:
  man ncdu
```

---

## 3.3 Discovering a Tool Without Knowing Its Name

```bash
what "search for a word inside all files in this project"
```

Possible result:

```text
Best Match:
rg (ripgrep)

Why:
Fast recursive text search.

Example:
  rg "TODO" .

Alternative:
  grep -R "TODO" .

Learn:
  man rg
```

The user did not need to know that `rg` existed.

That is the entire point.

---

# 4. Command Name — Finalized: `acclorite`

The command name is decided: **`acclorite`**.

## 4.1 Why `acclorite`

Acclorite is a mineral from *The Beginning After the End* (TBATE). In the source material, it is embedded into a person and absorbs input from them, gradually shaping itself into something suited to what was given to it — it does not have a fixed form until it is fed something to work with.

The naming parallel to this tool is intentional and mechanically accurate:

```text
Raw, unstructured intent goes in
    ↓
The tool absorbs it
    ↓
It shapes itself into the specific answer the user needed
```

This mirrors the project's own core philosophy from Section 2 — the tool does not have a fixed answer in advance, it forms one around whatever vague, messy, human input it receives.

## 4.2 Why the name works even outside TBATE

The name was deliberately chosen to succeed on two levels at once:

- **To someone unfamiliar with TBATE:** `acclorite` reads as an invented mineral/material name — sounds clean, technical, pronounceable, and fits naturally as a terminal command. No context is required to use or enjoy it.
- **To a TBATE reader:** the name is an immediate, unexplained signal, discoverable only by searching it.

## 4.3 The name is explicitly NOT explained anywhere in the README

This is a deliberate design decision, not an oversight.

The README should describe what the tool does, but should **never** explain where the name comes from.

The intended discovery path is:

```text
User sees "acclorite" is a Realmheart-adjacent project
        ↓
Realmheart is known to be thematically inspired by "some book"
        ↓
User searches "acclorite"
        ↓
User lands on TBATE wiki / lore
        ↓
User realizes the entire naming scheme was a deliberate reference
```

No signposting. No footnote. No "fun fact" section. The clue trail is the feature.

## 4.4 Command should still feel like a question

Regardless of the name's lore origins, the UX principle from the original draft still holds — using the tool should feel like asking:

> "What do I use for this?"

rather than:

> "Generate a command for me."

```bash
acclorite "find duplicate files"
acclorite "what's eating my disk"
acclorite "search inside files recursively"
```

---

# 5. High-Level Architecture

The proposed architecture:

```text
                         ┌───────────────────────┐
                         │      User Input       │
                         │ "find duplicate files"│
                         └───────────┬───────────┘
                                     │
                                     ▼
                         ┌───────────────────────┐
                         │   Local Intent LLM    │
                         │   (small / ephemeral) │
                         └───────────┬───────────┘
                                     │
                                     ▼
                         ┌───────────────────────┐
                         │  Intent Normalizer    │
                         │ keywords / concepts   │
                         └───────────┬───────────┘
                                     │
                    ┌────────────────┼────────────────┐
                    │                │                │
                    ▼                ▼                ▼
               man database    package metadata   local docs
                    │                │                │
                    └────────────────┼────────────────┘
                                     ▼
                         ┌───────────────────────┐
                         │ Candidate Generation │
                         └───────────┬───────────┘
                                     ▼
                         ┌───────────────────────┐
                         │   Candidate Ranking  │
                         └───────────┬───────────┘
                                     ▼
                         ┌───────────────────────┐
                         │  Human-Friendly UI   │
                         └───────────────────────┘
```

Optional future troubleshooting mode:

```text
Local discovery fails
        ↓
User explicitly chooses cloud help
        ↓
Context collector
        ↓
Evidence pack
        ↓
Cloud model
        ↓
Analysis + suggestions
        ↓
Human reviews and executes
```

---

# 6. Local LLM Intent Parser

## 6.1 Why Use a Local LLM?

Pure keyword matching would work for obvious requests, but natural language is messy.

Users write things like:

```text
"how do i see what folders are huge"
"thing that searches inside files"
"my wifi is being weird"
"what's the command to make a tar thing"
"how do I squash images into a smaller format"
"I need to see which process is eating RAM"
```

A small language model can normalize this language into structured intent.

Because this is a narrow task, the model does not need to be a general-purpose assistant.

---

## 6.2 Model Responsibilities

The local model should primarily perform:

### Intent extraction

Example:

```text
"find files bigger than 1GB"
```

→

```text
filesystem search
large files
file size filtering
```

### Synonym handling

```text
"storage"
"disk space"
"disk usage"
"drive full"
```

may all map to related concepts.

### Slang / informal language

```text
"what's eating my disk"
```

→

```text
disk usage analysis
```

### Typos and poor grammar

```text
"how do i searh inside txt files"
```

→

```text
text search
recursive file search
```

### Intent disambiguation

```text
"monitor cpu"
```

could mean:

- live CPU usage
- process-level CPU usage
- CPU profiling
- hardware monitoring

The parser may produce multiple possible intents and confidence scores rather than forcing one interpretation.

---

# 7. What the Local LLM Should NOT Do

The model should not be the primary source for:

- Exact command syntax
- Flags and options
- Version-specific behavior
- Distribution-specific package names
- Security-sensitive instructions
- Destructive commands
- System modification procedures

Those should come from local documentation or trusted metadata.

The model should not be treated as a command generator.

It should not autonomously execute anything.

---

# 8. How Small Can the Local Model Be?

The intended model should be **small**.

The project does not need a large general-purpose LLM.

Potential model scale:

```text
~20M parameters
~50M parameters
~100M parameters
```

The exact model size should be determined experimentally.

The design target is approximately:

> Small enough to load quickly, run entirely locally, and disappear from memory after a request.

A model around 100M parameters is still tiny compared with modern general-purpose language models.

A quantized model may require only on the order of tens to a few hundred MB of memory depending on architecture, runtime, precision, and context size.

The project should prioritize:

1. Fast startup
2. Low RAM usage
3. CPU-friendly inference
4. Small disk footprint
5. High intent-normalization quality
6. Minimal hallucination surface

The model does not need impressive prose generation.

---

# 9. Ephemeral Model Loading

The local model should not remain resident in memory.

Desired workflow:

```text
User invokes tool
       ↓
Load model
       ↓
Parse intent
       ↓
Unload model
       ↓
Perform local search
       ↓
Display results
```

This avoids the tool becoming a permanently running background process.

A user should be able to have the utility installed for months without it constantly consuming RAM.

---

# 10. Resource Usage Goals

These are targets, not guarantees.

### Idle

```text
RAM:
essentially zero

CPU:
zero

Background processes:
none
```

### During a request

Target:

```text
CPU:
temporary burst

RAM:
low hundreds of MB or less,
preferably substantially lower for the chosen model

GPU:
not required
```

### Disk

The model should ideally remain small enough that installation feels lightweight.

The user should never need a dedicated GPU merely to ask:

```bash
what "find duplicate files"
```

---

# 11. Quantization

The final implementation should consider a quantized representation of the intent model.

Potential goals:

- Reduce RAM usage
- Reduce model size
- Improve CPU inference
- Improve startup time

The exact format and runtime are implementation decisions.

The project should benchmark several small models rather than selecting one solely from parameter count.

---

# 12. Structured Intent Output

The local model should ideally return structured data rather than free-form prose.

Conceptual example:

```json
{
  "intent": "find duplicate files",
  "concepts": [
    "duplicate files",
    "deduplication",
    "file comparison"
  ],
  "operations": [
    "search",
    "compare"
  ],
  "entities": [
    "files"
  ],
  "constraints": [],
  "confidence": 0.94
}
```

Another:

```json
{
  "intent": "analyze disk usage",
  "concepts": [
    "disk usage",
    "storage",
    "large files",
    "directory size"
  ],
  "operations": [
    "inspect"
  ],
  "entities": [
    "filesystem"
  ],
  "confidence": 0.91
}
```

The application, not the model, should decide what to do with these fields.

---

# 13. No-LLM Fallback

The project should still function if:

- The model is missing
- The model failed to load
- The model crashes
- The user disabled local AI
- The machine is too constrained

Fallback chain:

```text
Local LLM
    ↓ failure
Keyword / fuzzy matching
    ↓
man / apropos / package metadata
    ↓
Results
```

This makes the model an enhancement rather than a hard dependency for the search architecture.

---

# 14. Local Documentation Sources

The first search layer should exploit existing Linux infrastructure.

## 14.1 `apropos` / `man -k`

These are especially valuable because they already search descriptions.

Conceptually:

```bash
apropos archive
```

or:

```bash
man -k archive
```

The project should use their databases rather than reimplementing every man-page indexing mechanism during the first version.

---

## 14.2 `whatis`

Useful for concise descriptions.

Example concept:

```bash
whatis tar
```

The output can help produce compact candidate descriptions.

---

## 14.3 `man`

Once a candidate is found, the project should be able to access the actual manual.

Example:

```bash
man tar
```

The application can use the manual to extract or display:

- NAME
- SYNOPSIS
- DESCRIPTION
- common options
- examples where available

The final UI should not replace `man`; it should point the user toward it.

---

## 14.4 `--help`

Some programs expose useful information outside man pages.

Potential search path:

```bash
tool --help
```

This should be considered carefully because running arbitrary commands merely to inspect help output can have edge cases.

The first version may limit this to known-safe binaries or rely on package metadata and existing docs first.

---

## 14.5 Package Metadata

Package managers expose information that can help discover tools not currently installed.

For example:

- package name
- short description
- repository
- installed status
- version

Distribution support should be modular.

Possible package-manager adapters:

```text
pacman
apt
dnf
zypper
apk
xbps
```

Not every package manager has to be supported in version 1.

---

# 15. Candidate Generation

After intent normalization, the application generates candidate tools.

Potential candidate sources:

```text
man descriptions
apropos results
package descriptions
installed executables
documentation
known command metadata
```

Example:

```text
Intent:
"find large files"

Potential candidates:
find
du
ncdu
dust
fd
```

The system should not blindly show every match.

Ranking is required.

---

# 16. Ranking System

Ranking should combine multiple signals.

Possible signals:

### Semantic relevance

How strongly does the candidate match the requested intent?

### Description relevance

How closely does the candidate's documentation describe the task?

### Installed status

A locally installed tool can be ranked higher when appropriate.

Example:

```text
✓ ncdu
✗ dust
```

### Exactness

A tool explicitly designed for the requested task should outrank a generic tool capable of accomplishing it.

### Simplicity

When two tools are similarly relevant, prefer the one whose common usage is easier to understand.

### Documentation quality

Tools with strong local documentation may be easier for the user to learn.

### Distribution availability

If a tool is not installed, the application can optionally indicate that it exists in the configured package repositories.

---

# 17. Suggested Result Format

The output should be compact enough for normal terminal use.

Example:

```text
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Intent
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Find duplicate files

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Best Match
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

fdupes

Detects duplicate files by comparing file contents.

Example:
  fdupes -r ~/Downloads

Installed:
  ✓ yes

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Alternatives
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

rdfind
  Finds duplicate files and can identify originals.

czkawka-cli
  Multi-purpose duplicate / cleanup utility.

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Learn
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

man fdupes
```

The exact visual design is intentionally undecided.

---

# 18. Installation Awareness

The tool should distinguish between:

```text
Installed
Not installed
Unknown
```

Example:

```text
Best Match:
dust

Installed:
✗

Available:
✓ package repository

Install:
Not automatically executed.
```

The system should not install anything automatically.

It may show the package manager command as a suggestion, but the user remains responsible for executing it.

---

# 19. Command Examples

Examples should come from trusted local documentation whenever possible.

Preferred source order:

```text
1. Explicit examples in local man / info documentation
2. Local --help content
3. Known package documentation
4. Carefully curated metadata
5. Model-generated wording only as a last resort
```

The application should avoid inventing syntax.

If it cannot verify an example, it should say so.

---

# 20. Confidence Handling

Not every request has one obvious answer.

Example:

```bash
what "monitor performance"
```

Possible interpretations:

```text
CPU / RAM monitoring
Process monitoring
Application profiling
Hardware monitoring
```

Instead of hallucinating a single answer:

```text
Your request could mean:

1. Live system resource monitoring
2. Per-process resource usage
3. Program performance profiling

Choose:
[1] [2] [3]
```

Alternatively, show the top few categories ranked by confidence.

---

# 21. Clarification Mode

The application may ask one concise clarification when the intent is too ambiguous.

Example:

```bash
what "watch cpu"
```

Output:

```text
What are you trying to do?

1. See live CPU usage
2. Find which process uses CPU
3. Profile a program
```

This keeps the local model from having to guess.

---

# 22. Learning-Oriented Behavior

The tool should encourage users to learn the discovered tool.

A result should naturally lead toward:

```text
man <tool>
```

or:

```text
<tool> --help
```

Potential commands:

```bash
what "search files"
what --learn "search files"
what --man ripgrep
```

The exact command syntax is undecided.

---

# 23. Tool vs Command Discovery

The system should distinguish between:

### Built-in shell functionality

Example:

```text
cd
export
alias
```

### Standard system commands

Example:

```text
cp
mv
find
tar
```

### External utilities

Example:

```text
rg
fd
fzf
ncdu
```

### GUI / hybrid tools

Potentially:

```text
meld
file managers
image utilities
```

The project is terminal-first, but it does not have to restrict results to strictly terminal-only software if a GUI tool is genuinely the best fit.

---

# 24. "There Is a Tool for That" Mode

One of the project's strongest UX moments should be when the user does not realize a dedicated utility exists.

Example:

```bash
what "quickly browse and filter files from terminal"
```

Result:

```text
You may be looking for:

fzf
fd
ranger
zoxide

Best fit:
fzf
```

This turns the tool into an exploration mechanism for the broader Linux ecosystem.

---

# 25. Potential Semantic Search Layer

A later local enhancement may introduce embeddings.

Concept:

```text
User intent
    ↓
Embedding
    ↓
Local document/tool embeddings
    ↓
Nearest candidates
```

This could improve matching beyond keyword search.

It may ultimately prove unnecessary if the tiny intent parser + classic search system performs well enough.

Therefore:

> Semantic search is an optimization / enhancement, not a core requirement for the initial version.

---

# 26. Initial MVP

The first practical version should be deliberately small.

## MVP responsibilities

1. Accept natural-language input
2. Run a local small intent model
3. Produce structured concepts / keywords
4. Search local man-page metadata
5. Search package metadata when useful
6. Identify installed status
7. Rank candidates
8. Display concise descriptions
9. Show a safe, verified example when available
10. Offer the relevant `man` documentation

No cloud AI.

No autonomous execution.

No system modification.

No long-running background process.

---

# 27. Example MVP Flow

Input:

```bash
what "how do I see which folders are taking the most space"
```

### Step 1 — Local LLM

Output:

```text
Intent:
disk usage analysis

Concepts:
disk usage
directory size
storage
large directories
```

### Step 2 — Search

Search:

```text
man database
package metadata
installed commands
```

### Step 3 — Candidate generation

Candidates:

```text
du
ncdu
dust
```

### Step 4 — Ranking

Possible result:

```text
1. ncdu
2. du
3. dust
```

### Step 5 — Response

```text
Best Match:
ncdu

Interactive disk usage browser.

Example:
  ncdu ~

Installed:
✓

Learn:
  man ncdu
```

---

# 28. Optional Future Cloud Troubleshooting Service

The cloud feature is a separate subsystem.

It should not be required for normal operation.

The local tool should solve discovery problems on its own whenever possible.

The cloud feature is for cases like:

> "I found the tool, tried it, and something is still wrong."

---

# 29. Cloud Feature Philosophy

The cloud model is a **consultant**, not an operator.

It may:

- Analyze a problem
- Interpret an error
- Suggest tools
- Suggest diagnostic commands
- Explain likely causes
- Recommend next steps

It must not:

- Execute shell commands
- Modify files
- Delete files
- Install packages
- Change system configuration
- Control services
- Modify network settings
- Run with elevated privileges
- Receive arbitrary write/edit/run tools

The human remains the operator.

---

# 30. Cloud Context Bundle

When the user explicitly requests cloud assistance, the client can build a structured evidence pack.

Potential contents:

## User intent

```text
"Why can't I connect to this server?"
```

## Current user message

The complete current problem description.

## Relevant command history

Potentially:

```text
last 10-50 relevant shell commands
```

Not necessarily the entire history.

Relevance filtering should happen locally.

## Error output

For example:

```text
Permission denied
connection refused
command not found
```

## Exit codes

If available.

## Operating-system information

Potentially:

```text
distribution
kernel
architecture
shell
package manager
```

## Relevant tool information

If the local discovery engine identified:

```text
ssh
systemctl
ip
nmcli
```

the client can gather the relevant local documentation.

## Exact relevant man pages

For example:

```text
man ssh
man ssh_config
```

The cloud model can then reason using the user's exact local documentation rather than relying entirely on its own memory.

---

# 31. Context Collection Must Be Permissioned

The user should explicitly opt into deeper collection.

Example:

```text
Cloud troubleshooting needs additional context.

Possible information:
- OS and kernel version
- recent relevant commands
- command errors
- relevant documentation

Send this information?

[y/N]
```

The user should understand what is being sent.

---

# 32. Machine Scanning

The future cloud service may optionally inspect additional local context.

However, scanning should be:

- Explicit
- Narrow
- User-approved
- Purpose-driven
- Read-only

The cloud model should not decide what to scan.

The local application should decide which collectors are appropriate.

Example:

```text
Potentially relevant files:

~/project/package.json
~/project/README.md
~/project/config.toml

Inspect these read-only?

[y/N]
```

---

# 33. Strict Cloud Permission Model

The cloud model should receive data, not capabilities.

Desired architecture:

```text
             LOCAL MACHINE

Collectors
   │
   ├── history
   ├── errors
   ├── system info
   ├── docs
   └── user-approved files
          │
          ▼
      Evidence Pack
          │
          ▼
       Cloud Model
          │
          ▼
    Advice / suggestions
          │
          ▼
        Human
```

Not:

```text
Cloud Model
    ↓
Shell tool
    ↓
Local machine
```

The second architecture is explicitly out of scope.

---

# 34. Cloud Model Output

The model should return structured advice.

Example:

```text
Likely cause:
SSH is reaching the host, but authentication is failing.

Suggested diagnostics:

1. Check whether the expected key exists.
2. Run:
   ssh -v user@host

3. Check the relevant permissions.

Relevant documentation:
   man ssh
   man ssh_config

Confidence:
High
```

The user executes the suggested steps manually.

---

# 35. Cloud Mode Should Preserve Local Authority

The cloud model should never override local documentation.

If the local man page says something different from the model's generic knowledge, the local documentation should be treated as the stronger source for that machine's environment.

This is especially important for:

- version-specific flags
- distribution-specific behavior
- package versions
- configuration paths
- shell behavior

---

# 36. Privacy Design

Privacy should be a first-class consideration.

The default product should work entirely offline.

Cloud assistance should require explicit user action.

The system should avoid sending:

- Entire home directories
- Full shell history by default
- Secrets
- Credentials
- Environment variables containing secrets
- SSH private keys
- Tokens
- Password files
- Browser cookies
- Arbitrary personal documents

The context collector should include redaction / filtering mechanisms.

Potential secret patterns should be detected before transmission.

---

# 37. Cloud Service Is Later

The cloud service should not be part of the initial MVP.

Suggested development order:

```text
Phase 1
Local discovery

Phase 2
Local intent model refinement

Phase 3
Better ranking / semantic search

Phase 4
Advanced local troubleshooting

Phase 5+
Optional cloud troubleshooting
```

The core product should be useful even if the cloud service never exists.

---

# 38. Explicit Non-Goals

The project should NOT become:

- An autonomous terminal agent
- A shell replacement
- A command execution agent
- A package installation manager
- A system optimizer
- A system cleaner
- A destructive automation tool
- A generic chatbot
- A permanently running daemon

The project should remain focused on discovery and guidance.

---

# 39. Safety Principles

### Never auto-execute

The application should not silently run suggested commands.

### Never auto-install

If a tool is missing, the application may show installation instructions but should not install it automatically.

### Never fabricate documentation

If the project cannot find a trustworthy example, say so.

### Never pretend confidence

Low-confidence results should be marked as such.

### Never silently upload information

Cloud assistance must be explicit.

### Never give the cloud model machine-control tools

This is a permanent architectural boundary.

---

# 40. Potential Advanced Features

These should not distract from the MVP.

## 40.1 Command comparison

```text
what "search text"
```

could show:

```text
grep
ripgrep
ag
```

with differences such as:

```text
Traditional
Fast
Recursive by default
Regex support
Installed status
```

---

## 40.2 Alternative awareness

If the user is asking for a familiar tool:

```text
what "grep faster"
```

the tool might explain:

```text
You may be looking for:
ripgrep (rg)
```

---

## 40.3 Distribution awareness

The system may tailor availability information to the current distro.

Example:

```text
Available in configured repositories:
✓
```

---

## 40.4 Learning mode

A command such as:

```bash
what --learn "search files"
```

could present a small progression:

```text
Basic:
rg pattern

Recursive:
rg pattern directory

File filtering:
rg pattern -g '*.py'
```

with links into documentation.

---

## 40.5 Interactive discovery

Potential UI:

```text
Search:
> find duplicate files

Results:
[1] fdupes
[2] rdfind
[3] czkawka-cli

Select:
```

This could make the tool useful even without natural-language generation.

---

# 41. Example Scenarios

## Scenario A — New Linux User

User:

```bash
what "how do i search for a file by name"
```

Tool:

```text
Best matches:

find
fd

Recommended:
fd

Reason:
Simpler and more user-friendly for common filename searches.

Example:
  fd filename

Learn:
  man fd
```

---

## Scenario B — Experienced User Forgot a Tool

User:

```bash
what "there's that terminal thing that lets me fuzzy find stuff"
```

Tool:

```text
Likely match:
fzf

Why:
General-purpose fuzzy finder.

Example:
  fzf

Learn:
  man fzf
```

---

## Scenario C — Ambiguous Request

User:

```bash
what "monitor system"
```

Tool:

```text
Possible meanings:

1. Live CPU/RAM/process monitoring
2. Hardware monitoring
3. Program profiling

Choose one.
```

---

## Scenario D — Discovery Fails Locally

User:

```bash
what "fix this weird network issue"
```

Tool:

```text
I found several possible categories:

- Network connectivity
- DNS
- Wi-Fi
- Routing
- Firewall

I need more information.

Try:
what "my Wi-Fi disconnects every few minutes"
```

Or, later:

```text
Need more help?
[Ask cloud AI]
```

---

## Scenario E — Cloud Troubleshooting

User explicitly requests cloud assistance.

Local system gathers:

```text
Intent:
SSH connection failure

Error:
Permission denied (publickey)

Recent relevant command:
ssh user@server

System:
Linux x86_64

Relevant documentation:
man ssh
man ssh_config
```

Cloud model returns:

```text
Likely issue:
The server is rejecting the available authentication method.

Suggested next steps:
1. Run:
   ssh -v user@server

2. Check which identity files are being offered.

3. Compare the client configuration with ssh_config.

No commands were executed.
```

---

# 42. Development Strategy

The project should be developed in layers.

## Stage 1 — Search Prototype

Build the basic pipeline without the LLM.

```text
Input
→ keyword extraction
→ apropos / man database
→ ranking
→ output
```

Goal:

Prove that useful command discovery is possible using local documentation.

---

## Stage 2 — Intent Parser

Add the tiny local model.

```text
Input
→ local LLM
→ structured intent
→ search
→ ranking
→ output
```

Benchmark whether the model meaningfully improves results over keyword matching.

---

## Stage 3 — Better Ranking

Add:

- Installed-state weighting
- package metadata
- semantic matching
- description quality
- exactness scoring

---

## Stage 4 — UX Refinement

Improve:

- output formatting
- ambiguity handling
- interactive selection
- documentation jumping
- examples
- errors

---

## Stage 5 — Optional Cloud System

Only after the local system is mature.

Add:

- explicit user opt-in
- evidence-pack construction
- secret redaction
- read-only context collectors
- cloud troubleshooting
- strict no-execution interface

---

# 43. Testing Strategy

The project should build a large test collection of natural-language requests.

Examples:

```text
"find files"
"find big files"
"find duplicate files"
"search text"
"watch cpu"
"see ram usage"
"compress a folder"
"extract tar.gz"
"compare folders"
"find which process is using a port"
"show network connections"
"rename lots of files"
"convert images"
"check disk space"
"see what is using storage"
"fuzzy search terminal"
```

Each request should have expected candidate categories.

The system should be evaluated on:

- Top-1 relevance
- Top-3 relevance
- Correct ambiguity detection
- False-positive rate
- Hallucinated tool rate
- Startup time
- RAM usage
- Search latency

---

# 44. Local Model Evaluation

Do not select a model only because it has the smallest parameter count.

Measure:

```text
Intent accuracy
Ambiguity detection
Typo tolerance
Slang tolerance
CPU inference time
Startup time
RAM usage
Disk size
```

The ideal model is the smallest model that provides a meaningful improvement over the non-LLM baseline.

---

# 45. Important Architectural Principle

The project should be designed so that the model can be replaced.

Conceptually:

```text
IntentParser
    ├── RuleBasedParser
    ├── TinyLocalLLMParser
    └── FutureAlternativeParser
```

The rest of the application should not care which parser produced the intent object.

This keeps the core system stable while allowing experiments with:

- Different models
- Different runtimes
- Embedding-based parsing
- Pure rule-based parsing

---

# 46. Offline-First Requirement

The core discovery system should work without:

- Internet access
- API keys
- User accounts
- Cloud servers

The only major external dependency should be whatever the user already uses for installing software / obtaining package metadata.

The tool should remain useful on an offline machine using locally available documentation.

---

# 47. Performance Philosophy

The utility should feel like a normal shell command.

A user should not type:

```bash
what "find duplicate files"
```

and then wait for an enormous AI model to boot.

Performance targets should feel closer to:

```text
invoke
→ tiny model load
→ parse
→ search
→ output
```

The model's job is small enough that responsiveness is more important than conversational sophistication.

---

# 48. Potential Future Architecture

A mature version could eventually look like:

```text
                     ┌─────────────────────┐
                     │     User Input      │
                     └──────────┬──────────┘
                                │
                                ▼
                   ┌────────────────────────┐
                   │ Local Intent Parser     │
                   │ Tiny Quantized LLM      │
                   └───────────┬────────────┘
                               │
                               ▼
                   ┌────────────────────────┐
                   │ Intent / Query Object  │
                   └───────────┬────────────┘
                               │
               ┌───────────────┼────────────────┐
               │               │                │
               ▼               ▼                ▼
        Man / Info DB     Package DB      Local Metadata
               │               │                │
               └───────────────┼────────────────┘
                               ▼
                     ┌────────────────────┐
                     │ Candidate Ranking  │
                     └─────────┬──────────┘
                               │
                               ▼
                     ┌────────────────────┐
                     │ Human-Friendly UI  │
                     └─────────┬──────────┘
                               │
                      ┌────────┴─────────┐
                      │                  │
                      ▼                  ▼
                 Learn locally      Need more help?
                                         │
                                         ▼
                               Explicit Cloud Opt-In
                                         │
                                         ▼
                                Context / Evidence
                                         │
                                         ▼
                                   Cloud Model
                                         │
                                         ▼
                                      Advice
```

---

# 48.1 Implementation Language Strategy

## 48.1.1 Decision

The tool will ultimately ship as a **native C++ binary**, matching the performance goals in Sections 8–11 (fast startup, low RAM, no interpreter overhead) and fitting naturally alongside the existing Realmheart C++ codebase and toolchain.

## 48.1.2 Prototype-first approach

The C++ version will **not** be written from a blank spec. Instead:

```text
Phase 1: Build the full pipeline in Python
    ↓
Use it daily, stress-test the ranking logic and UX
    ↓
Let the Python version reveal what the tool actually needs
    ↓
Rewrite in C++ using the Python version as the reference implementation
```

The Python version is a disposable prototype, not a foundation to preserve. It should stay scrappy and un-abstracted — the goal is to answer design questions fast (does the LLM meaningfully help? what does ranking need to weigh? what should output formatting look like?), not to produce clean, reusable code. Over-engineering the prototype risks becoming reluctant to throw it away, which defeats the purpose of prototyping.

The Python prototype effectively front-loads Stages 1–3 of the Development Strategy (Section 42) before any C++ is written.

---

# 48.2 Distribution Strategy — AUR

## 48.2.1 Target audience

Acclorite is explicitly **not** scoped to Realmheart users. It is intended as a general-purpose Linux utility, useful to new and experienced Linux users alike, and should be packaged for the broadest reasonable audience — starting with Arch/AUR.

## 48.2.2 Dependency philosophy

AUR packages succeed or fail based on how clean their dependency tree is. Core discovery (Stage 1: fuzzy search + `apropos`/`man` + package metadata) must install and run with a minimal, standard dependency set and **zero** bundled model weights or mandatory downloads.

## 48.2.3 Optional LLM backend via Ollama

Rather than bundling or vendoring a local model, the LLM-assisted intent parsing (Section 6) should integrate with **Ollama** as an optional backend:

```text
Ollama running locally?
    → use it for intent parsing (e.g. a small model like qwen2.5:0.5b)
Ollama not found / not running?
    → silently fall back to fuzzy search (Section 13's No-LLM Fallback)
```

This keeps Acclorite's own package lightweight:

- Ollama is already available in the AUR and commonly pre-installed on Arch systems
- listed as an `optdepends` entry, not a hard dependency
- the user manages their own model pull (`ollama pull qwen2.5:0.5b`) — Acclorite's package never ships or touches model weights
- Acclorite just calls Ollama's local API (`localhost:11434`) if present

This preserves every principle from Sections 6–13 (local-first, no hard AI dependency, model as enhancement not requirement) while making the AUR install trivial:

```text
1. paru -S acclorite              → works immediately via fuzzy search
2. paru -S ollama && ollama pull qwen2.5:0.5b   → optional smarter matching
```

---

# 48.3 The Lore / Quote System

## 48.3.1 Concept

Acclorite includes a hidden, non-essential feature layered on top of the core discovery tool: a curated set of TBATE quotes, unattached to any explanation, that surface as flavor text.

This is purely cosmetic and never interferes with the tool's actual output or function.

## 48.3.2 Behavior

Rather than being gated behind a single flag, a quote has a **random chance of appearing at the bottom of any normal command's output** — not on every run, so it never feels spammy, but often enough to be a pleasant surprise:

```text
acclorite "find duplicate files"

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Best Match: fdupes
Detects duplicate files by comparing file contents.

Example:
  fdupes -r ~/Downloads

Learn:
  man fdupes
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  "You're not slow, the world is just moving too fast for its own good."
                                                              — Regis
```

A dedicated `--lore` flag may still exist as a way to pull a quote on demand without running a real query, but the primary delivery mechanism is the random appearance after ordinary tool output.

## 48.3.3 Quote source and scope

Quotes are pulled from a bundled local file (e.g. `quotes.json`), one quote + attribution per entry, randomly selected at runtime. The list is intended to be extensive — not a "top 10 greatest hits" — spanning popular and niche lines across many characters, expected to skew heavily toward Regis given how quotable he is throughout the series.

The quote file should ship as static local data (no network calls, no generation), consistent with the project's offline-first principle (Section 46).

## 48.3.4 Relationship to the naming secrecy (Section 4.3)

The quotes reinforce the same "no explanation, let it be discovered" design language as the tool's name. Nothing in the tool ever states that it is TBATE-themed. A user who doesn't recognize the quotes simply sees pleasant, mysterious flavor text. A user who does recognize them gets the full picture without ever being told outright.

---

# 49. Final Product Definition

The clearest definition of the project is:

> A local-first Linux tool-discovery assistant that translates natural-language intent into relevant commands and utilities by searching the system's own documentation and metadata, using a tiny local language model primarily to understand human intent.

The future optional cloud component extends this into:

> A read-only troubleshooting consultant that can analyze explicitly provided local context and documentation, but cannot execute commands or modify the machine.

---

# 50. Design Rules to Preserve

These should remain visible throughout implementation.

1. **The tool discovers; the user operates.**
2. **Local documentation is the authority.**
3. **The local LLM parses intent; it does not need to be a Linux encyclopedia.**
4. **The smallest useful model is preferred.**
5. **The model should load on demand rather than run permanently.**
6. **No cloud dependency for core discovery.**
7. **No automatic command execution.**
8. **No automatic package installation.**
9. **No silent data collection.**
10. **Cloud troubleshooting is optional and much later.**
11. **Cloud models receive information, not machine-control tools.**
12. **When uncertain, ask the user rather than hallucinate.**
13. **The final destination should usually be the real Linux documentation.**
14. **The project should help users discover Linux tools, not hide them.**
15. **The name and lore theme are never explained in the README. Discovery is the feature.**

---

# 51. The One-Sentence Vision

```text
"I know what I want to do — just tell me what Linux tool I should be looking at."
```

That is the product.

Everything else should support that idea rather than replace it.
