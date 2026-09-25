# Acclorite Benchmark Report

- Version: `acclorite 0.3.3`
- Corpus: `acclorite-actionable-v5`
- Generated: `2026-09-12T13:09:50.456642+00:00`
- Cases: **14/14** passed (100.0%)
- Assertions: **152/152** passed (100.0%)
- Latency: p50 **167.1 ms**, p95 **371.2 ms**, max **399.2 ms**
- Internal search: p50 **162.2 ms**, p95 **362.5 ms**, max **392.9 ms**

## Profiled Stages

| Stage | Samples | p50 | p95 | Max |
|---|---:|---:|---:|---:|
| `syntax:man-child` | 2 | 159.7 ms | 172.2 ms | 173.6 ms |
| `guidance:man` | 14 | 65.7 ms | 120.0 ms | 130.2 ms |
| `syntax:man` | 14 | 63.7 ms | 126.1 ms | 130.5 ms |
| `enricher:pkgfile` | 14 | 13.4 ms | 51.1 ms | 61.1 ms |
| `source:index` | 14 | 3.4 ms | 7.4 ms | 9.9 ms |
| `source:arch-packages` | 14 | 1.7 ms | 18.9 ms | 22.1 ms |
| `answer-compose` | 14 | 0.7 ms | 156.7 ms | 174.9 ms |
| `ranking-prefilter` | 14 | 0.5 ms | 1.0 ms | 1.1 ms |
| `ranking-finalize` | 14 | 0.4 ms | 1.3 ms | 1.4 ms |
| `guidance:info` | 14 | 0.4 ms | 10.4 ms | 10.6 ms |
| `ranking-preferences` | 14 | 0.4 ms | 1.3 ms | 1.4 ms |
| `syntax:fish-completion-child` | 2 | 0.1 ms | 0.1 ms | 0.1 ms |
| `guidance:tldr` | 14 | 0.1 ms | 1.4 ms | 2.4 ms |
| `confidence` | 14 | 0.0 ms | 0.1 ms | 0.1 ms |
| `syntax:fish-completion` | 14 | 0.0 ms | 0.1 ms | 0.3 ms |
| `ranking-remerge` | 14 | 0.0 ms | 0.0 ms | 0.0 ms |
| `ranking-sort` | 14 | 0.0 ms | 0.1 ms | 0.1 ms |
| `guidance:curated` | 14 | 0.0 ms | 0.0 ms | 0.0 ms |

| Case | Query | Top 1 | Ambiguity | Confidence | Latency | Result |
|---|---|---|---|---:|---:|---|
| `systemctl-status-bind-unit` | systemctl show me what nginx is doing | `systemctl` | `clear` | 0.988 | 265.7 ms | **PASS** |
| `systemctl-status-bind-many` | systemctl show what nginx and sshd are doing | `systemctl` | `clear` | 0.982 | 182.6 ms | **PASS** |
| `ffmpeg-seek-bind-value` | ffmpeg start reading this video from 30 seconds in | `ffmpeg` | `clear` | 0.955 | 329.5 ms | **PASS** |
| `ffmpeg-explain-does-not-bind` | what does ffmpeg -ss do | `ffmpeg` | `clear` | 0.979 | 303.6 ms | **PASS** |
| `git-create-and-switch-child-man` | git make a fresh branch called feature/foo and put me on it | `git` | `clear` | 0.979 | 399.2 ms | **PASS** |
| `git-create-and-go-there-child-man` | git make a new branch called release/test and go there | `git` | `clear` | 0.979 | 356.1 ms | **PASS** |
| `git-create-only-does-not-add-switch` | git make a fresh branch called feature/only | `git` | `clear` | 0.979 | 190.2 ms | **PASS** |
| `rg-hidden-root-grammar-insufficient` | rg is skipping dotfiles, make it search them too | `rg` | `clear` | 0.961 | 146.3 ms | **PASS** |
| `mv-rename-source-dest` | mv rename ~/uncool-shit to ~/cool-shit | `mv` | `clear` | 0.983 | 107.9 ms | **PASS** |
| `cp-copy-source-dest` | cp copy ~/notes.txt to ~/Backup/notes.txt | `cp` | `clear` | 0.961 | 115.8 ms | **PASS** |
| `mkdir-create-directory` | mkdir create directory ~/Projects/demo | `mkdir` | `clear` | 0.983 | 114.6 ms | **PASS** |
| `grep-pattern-and-file` | grep search TODO in ~/notes.txt | `grep` | `clear` | 0.961 | 151.6 ms | **PASS** |
| `touch-create-file` | touch create file ~/tmp/new-note.txt | `touch` | `clear` | 0.983 | 130.3 ms | **PASS** |
| `rm-remove-file` | rm remove ~/tmp/junk.txt | `rm` | `clear` | 0.983 | 118.9 ms | **PASS** |
