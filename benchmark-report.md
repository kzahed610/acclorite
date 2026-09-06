# Acclorite Benchmark Report

- Version: `acclorite 0.2.6`
- Corpus: `acclorite-core-arch-v1`
- Generated: `2026-09-06T10:48:15.720551+00:00`
- Cases: **25/25** passed (100.0%)
- Assertions: **65/65** passed (100.0%)
- Latency: p50 **535.2 ms**, p95 **1574.2 ms**, max **3805.8 ms**
- Internal search: p50 **510.6 ms**, p95 **1562.7 ms**, max **3789.4 ms**

## Profiled Stages

| Stage | Samples | p50 | p95 | Max |
|---|---:|---:|---:|---:|
| `source:index` | 25 | 184.3 ms | 340.0 ms | 380.6 ms |
| `guidance:man` | 25 | 125.2 ms | 296.3 ms | 319.5 ms |
| `enricher:pkgfile` | 25 | 53.1 ms | 109.8 ms | 137.4 ms |
| `ranking-finalize` | 25 | 35.0 ms | 510.2 ms | 1323.1 ms |
| `source:arch-packages` | 25 | 32.8 ms | 538.2 ms | 713.3 ms |
| `ranking-preferences` | 25 | 32.4 ms | 506.6 ms | 1316.9 ms |
| `ranking-prefilter` | 25 | 31.1 ms | 480.2 ms | 959.3 ms |
| `guidance:info` | 25 | 2.6 ms | 22.2 ms | 26.3 ms |
| `ranking-sort` | 25 | 2.4 ms | 4.7 ms | 5.6 ms |
| `confidence` | 25 | 1.0 ms | 6.9 ms | 18.5 ms |
| `guidance:tldr` | 25 | 0.9 ms | 2.9 ms | 3.3 ms |
| `ranking-remerge` | 25 | 0.5 ms | 0.9 ms | 1.0 ms |
| `location-source` | 1 | 0.2 ms | 0.2 ms | 0.2 ms |
| `guidance:curated` | 25 | 0.0 ms | 0.0 ms | 0.0 ms |

| Case | Query | Top 1 | Ambiguity | Confidence | Latency | Result |
|---|---|---|---|---:|---:|---|
| `search-text` | search text | `rg` | `clear` | 0.806 | 629.0 ms | **PASS** |
| `search-inside-files` | search inside files recursively | `rg` | `low-confidence` | 0.580 | 3805.8 ms | **PASS** |
| `search-pdf-text` | search pdf text | `pdfgrep` | `clear` | 0.897 | 535.2 ms | **PASS** |
| `search-compressed` | search compressed files | `zipgrep` | `clear` | 0.799 | 1251.5 ms | **PASS** |
| `full-text-search` | full text search | `recoll` | `clear` | 0.892 | 854.1 ms | **PASS** |
| `duplicate-files` | duplicate files | `fdupes` | `competitive` | 0.747 | 292.8 ms | **PASS** |
| `find-duplicate-files` | find duplicate files | `rdfind` | `clear` | 0.869 | 302.9 ms | **PASS** |
| `disk-usage` | disk usage | `duf` | `clear` | 0.857 | 569.1 ms | **PASS** |
| `disk-space-human` | what is using all my disk space | `duf` | `clear` | 0.867 | 523.9 ms | **PASS** |
| `network-connections` | network connections | `ss` | `clear` | 0.869 | 621.2 ms | **PASS** |
| `show-network-connections` | show open network connections | `ss` | `low-confidence` | 0.580 | 848.1 ms | **PASS** |
| `port-owner` | which process is using port 8080 | `fuser` | `clear` | 0.885 | 460.4 ms | **PASS** |
| `process-monitoring` | process monitoring | `btop` | `clear` | 0.776 | 397.9 ms | **PASS** |
| `ram-human` | what is eating RAM | `btop` | `clear` | 0.885 | 498.6 ms | **PASS** |
| `watch-cpu` | watch CPU usage | `btop` | `clear` | 0.877 | 420.3 ms | **PASS** |
| `archive-ambiguous` | archive files | `cpio` | `ambiguous` | 0.622 | 500.5 ms | **PASS** |
| `compress-folder` | compress a folder | `zip` | `competitive` | 0.722 | 447.1 ms | **PASS** |
| `extract-targz` | extract a tar.gz | `tar` | `clear` | 0.774 | 326.2 ms | **PASS** |
| `compare-folders` | compare two folders | `meld` | `clear` | 0.866 | 154.1 ms | **PASS** |
| `large-files` | find large files | `find` | `low-confidence` | 0.556 | 1543.1 ms | **PASS** |
| `bulk-rename` | rename lots of files | `rename` | `competitive` | 0.771 | 884.1 ms | **PASS** |
| `convert-images` | convert images | `convert` | `competitive` | 0.761 | 1582.0 ms | **PASS** |
| `explain-rg` | what is rg | `rg` | `clear` | 0.985 | 245.4 ms | **PASS** |
| `compare-rg-grep` | difference between rg and grep | `rg` | `clear` | 0.972 | 558.2 ms | **PASS** |
| `locate-ssh-config` | where is ssh config | `ssh` | `clear` | 0.985 | 798.4 ms | **PASS** |
