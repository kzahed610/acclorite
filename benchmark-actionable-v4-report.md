# Acclorite Benchmark Report

- Version: `acclorite 0.3.3`
- Corpus: `acclorite-actionable-v4`
- Generated: `2026-09-12T12:50:47.851295+00:00`
- Cases: **14/14** passed (100.0%)
- Assertions: **138/138** passed (100.0%)
- Latency: p50 **184.9 ms**, p95 **366.1 ms**, max **380.3 ms**
- Internal search: p50 **181.1 ms**, p95 **357.5 ms**, max **374.7 ms**

## Profiled Stages

| Stage | Samples | p50 | p95 | Max |
|---|---:|---:|---:|---:|
| `syntax:man-child` | 2 | 152.6 ms | 156.9 ms | 157.3 ms |
| `guidance:man` | 14 | 68.4 ms | 118.5 ms | 122.6 ms |
| `syntax:man` | 14 | 66.4 ms | 113.7 ms | 116.5 ms |
| `enricher:pkgfile` | 14 | 22.9 ms | 56.4 ms | 57.1 ms |
| `source:index` | 14 | 3.0 ms | 5.2 ms | 5.4 ms |
| `source:arch-packages` | 14 | 2.2 ms | 19.2 ms | 26.4 ms |
| `answer-compose` | 14 | 0.6 ms | 152.2 ms | 158.4 ms |
| `guidance:tldr` | 14 | 0.5 ms | 4.1 ms | 5.2 ms |
| `ranking-finalize` | 14 | 0.5 ms | 1.3 ms | 1.7 ms |
| `ranking-preferences` | 14 | 0.4 ms | 1.1 ms | 1.4 ms |
| `ranking-prefilter` | 14 | 0.4 ms | 0.9 ms | 1.0 ms |
| `guidance:info` | 14 | 0.3 ms | 20.2 ms | 25.2 ms |
| `syntax:fish-completion-child` | 2 | 0.1 ms | 0.1 ms | 0.1 ms |
| `confidence` | 14 | 0.0 ms | 0.1 ms | 0.1 ms |
| `syntax:fish-completion` | 14 | 0.0 ms | 5.4 ms | 7.6 ms |
| `ranking-remerge` | 14 | 0.0 ms | 0.1 ms | 0.3 ms |
| `ranking-sort` | 14 | 0.0 ms | 0.0 ms | 0.0 ms |
| `guidance:curated` | 14 | 0.0 ms | 0.0 ms | 0.0 ms |

| Case | Query | Top 1 | Ambiguity | Confidence | Latency | Result |
|---|---|---|---|---:|---:|---|
| `systemctl-status-bind-unit` | systemctl show me what nginx is doing | `systemctl` | `clear` | 0.988 | 195.3 ms | **PASS** |
| `systemctl-status-bind-many` | systemctl show what nginx and sshd are doing | `systemctl` | `clear` | 0.982 | 172.5 ms | **PASS** |
| `ffmpeg-seek-bind-value` | ffmpeg start reading this video from 30 seconds in | `ffmpeg` | `clear` | 0.955 | 307.5 ms | **PASS** |
| `ffmpeg-explain-does-not-bind` | what does ffmpeg -ss do | `ffmpeg` | `clear` | 0.979 | 308.3 ms | **PASS** |
| `git-create-and-switch-child-man` | git make a fresh branch called feature/foo and put me on it | `git` | `clear` | 0.979 | 380.3 ms | **PASS** |
| `git-create-and-go-there-child-man` | git make a new branch called release/test and go there | `git` | `clear` | 0.979 | 358.4 ms | **PASS** |
| `git-create-only-does-not-add-switch` | git make a fresh branch called feature/only | `git` | `clear` | 0.979 | 178.3 ms | **PASS** |
| `rg-hidden-root-grammar-insufficient` | rg is skipping dotfiles, make it search them too | `rg` | `clear` | 0.961 | 168.0 ms | **PASS** |
| `mv-rename-source-dest` | mv rename ~/uncool-shit to ~/cool-shit | `mv` | `clear` | 0.983 | 116.6 ms | **PASS** |
| `cp-copy-source-dest` | cp copy ~/notes.txt to ~/Backup/notes.txt | `cp` | `clear` | 0.961 | 191.4 ms | **PASS** |
| `mkdir-create-directory` | mkdir create directory ~/Projects/demo | `mkdir` | `clear` | 0.983 | 196.8 ms | **PASS** |
| `grep-pattern-and-file` | grep search TODO in ~/notes.txt | `grep` | `clear` | 0.961 | 174.4 ms | **PASS** |
| `touch-create-file` | touch create file ~/tmp/new-note.txt | `touch` | `clear` | 0.983 | 144.7 ms | **PASS** |
| `rm-remove-file` | rm remove ~/tmp/junk.txt | `rm` | `clear` | 0.983 | 150.7 ms | **PASS** |
