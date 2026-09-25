# Acclorite Benchmark Report

- Version: `acclorite 0.3.3`
- Corpus: `acclorite-actionable-v3`
- Generated: `2026-09-12T12:00:19.508766+00:00`
- Cases: **8/8** passed (100.0%)
- Assertions: **69/69** passed (100.0%)
- Latency: p50 **232.8 ms**, p95 **326.3 ms**, max **326.4 ms**
- Internal search: p50 **228.6 ms**, p95 **322.2 ms**, max **322.3 ms**

## Profiled Stages

| Stage | Samples | p50 | p95 | Max |
|---|---:|---:|---:|---:|
| `syntax:man-child` | 2 | 139.9 ms | 140.5 ms | 140.5 ms |
| `syntax:man` | 8 | 73.6 ms | 115.4 ms | 115.6 ms |
| `guidance:man` | 8 | 71.2 ms | 112.3 ms | 115.4 ms |
| `enricher:pkgfile` | 8 | 30.7 ms | 35.9 ms | 37.7 ms |
| `source:index` | 8 | 3.1 ms | 5.7 ms | 5.8 ms |
| `answer-compose` | 8 | 1.8 ms | 141.2 ms | 141.6 ms |
| `source:arch-packages` | 8 | 1.8 ms | 16.2 ms | 16.3 ms |
| `ranking-finalize` | 8 | 0.9 ms | 1.2 ms | 1.2 ms |
| `ranking-preferences` | 8 | 0.9 ms | 1.1 ms | 1.1 ms |
| `ranking-prefilter` | 8 | 0.8 ms | 1.0 ms | 1.0 ms |
| `guidance:info` | 8 | 0.3 ms | 0.3 ms | 0.3 ms |
| `syntax:fish-completion-child` | 2 | 0.1 ms | 0.1 ms | 0.1 ms |
| `guidance:tldr` | 8 | 0.1 ms | 0.1 ms | 0.1 ms |
| `confidence` | 8 | 0.1 ms | 0.1 ms | 0.1 ms |
| `syntax:fish-completion` | 8 | 0.0 ms | 0.2 ms | 0.3 ms |
| `ranking-remerge` | 8 | 0.0 ms | 0.0 ms | 0.0 ms |
| `ranking-sort` | 8 | 0.0 ms | 0.1 ms | 0.1 ms |
| `guidance:curated` | 8 | 0.0 ms | 0.0 ms | 0.0 ms |

| Case | Query | Top 1 | Ambiguity | Confidence | Latency | Result |
|---|---|---|---|---:|---:|---|
| `systemctl-status-bind-unit` | systemctl show me what nginx is doing | `systemctl` | `clear` | 0.988 | 173.7 ms | **PASS** |
| `systemctl-status-bind-many` | systemctl show what nginx and sshd are doing | `systemctl` | `clear` | 0.982 | 193.0 ms | **PASS** |
| `ffmpeg-seek-bind-value` | ffmpeg start reading this video from 30 seconds in | `ffmpeg` | `clear` | 0.955 | 272.6 ms | **PASS** |
| `ffmpeg-explain-does-not-bind` | what does ffmpeg -ss do | `ffmpeg` | `clear` | 0.979 | 274.7 ms | **PASS** |
| `git-create-and-switch-child-man` | git make a fresh branch called feature/foo and put me on it | `git` | `clear` | 0.979 | 326.2 ms | **PASS** |
| `git-create-and-go-there-child-man` | git make a new branch called release/test and go there | `git` | `clear` | 0.979 | 326.4 ms | **PASS** |
| `git-create-only-does-not-add-switch` | git make a fresh branch called feature/only | `git` | `clear` | 0.979 | 187.8 ms | **PASS** |
| `rg-hidden-root-grammar-insufficient` | rg is skipping dotfiles, make it search them too | `rg` | `clear` | 0.961 | 151.8 ms | **PASS** |
