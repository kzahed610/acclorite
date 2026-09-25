# Acclorite Benchmark Report

- Version: `acclorite 0.3.3`
- Corpus: `acclorite-actionable-v1`
- Generated: `2026-09-12T05:34:26.524493+00:00`
- Cases: **6/6** passed (100.0%)
- Assertions: **43/43** passed (100.0%)
- Latency: p50 **183.1 ms**, p95 **312.4 ms**, max **319.8 ms**
- Internal search: p50 **177.9 ms**, p95 **306.8 ms**, max **313.6 ms**

## Profiled Stages

| Stage | Samples | p50 | p95 | Max |
|---|---:|---:|---:|---:|
| `guidance:man` | 6 | 72.1 ms | 127.4 ms | 131.1 ms |
| `syntax:man` | 6 | 69.7 ms | 122.6 ms | 123.7 ms |
| `enricher:pkgfile` | 6 | 24.3 ms | 50.2 ms | 54.0 ms |
| `source:index` | 6 | 3.7 ms | 6.1 ms | 6.5 ms |
| `answer-compose` | 6 | 1.6 ms | 1.9 ms | 1.9 ms |
| `source:arch-packages` | 6 | 1.4 ms | 12.7 ms | 16.0 ms |
| `ranking-prefilter` | 6 | 0.5 ms | 0.9 ms | 0.9 ms |
| `ranking-finalize` | 6 | 0.5 ms | 1.1 ms | 1.1 ms |
| `ranking-preferences` | 6 | 0.4 ms | 1.0 ms | 1.0 ms |
| `guidance:info` | 6 | 0.3 ms | 0.5 ms | 0.6 ms |
| `guidance:tldr` | 6 | 0.1 ms | 0.1 ms | 0.1 ms |
| `confidence` | 6 | 0.1 ms | 0.1 ms | 0.1 ms |
| `ranking-remerge` | 6 | 0.0 ms | 0.0 ms | 0.0 ms |
| `ranking-sort` | 6 | 0.0 ms | 0.0 ms | 0.0 ms |
| `guidance:curated` | 6 | 0.0 ms | 0.0 ms | 0.0 ms |

| Case | Query | Top 1 | Ambiguity | Confidence | Latency | Result |
|---|---|---|---|---:|---:|---|
| `systemctl-status-bind-unit` | systemctl show me what nginx is doing | `systemctl` | `clear` | 0.988 | 155.2 ms | **PASS** |
| `systemctl-status-bind-many` | systemctl show what nginx and sshd are doing | `systemctl` | `clear` | 0.982 | 159.3 ms | **PASS** |
| `ffmpeg-seek-bind-value` | ffmpeg start reading this video from 30 seconds in | `ffmpeg` | `clear` | 0.955 | 290.2 ms | **PASS** |
| `ffmpeg-explain-does-not-bind` | what does ffmpeg -ss do | `ffmpeg` | `clear` | 0.979 | 319.8 ms | **PASS** |
| `git-branch-root-grammar-insufficient` | git make a fresh branch called feature/foo and put me on it | `git` | `clear` | 0.979 | 204.7 ms | **PASS** |
| `rg-hidden-root-grammar-insufficient` | rg is skipping dotfiles, make it search them too | `rg` | `clear` | 0.961 | 161.5 ms | **PASS** |
