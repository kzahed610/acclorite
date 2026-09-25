# Acclorite Benchmark Report

- Version: `acclorite 0.3.3`
- Corpus: `acclorite-core-arch-v1`
- Generated: `2026-09-12T13:09:43.362048+00:00`
- Cases: **25/25** passed (100.0%)
- Assertions: **65/65** passed (100.0%)
- Latency: p50 **111.5 ms**, p95 **222.4 ms**, max **271.3 ms**
- Internal search: p50 **107.2 ms**, p95 **216.0 ms**, max **265.0 ms**

## Profiled Stages

| Stage | Samples | p50 | p95 | Max |
|---|---:|---:|---:|---:|
| `syntax:man` | 1 | 95.2 ms | 95.2 ms | 95.2 ms |
| `guidance:man` | 25 | 59.0 ms | 121.5 ms | 151.7 ms |
| `enricher:pkgfile` | 25 | 37.1 ms | 54.8 ms | 66.0 ms |
| `source:index` | 25 | 5.2 ms | 8.0 ms | 8.3 ms |
| `source:arch-packages` | 25 | 2.4 ms | 17.9 ms | 28.1 ms |
| `ranking-finalize` | 25 | 2.0 ms | 10.4 ms | 31.8 ms |
| `ranking-preferences` | 25 | 1.8 ms | 10.2 ms | 31.4 ms |
| `ranking-prefilter` | 25 | 1.8 ms | 13.2 ms | 28.9 ms |
| `syntax:fish-completion` | 1 | 0.7 ms | 0.7 ms | 0.7 ms |
| `guidance:tldr` | 25 | 0.4 ms | 4.4 ms | 6.2 ms |
| `location-source` | 1 | 0.3 ms | 0.3 ms | 0.3 ms |
| `guidance:info` | 25 | 0.3 ms | 8.7 ms | 17.8 ms |
| `confidence` | 25 | 0.1 ms | 0.2 ms | 0.6 ms |
| `ranking-sort` | 25 | 0.1 ms | 0.1 ms | 0.2 ms |
| `ranking-remerge` | 25 | 0.1 ms | 0.3 ms | 0.4 ms |
| `answer-compose` | 1 | 0.1 ms | 0.1 ms | 0.1 ms |
| `guidance:curated` | 25 | 0.0 ms | 0.0 ms | 0.0 ms |

| Case | Query | Top 1 | Ambiguity | Confidence | Latency | Result |
|---|---|---|---|---:|---:|---|
| `search-text` | search text | `rg` | `clear` | 0.806 | 145.8 ms | **PASS** |
| `search-inside-files` | search inside files recursively | `rg` | `low-confidence` | 0.580 | 221.4 ms | **PASS** |
| `search-pdf-text` | search pdf text | `pdfgrep` | `clear` | 0.897 | 59.3 ms | **PASS** |
| `search-compressed` | search compressed files | `zipgrep` | `clear` | 0.799 | 86.2 ms | **PASS** |
| `full-text-search` | full text search | `recoll` | `clear` | 0.869 | 57.8 ms | **PASS** |
| `duplicate-files` | duplicate files | `fdupes` | `competitive` | 0.747 | 37.2 ms | **PASS** |
| `find-duplicate-files` | find duplicate files | `rdfind` | `clear` | 0.869 | 26.7 ms | **PASS** |
| `disk-usage` | disk usage | `duf` | `competitive` | 0.750 | 132.4 ms | **PASS** |
| `disk-space-human` | what is using all my disk space | `duf` | `competitive` | 0.776 | 111.5 ms | **PASS** |
| `network-connections` | network connections | `ss` | `clear` | 0.869 | 120.2 ms | **PASS** |
| `show-network-connections` | show open network connections | `ss` | `low-confidence` | 0.580 | 98.4 ms | **PASS** |
| `port-owner` | which process is using port 8080 | `fuser` | `clear` | 0.885 | 72.8 ms | **PASS** |
| `process-monitoring` | process monitoring | `btop` | `clear` | 0.776 | 104.8 ms | **PASS** |
| `ram-human` | what is eating RAM | `btop` | `clear` | 0.885 | 96.8 ms | **PASS** |
| `watch-cpu` | watch CPU usage | `btop` | `clear` | 0.877 | 69.7 ms | **PASS** |
| `archive-ambiguous` | archive files | `cpio` | `ambiguous` | 0.622 | 128.4 ms | **PASS** |
| `compress-folder` | compress a folder | `zip` | `competitive` | 0.722 | 110.9 ms | **PASS** |
| `extract-targz` | extract a tar.gz | `tar` | `clear` | 0.869 | 189.9 ms | **PASS** |
| `compare-folders` | compare two folders | `meld` | `clear` | 0.838 | 35.9 ms | **PASS** |
| `large-files` | find large files | `find` | `low-confidence` | 0.580 | 155.6 ms | **PASS** |
| `bulk-rename` | rename lots of files | `rename` | `competitive` | 0.792 | 271.3 ms | **PASS** |
| `convert-images` | convert images | `convert` | `competitive` | 0.762 | 182.0 ms | **PASS** |
| `explain-rg` | what is rg | `rg` | `clear` | 0.985 | 168.5 ms | **PASS** |
| `compare-rg-grep` | difference between rg and grep | `rg` | `clear` | 0.972 | 222.6 ms | **PASS** |
| `locate-ssh-config` | where is ssh config | `ssh` | `clear` | 0.985 | 210.8 ms | **PASS** |
