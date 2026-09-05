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
python3 benchmarks/run.py \
  --binary ./build/acclorite \
  --json-out benchmark-report.json \
  --markdown-out benchmark-report.md
```

Use `--strict` when a benchmark failure should produce a non-zero exit status.
The default mode always prints failures but exits successfully unless the
harness itself cannot run; this makes it convenient for exploratory tuning.


## Frozen v0.1.0 gate

`corpus-v1.json` is frozen as the **v0.1.0 regression gate** after the real Arch/CachyOS run reached 25/25 cases and 65/65 assertions. Do not tune future ranking changes by rewriting this corpus to accept new winners. New/adversarial coverage belongs in a new versioned corpus while v1 remains a non-regression contract.

For release validation on an Arch-family machine, run the frozen gate with `--strict`.

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
- `clarification_ids`: deterministic clarification choices that must be present;
- `locations_nonempty`: whether Locate must return at least one path;
- `min_results`: minimum candidate count.

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
