# Acclorite machine interface

Acclorite v0.3.5 preserves the Milestone 7 compatibility contract first
published in v0.2.5 and frozen in v0.2.6 for non-interactive clients such as Realmheart. This document describes the public
CLI/JSON boundary. Internal C++ types, ranking weights, cache schemas, and exact
floating-point scores are not part of this contract.

## Runtime guarantees

Normal Acclorite operation is local-first and non-interactive:

- search, `doctor`, capability detection, and local guidance do not prompt;
- normal runtime does not fetch network resources;
- package discovery reads already-synchronized local package metadata;
- TLDR guidance reads an already-populated local cache and never updates it;
- curated HTTP(S) targets are provenance/learning links and are not fetched by
  Acclorite during a query;
- `--reindex` only rebuilds Acclorite's own local cache.

Successful machine output is written to stdout. CLI misuse and operational
errors are written to stderr. A valid search that finds no result still emits a
complete search JSON document when `--json` is requested.

## Exit codes

The following process exit codes are stable beginning with v0.2.5:

| Code | Name | Meaning |
|---:|---|---|
| 0 | `success` | The requested operation completed successfully. |
| 1 | `no_result` | A valid search completed but returned no candidate or location. |
| 2 | `usage_error` | The invocation is invalid: missing query, unknown option, or incompatible flags. |
| 3 | `operational_error` | Acclorite itself could not complete an operation, such as a failed explicit reindex or an uncaught runtime exception. |
| 4 | `diagnostic_error` | `doctor` completed successfully and found a fundamental broken-state diagnostic. |

Warnings reported by `doctor` do not use exit code 4. They are represented in
the doctor JSON/status model while the command exits successfully.

## Capability discovery

`acclorite --capabilities` emits JSON even without `--json`. For consistency,
`acclorite --json --capabilities` is also accepted.

The capability document has its own compatibility version:

```json
{
  "capabilities_schema_version": 1,
  "acclorite_version": "0.3.5",
  "search_schema_version": 15,
  "doctor_schema_version": 1,
  "offline_runtime": true,
  "non_interactive": true,
  "commands": {
    "search": true,
    "doctor": true,
    "reindex": true
  },
  "output": {
    "json": true,
    "ranking_explanations": true,
    "profiling": true
  },
  "build": {
    "sqlite_fts": true
  },
  "integrations": {
    "manual_guidance": true,
    "manual_syntax": true,
    "fish_completion_syntax": true,
    "info_guidance": true,
    "tldr_cache": true,
    "curated_guidance": true,
    "arch_packages": true,
    "apt_packages": false,
    "dnf_packages": false,
    "zypper_packages": false,
    "xbps_packages": false,
    "pkgfile": true
  },
  "exit_codes": {
    "success": 0,
    "no_result": 1,
    "usage_error": 2,
    "operational_error": 3,
    "diagnostic_error": 4
  }
}
```

The integration booleans are deliberately cheap local detection, not health
checks. `manual_syntax` means the local `man` frontend required by the current
manual syntax provider is available; it does not assert that every candidate has a
parseable manual. `fish_completion_syntax` means at least one static system/admin
Fish completion root is present; it does not assert that a particular command has
a parseable `<command>.fish` declaration. The capability covers the conservative
static-option parser and the allowlisted literal subcommand-condition subset; Fish
conditions and completion scripts are still never executed. `arch_packages`, `apt_packages`, `dnf_packages`, `zypper_packages`, and `xbps_packages` indicate which supported local package
backend is selected for the current environment; they are mutually exclusive in
normal operation. For example, `pkgfile: true` means the executable was detected
on the active Arch backend; it does not prove that its `.files` metadata is populated. Use `acclorite --json doctor`
when readiness/degradation detail is required. Capability detection must not
spawn subprocesses, rebuild caches, update metadata, or access the network.

## Search JSON compatibility

Search JSON schema 15 is the Milestone-7 compatibility baseline. The currently
documented top-level fields are:

```text
schema_version
query
normalized_query
timing
query_frame
confidence
clarifications
targets
action_target
locations
actionable_answer
results
```

