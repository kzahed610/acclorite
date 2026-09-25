# Acclorite Benchmark Report

- Version: `acclorite 0.3.3`
- Corpus: `acclorite-actionable-v2`
- Generated: `2026-09-12T09:24:32.603592+00:00`
- Cases: **8/8** passed (100.0%)
- Assertions: **69/69** passed (100.0%)
- Latency: p50 **202.7 ms**, p95 **381.9 ms**, max **386.9 ms**
- Internal search: p50 **198.8 ms**, p95 **377.3 ms**, max **381.8 ms**

## Profiled Stages

| Stage | Samples | p50 | p95 | Max |
|---|---:|---:|---:|---:|
| `syntax:man-child` | 2 | 160.3 ms | 176.9 ms | 178.7 ms |
| `syntax:man` | 8 | 74.9 ms | 97.6 ms | 97.8 ms |
| `guidance:man` | 8 | 69.9 ms | 96.8 ms | 97.1 ms |
| `enricher:pkgfile` | 8 | 27.7 ms | 33.9 ms | 34.6 ms |
| `source:index` | 8 | 2.9 ms | 5.4 ms | 5.6 ms |
| `answer-compose` | 8 | 1.6 ms | 166.9 ms | 180.0 ms |
| `source:arch-packages` | 8 | 1.6 ms | 18.8 ms | 21.6 ms |
| `ranking-finalize` | 8 | 0.8 ms | 1.1 ms | 1.2 ms |
| `ranking-preferences` | 8 | 0.8 ms | 1.0 ms | 1.1 ms |
| `ranking-prefilter` | 8 | 0.8 ms | 0.8 ms | 0.8 ms |
| `guidance:info` | 8 | 0.3 ms | 0.4 ms | 0.4 ms |
| `guidance:tldr` | 8 | 0.1 ms | 0.1 ms | 0.1 ms |
| `confidence` | 8 | 0.1 ms | 0.1 ms | 0.1 ms |
| `ranking-remerge` | 8 | 0.0 ms | 0.0 ms | 0.0 ms |
| `ranking-sort` | 8 | 0.0 ms | 0.0 ms | 0.1 ms |
| `guidance:curated` | 8 | 0.0 ms | 0.0 ms | 0.0 ms |

| Case | Query | Top 1 | Ambiguity | Confidence | Latency | Result |
|---|---|---|---|---:|---:|---|
| `systemctl-status-bind-unit` | systemctl show me what nginx is doing | `systemctl` | `clear` | 0.988 | 154.1 ms | **PASS** |
| `systemctl-status-bind-many` | systemctl show what nginx and sshd are doing | `systemctl` | `clear` | 0.982 | 159.8 ms | **PASS** |
| `ffmpeg-seek-bind-value` | ffmpeg start reading this video from 30 seconds in | `ffmpeg` | `clear` | 0.955 | 238.8 ms | **PASS** |
| `ffmpeg-explain-does-not-bind` | what does ffmpeg -ss do | `ffmpeg` | `clear` | 0.979 | 235.6 ms | **PASS** |
| `git-create-and-switch-child-man` | git make a fresh branch called feature/foo and put me on it | `git` | `clear` | 0.979 | 372.7 ms | **PASS** |
| `git-create-and-go-there-child-man` | git make a new branch called release/test and go there | `git` | `clear` | 0.979 | 386.9 ms | **PASS** |
| `git-create-only-does-not-add-switch` | git make a fresh branch called feature/only | `git` | `clear` | 0.979 | 169.9 ms | **PASS** |
| `rg-hidden-root-grammar-insufficient` | rg is skipping dotfiles, make it search them too | `rg` | `clear` | 0.961 | 149.1 ms | **PASS** |
