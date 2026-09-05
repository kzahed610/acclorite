# Acclorite Benchmark Report

- Version: `acclorite 0.2.0`
- Corpus: `acclorite-core-arch-v1`
- Generated: `2026-09-05T08:09:50.170986+00:00`
- Cases: **25/25** passed (100.0%)
- Assertions: **65/65** passed (100.0%)
- Latency: p50 **487.1 ms**, p95 **1524.2 ms**, max **2886.0 ms**
- Internal search: p50 **477.6 ms**, p95 **1514.5 ms**, max **2876.5 ms**

## Profiled Stages

| Stage | Samples | p50 | p95 | Max |
|---|---:|---:|---:|---:|
| `source:index` | 25 | 175.8 ms | 265.8 ms | 301.2 ms |
| `guidance:man` | 25 | 119.7 ms | 256.6 ms | 312.7 ms |
| `enricher:pkgfile` | 25 | 51.8 ms | 101.4 ms | 111.7 ms |
| `ranking-finalize` | 25 | 28.9 ms | 466.3 ms | 829.8 ms |
| `ranking-prefilter` | 25 | 28.7 ms | 490.6 ms | 822.1 ms |
| `source:arch-packages` | 25 | 27.2 ms | 450.9 ms | 656.2 ms |
| `ranking-preferences` | 25 | 26.7 ms | 463.5 ms | 826.5 ms |
| `location-source` | 1 | 2.2 ms | 2.2 ms | 2.2 ms |
| `ranking-sort` | 25 | 2.1 ms | 3.1 ms | 3.3 ms |
| `confidence` | 25 | 0.9 ms | 6.7 ms | 13.9 ms |
| `ranking-remerge` | 25 | 0.4 ms | 0.6 ms | 0.6 ms |

| Case | Query | Top 1 | Ambiguity | Confidence | Latency | Result |
|---|---|---|---|---:|---:|---|
| `search-text` | search text | `rg` | `clear` | 0.806 | 521.0 ms | **PASS** |
| `search-inside-files` | search inside files recursively | `rg` | `low-confidence` | 0.580 | 2886.0 ms | **PASS** |
| `search-pdf-text` | search pdf text | `pdfgrep` | `clear` | 0.897 | 370.2 ms | **PASS** |
| `search-compressed` | search compressed files | `zipgrep` | `clear` | 0.799 | 903.6 ms | **PASS** |
| `full-text-search` | full text search | `recoll` | `clear` | 0.892 | 785.6 ms | **PASS** |
| `duplicate-files` | duplicate files | `fdupes` | `competitive` | 0.747 | 246.5 ms | **PASS** |
| `find-duplicate-files` | find duplicate files | `rdfind` | `clear` | 0.869 | 258.9 ms | **PASS** |
| `disk-usage` | disk usage | `duf` | `clear` | 0.857 | 483.3 ms | **PASS** |
| `disk-space-human` | what is using all my disk space | `duf` | `clear` | 0.867 | 487.1 ms | **PASS** |
| `network-connections` | network connections | `ss` | `clear` | 0.869 | 529.0 ms | **PASS** |
| `show-network-connections` | show open network connections | `ss` | `low-confidence` | 0.580 | 780.6 ms | **PASS** |
| `port-owner` | which process is using port 8080 | `fuser` | `clear` | 0.885 | 419.0 ms | **PASS** |
| `process-monitoring` | process monitoring | `btop` | `clear` | 0.776 | 342.5 ms | **PASS** |
| `ram-human` | what is eating RAM | `btop` | `clear` | 0.885 | 521.9 ms | **PASS** |
| `watch-cpu` | watch CPU usage | `btop` | `clear` | 0.877 | 450.7 ms | **PASS** |
| `archive-ambiguous` | archive files | `cpio` | `ambiguous` | 0.622 | 448.7 ms | **PASS** |
| `compress-folder` | compress a folder | `zip` | `competitive` | 0.722 | 470.7 ms | **PASS** |
| `extract-targz` | extract a tar.gz | `tar` | `clear` | 0.774 | 303.5 ms | **PASS** |
| `compare-folders` | compare two folders | `meld` | `clear` | 0.866 | 142.9 ms | **PASS** |
| `large-files` | find large files | `find` | `low-confidence` | 0.556 | 1482.9 ms | **PASS** |
| `bulk-rename` | rename lots of files | `rename` | `competitive` | 0.771 | 898.2 ms | **PASS** |
| `convert-images` | convert images | `convert` | `competitive` | 0.761 | 1534.5 ms | **PASS** |
| `explain-rg` | what is rg | `rg` | `clear` | 0.985 | 241.4 ms | **PASS** |
| `compare-rg-grep` | difference between rg and grep | `rg` | `clear` | 0.972 | 531.1 ms | **PASS** |
| `locate-ssh-config` | where is ssh config | `ssh` | `clear` | 0.985 | 734.8 ms | **PASS** |