`action_target` is an additive schema-15 field. It is `null` for ordinary
discovery/explanation queries that are not asking about syntax. When present it is:

```json
{
  "kind": "option",
  "command": "curl",
  "literal": null,
  "terms": ["follow", "redirects"],
  "explicit_syntax": false
}
```

`kind` is currently one of `option`, `subcommand`, `positional`, or `operation`.
`command` and `literal` are nullable. A literal option is copied from the raw query
and therefore preserves case-sensitive syntax exactly; clients must not reconstruct it
from `normalized_query`. `terms[]` contains deterministic capability terms for semantic
syntax questions. `explicit_syntax` distinguishes literal syntax such as `-L` from a
request such as “what flag makes curl follow redirects”. For command-addressed human language that
does not name a syntax category, Acclorite may first create an internal `operation` target. If verified
grammar proves that operation is implemented by exactly one root option or subcommand, the emitted
`action_target.kind` is promoted to that resolved kind before rendering JSON. Compound operations may instead
remain `operation` while `actionable_answer` contains both a proven subcommand and a proven child option (for
example a create-and-switch operation). If grammar is missing or ambiguous, it remains `operation` and
`actionable_answer` stays `null`. A subcommand action target may produce an answer when a provider can prove a
matching local subcommand entry.

`results[]` currently documents:

```text
command
path
summary
source
package
repository
package_version
installed
repository_available
interface
cli_capable
gui_capable
matched_terms
provided_commands
examples
learning_resources
score
ranking
```

Guidance records under `examples[]` and `learning_resources[]` carry explicit
source provenance. Schema 15 includes `verified_by` and `verified_on` so bundled
curated assertions can be distinguished from documentation verified locally at
query time.

`actionable_answer` is an additive nullable schema-15 field introduced by the
Milestone-10 syntax foundation. It is `null` when Acclorite cannot prove an
actionable syntax fact for the query. Implemented paths include explicit/semantic option inspection (for example,
`what does ffmpeg -ss do` or `which rg flag includes hidden files`) and semantic
subcommand selection when a local man command section proves the answer. When present,
the object currently documents:

```text
command
explanation
invocation
relevant_options[]
relevant_subcommands[]
examples[]
resources[]
safety
```

`relevant_options[]` contains `names`, `description`, nullable `value_name`,
`value_shape_known`, `takes_value`, `value_required`, and a `provenance` object with `source_type`,
`source_reference`, and `section`. `value_shape_known=false` means the source proves the option identity/description but does not prove whether that option consumes a value; machine clients must not infer arity from `takes_value=false` in that state. Syntax provenance is separate from ordinary
guidance provenance because a source-backed example is not automatically
authoritative command grammar.

`relevant_subcommands[]` contains `name`, `description`, and a `provenance` object with
`source_type`, `source_reference`, and `section`. A populated subcommand entry identifies verified grammar; it
does not imply that Acclorite has bound required child arguments or constructed a complete invocation. Compound
answers may populate both `relevant_subcommands[]` and `relevant_options[]`; in that case option provenance may
point at a parent-derived child page such as `man:git-switch` while the subcommand identity remains proven by
`man:git`.

Explain-only option/subcommand inspection still leaves `invocation = null`: inspecting syntax is not the
same as requesting an execution shape. For action-shaped queries, the first deterministic binder may now
populate `invocation` when verified grammar exposes bindable slots. `arguments[]` remains the ordered string
projection, while additive `segments[]` entries expose `{ value, placeholder }` for each argument and
`complete` states whether every required slot in one proven syntax path is satisfied. For nested child-command
answers, completeness additionally requires one explicit child `SYNOPSIS` alternative to contain the selected
option (or a proven alias), account for its required value, and leave no other mandatory syntax unmet. A generic
`[<options>]` group is only permission, not proof of the chosen path. An incomplete invocation is a verified
template/partial command, not a claim that it is ready to execute. Unsupported or unproven explicit flags still
leave `actionable_answer = null` rather than producing guessed syntax.

Schema-15 enum domains are stable:

