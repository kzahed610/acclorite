# Acclorite Benchmark Report

- Version: `acclorite 0.3.3`
- Corpus: `acclorite-human-language-v1`
- Generated: `2026-09-12T13:09:47.351028+00:00`
- Cases: **25/25** passed (100.0%)
- Assertions: **85/85** passed (100.0%)
- Latency: p50 **130.9 ms**, p95 **323.3 ms**, max **382.6 ms**
- Internal search: p50 **125.5 ms**, p95 **316.9 ms**, max **378.3 ms**

## Profiled Stages

| Stage | Samples | p50 | p95 | Max |
|---|---:|---:|---:|---:|
| `syntax:man-child` | 1 | 178.6 ms | 178.6 ms | 178.6 ms |
| `syntax:man` | 7 | 74.1 ms | 139.4 ms | 153.0 ms |
| `guidance:man` | 25 | 68.5 ms | 117.8 ms | 136.8 ms |
| `enricher:pkgfile` | 25 | 12.3 ms | 53.6 ms | 98.8 ms |
| `source:index` | 25 | 5.1 ms | 9.6 ms | 10.4 ms |
| `ranking-finalize` | 25 | 3.0 ms | 21.0 ms | 53.4 ms |
| `ranking-preferences` | 25 | 2.9 ms | 20.9 ms | 53.2 ms |
| `ranking-prefilter` | 25 | 2.6 ms | 22.6 ms | 51.3 ms |
| `answer-compose` | 7 | 1.8 ms | 127.2 ms | 179.8 ms |
| `source:arch-packages` | 25 | 1.6 ms | 15.1 ms | 21.5 ms |
| `guidance:info` | 25 | 0.3 ms | 12.3 ms | 15.1 ms |
| `confidence` | 25 | 0.1 ms | 0.8 ms | 1.3 ms |
| `syntax:fish-completion-child` | 1 | 0.1 ms | 0.1 ms | 0.1 ms |
| `guidance:tldr` | 25 | 0.1 ms | 1.9 ms | 3.7 ms |
| `syntax:fish-completion` | 7 | 0.1 ms | 6.0 ms | 8.0 ms |
| `ranking-sort` | 25 | 0.1 ms | 0.1 ms | 0.1 ms |
| `ranking-remerge` | 25 | 0.0 ms | 0.2 ms | 0.3 ms |
| `guidance:curated` | 25 | 0.0 ms | 0.0 ms | 0.0 ms |

| Case | Query | Top 1 | Ambiguity | Confidence | Latency | Result |
|---|---|---|---|---:|---:|---|
| `unknown-command-extract-targz` | What is that command that can extract tar.gz files in terminal | `tar` | `clear` | 0.882 | 109.8 ms | **PASS** |
| `downloaded-targz-unpack` | i downloaded some .tar.gz thing how tf do i unpack it | `tar` | `clear` | 0.872 | 101.5 ms | **PASS** |
| `compress-directory-one-file` | got a directory and need to turn it into one compressed file from terminal | `zip` | `competitive` | 0.722 | 115.3 ms | **PASS** |
| `disk-full-what-ate-it` | my drive is full what the hell ate all the space | `duf` | `competitive` | 0.776 | 214.7 ms | **PASS** |
| `port-stolen-8080` | what terminal thing tells me which process stole port 8080 | `fuser` | `low-confidence` | 0.580 | 130.9 ms | **PASS** |
| `disk-exploding-large-files` | need to hunt down giant files before my disk explodes | `fd` | `low-confidence` | 0.488 | 170.0 ms | **PASS** |
| `grep-everything-todo` | grep through everything under this folder for TODO | `grep` | `low-confidence` | 0.425 | 214.0 ms | **PASS** |
| `compare-dirs-whats-different` | what can compare these two directories and tell me what's different | `meld` | `clear` | 0.838 | 28.1 ms | **PASS** |
| `rename-five-hundred-files` | i have like 500 files and need to rename all of them | `rename` | `low-confidence` | 0.550 | 201.4 ms | **PASS** |
| `png-to-jpg-bunch` | turn a bunch of png files into jpg from terminal | `mogrify` | `clear` | 0.800 | 103.3 ms | **PASS** |
| `duplicate-files-again` | what's that terminal thing for finding duplicate files again | `rdfind` | `clear` | 0.896 | 31.7 ms | **PASS** |
| `connections-on-this-box` | need to see open connections on this box | `lsof` | `low-confidence` | 0.551 | 202.5 ms | **PASS** |
| `rg-dotfiles-without-flag-word` | rg is skipping dotfiles, make it search them too | `rg` | `clear` | 0.961 | 169.3 ms | **PASS** |
| `curl-3xx-follow-without-flag-word` | curl keeps getting 3xx responses and stopping, make it follow the redirect | `curl` | `clear` | 0.961 | 334.3 ms | **PASS** |
| `git-branch-and-go-there` | git make a fresh branch called feature/foo and put me on it | `git` | `clear` | 0.979 | 382.6 ms | **PASS** |
| `systemctl-nginx-status-natural` | systemctl show me what nginx is doing | `systemctl` | `clear` | 0.988 | 165.2 ms | **PASS** |
| `ffmpeg-start-thirty-seconds` | ffmpeg start reading this video from 30 seconds in | `ffmpeg` | `clear` | 0.955 | 279.0 ms | **PASS** |
| `rg-todo-ignore-build` | rg search TODO here but don't go into build | `rg` | `clear` | 0.961 | 172.8 ms | **PASS** |
| `typo-unzipz-targz` | wht cmd unzipz targz | `tar` | `clear` | 0.869 | 97.7 ms | **PASS** |
| `who-tf-owns-port` | who tf owns port 3000 | `fuser` | `low-confidence` | 0.580 | 81.6 ms | **PASS** |
| `ram-hogs-command` | which cmd shows me the ram hogs | `btop` | `clear` | 0.880 | 85.5 ms | **PASS** |
| `folder-size-readable` | need a command for folder size but readable not raw bytes | `pdu` | `competitive` | 0.778 | 15.5 ms | **PASS** |
| `tarball-command-again` | what was that command for extracting a tarball again | `tar` | `low-confidence` | 0.518 | 101.8 ms | **PASS** |
| `file-contents-not-filenames` | the thing that recursively searches inside file contents not filenames | `rg` | `low-confidence` | 0.557 | 192.7 ms | **PASS** |
| `watch-cpu-live-something` | something that can watch cpu usage live | `btop` | `low-confidence` | 0.580 | 79.9 ms | **PASS** |
