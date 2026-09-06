# Acclorite Benchmark Report

- Version: `acclorite 0.2.4`
- Corpus: `acclorite-core-arch-v1`
- Generated: `2026-09-06T07:19:02.893122+00:00`
- Cases: **25/25** passed (100.0%)
- Assertions: **65/65** passed (100.0%)
- Latency: p50 **561.6 ms**, p95 **1649.0 ms**, max **2865.2 ms**
- Internal search: p50 **549.4 ms**, p95 **1636.1 ms**, max **2853.9 ms**

## Profiled Stages

| Stage | Samples | p50 | p95 | Max |
|---|---:|---:|---:|---:|
| `source:index` | 25 | 182.2 ms | 291.8 ms | 320.9 ms |
| `guidance:man` | 25 | 142.2 ms | 343.4 ms | 424.6 ms |
| `enricher:pkgfile` | 25 | 64.6 ms | 134.7 ms | 210.7 ms |
| `ranking-finalize` | 25 | 33.2 ms | 480.9 ms | 813.1 ms |
| `ranking-preferences` | 25 | 30.6 ms | 477.7 ms | 809.7 ms |
| `source:arch-packages` | 25 | 30.5 ms | 459.2 ms | 616.7 ms |
| `ranking-prefilter` | 25 | 29.5 ms | 522.1 ms | 831.6 ms |
| `guidance:info` | 25 | 2.8 ms | 24.1 ms | 27.7 ms |
| `ranking-sort` | 25 | 2.4 ms | 3.2 ms | 3.7 ms |
| `location-source` | 1 | 1.2 ms | 1.2 ms | 1.2 ms |
| `confidence` | 25 | 1.0 ms | 7.1 ms | 12.5 ms |
| `guidance:tldr` | 25 | 0.9 ms | 3.8 ms | 4.4 ms |
| `ranking-remerge` | 25 | 0.5 ms | 0.6 ms | 0.7 ms |
| `guidance:curated` | 25 | 0.0 ms | 0.0 ms | 0.1 ms |

| Case | Query | Top 1 | Ambiguity | Confidence | Latency | Result |
|---|---|---|---|---:|---:|---|
| `search-text` | search text | `rg` | `clear` | 0.806 | 519.8 ms | **PASS** |
| `search-inside-files` | search inside files recursively | `rg` | `low-confidence` | 0.580 | 2865.2 ms | **PASS** |
| `search-pdf-text` | search pdf text | `pdfgrep` | `clear` | 0.897 | 391.5 ms | **PASS** |
| `search-compressed` | search compressed files | `zipgrep` | `clear` | 0.799 | 962.5 ms | **PASS** |
| `full-text-search` | full text search | `recoll` | `clear` | 0.892 | 772.6 ms | **PASS** |
| `duplicate-files` | duplicate files | `fdupes` | `competitive` | 0.747 | 274.7 ms | **PASS** |
| `find-duplicate-files` | find duplicate files | `rdfind` | `clear` | 0.869 | 296.1 ms | **PASS** |
| `disk-usage` | disk usage | `duf` | `clear` | 0.857 | 566.7 ms | **PASS** |
| `disk-space-human` | what is using all my disk space | `duf` | `clear` | 0.867 | 561.6 ms | **PASS** |
| `network-connections` | network connections | `ss` | `clear` | 0.869 | 616.3 ms | **PASS** |
| `show-network-connections` | show open network connections | `ss` | `low-confidence` | 0.580 | 874.9 ms | **PASS** |
| `port-owner` | which process is using port 8080 | `fuser` | `clear` | 0.885 | 464.8 ms | **PASS** |
| `process-monitoring` | process monitoring | `btop` | `clear` | 0.776 | 425.0 ms | **PASS** |
| `ram-human` | what is eating RAM | `btop` | `clear` | 0.885 | 561.1 ms | **PASS** |
| `watch-cpu` | watch CPU usage | `btop` | `clear` | 0.877 | 466.9 ms | **PASS** |
| `archive-ambiguous` | archive files | `cpio` | `ambiguous` | 0.622 | 569.8 ms | **PASS** |
| `compress-folder` | compress a folder | `zip` | `competitive` | 0.722 | 496.3 ms | **PASS** |
| `extract-targz` | extract a tar.gz | `tar` | `clear` | 0.774 | 347.3 ms | **PASS** |
| `compare-folders` | compare two folders | `meld` | `clear` | 0.866 | 159.9 ms | **PASS** |
| `large-files` | find large files | `find` | `low-confidence` | 0.556 | 1568.1 ms | **PASS** |
| `bulk-rename` | rename lots of files | `rename` | `competitive` | 0.771 | 983.6 ms | **PASS** |
| `convert-images` | convert images | `convert` | `competitive` | 0.761 | 1669.2 ms | **PASS** |
| `explain-rg` | what is rg | `rg` | `clear` | 0.985 | 302.3 ms | **PASS** |
| `compare-rg-grep` | difference between rg and grep | `rg` | `clear` | 0.972 | 746.4 ms | **PASS** |
| `locate-ssh-config` | where is ssh config | `ssh` | `clear` | 0.985 | 1038.9 ms | **PASS** |