```text
query_frame.type: Discover | Explain | Locate | Inspect | Modify | Compare | Diagnose | Unknown
confidence.level: low | medium | high
confidence.ambiguity: clear | competitive | ambiguous | low-confidence | no-result
results[].interface: Unknown | CLI | GUI | Hybrid
examples[].source_type / learning_resources[].source_type: man | info | tldr | curated | package
actionable_answer.relevant_options[]/relevant_subcommands[].provenance.source_type: man | info | completion | curated
actionable_answer.safety: ReadOnly | Mutating | Destructive | Privileged | Network | Unknown
```

`timing` is `null` unless profiling is requested; with `--profile` it is an
object containing `total_ms` and `stages[]`, where every stage has `name`,
`milliseconds`, and integer `items`. Relevant actionable queries may add `syntax:<provider>`, `syntax:<provider>-child`, and `answer-compose` timing
stages; repeated child-stage entries are additive timing samples. Ordinary discovery bypasses syntax providers
entirely. `results[].ranking` is `null` unless
`--explain-ranking` is requested; when present it is a structured ranking
breakdown. A valid no-result search still emits schema 15, has empty `results`
and `locations`, reports `confidence.ambiguity = no-result`, and exits with code
1.

`locations[]` entries document `path`, `kind`, `source`, and numeric `score`.
`clarifications[]` entries document string `id` and `label`. Guidance records
document source type/reference, boolean `verified`, and string audit fields
`verified_by` / `verified_on` (empty strings are valid for runtime-verified local
sources).

The `results` array is ordered by Acclorite's current ranking policy. Ranking
weights and exact floating-point scores remain tunable implementation details.
Clients should use structured confidence/ambiguity fields rather than assuming a
hard-coded raw-score threshold.

## Doctor JSON compatibility

Doctor uses an independent `doctor_schema_version` because diagnostic checks and
search results evolve independently. Schema 1 documents:

```text
doctor_schema_version
status
mutated_system
checks[]
```

Each `checks[]` entry documents:

```text
id
section
label
state
detail
hint
```

Stable Doctor document statuses are `healthy`, `degraded`, and `broken`. Stable
check states are `ready`, `warning`, `optional`, and `error`. `mutated_system` is
a boolean and remains `false` for the read-only Doctor contract.

## Compatibility rules

Beginning with the Milestone-7 baseline:

1. Clients must ignore unknown object fields.
2. Adding an optional field is backward-compatible and does not by itself
   require a schema-version increment.
3. Removing a documented field, renaming it, changing its JSON type or documented
   nullability, removing/renaming a documented enum value, or changing documented
   meaning requires the relevant schema version to increment.
4. Search, Doctor, and capability documents version independently.
5. Array ordering is meaningful only where documented. In particular,
   `results[]` is ranking order; clients must not infer undocumented ordering
   from unrelated arrays.
6. Exact ranking weights, score magnitudes, timing values, cache paths, and
   internal source implementations are not compatibility promises.
7. CLI exit-code meanings above are part of the public machine interface and do
   not change without an explicit compatibility break.

## CLI command-mode rules

The stable command shapes are:

```text
acclorite [--json] [--explain-ranking] [--profile] <query...>
acclorite [--json] doctor
acclorite --reindex
acclorite [--json] --capabilities
acclorite --version
acclorite --help
```

`--reindex` is a standalone maintenance command. Unknown options are usage
errors rather than query text. A standard `--` delimiter ends option parsing if
a query intentionally needs a leading-hyphen token.

## Contract-freeze tests

`v0.2.6` adds `acclorite_schema_contract_tests`, an end-to-end structural suite
that invokes the compiled executable. It validates required fields, JSON types,
enum domains, nullability, schema/exit-code pairing, representative search
frames, location/guidance/ranking/timing records, healthy/broken Doctor output,
and capability output. The suite deliberately permits unknown additive object
fields and does not snapshot exact scores, timings, paths, ranking weights, or
other implementation-dependent values.

This is the executable definition of the Milestone-7 freeze: compatible
additions should pass; client-visible breaking changes should fail until the
relevant schema contract is intentionally advanced.
