# Acclorite Benchmark Report

- Version: `acclorite 0.3.3`
- Corpus: `acclorite-m11-research-v1`
- Generated: `2026-09-13T04:21:11.797890+00:00`
- Cases: **3/26** passed (11.5%)
- Assertions: **7/52** passed (13.5%)
- Latency: p50 **141.3 ms**, p95 **220.5 ms**, max **256.7 ms**
- Internal search: p50 **137.4 ms**, p95 **216.5 ms**, max **250.6 ms**

## Profiled Stages

| Stage | Samples | p50 | p95 | Max |
|---|---:|---:|---:|---:|
| `syntax:man` | 1 | 49.2 ms | 49.2 ms | 49.2 ms |
| `guidance:man` | 26 | 43.3 ms | 57.4 ms | 80.6 ms |
| `ranking-finalize` | 26 | 30.5 ms | 55.1 ms | 64.9 ms |
| `ranking-preferences` | 26 | 30.4 ms | 55.0 ms | 64.8 ms |
| `ranking-prefilter` | 26 | 30.0 ms | 53.2 ms | 58.0 ms |
| `enricher:pkgfile` | 26 | 12.9 ms | 58.8 ms | 63.1 ms |
| `source:index` | 26 | 6.9 ms | 9.8 ms | 11.9 ms |
| `source:arch-packages` | 26 | 2.8 ms | 47.4 ms | 52.4 ms |
| `confidence` | 26 | 0.7 ms | 1.1 ms | 1.2 ms |
| `guidance:tldr` | 26 | 0.5 ms | 4.8 ms | 7.4 ms |
| `syntax:fish-completion` | 1 | 0.5 ms | 0.5 ms | 0.5 ms |
| `guidance:info` | 26 | 0.3 ms | 10.2 ms | 15.5 ms |
| `ranking-sort` | 26 | 0.1 ms | 0.2 ms | 0.2 ms |
| `ranking-remerge` | 26 | 0.0 ms | 0.1 ms | 0.1 ms |
| `answer-compose` | 1 | 0.0 ms | 0.0 ms | 0.0 ms |
| `guidance:curated` | 26 | 0.0 ms | 0.0 ms | 0.0 ms |

| Case | Query | Top 1 | Ambiguity | Confidence | Latency | Result |
|---|---|---|---|---:|---:|---|
| `recent-files-by-time` | show me files that changed since yesterday | `era_invalidate` | `low-confidence` | 0.580 | 176.9 ms | **FAIL** |
| `repeat-command-periodically` | keep running the same command every two seconds so i can watch it change | `oh-my-zsh-git` | `low-confidence` | 0.507 | 53.2 ms | **FAIL** |
| `pid-from-process-name` | i know the process name but need its pid | `htop` | `low-confidence` | 0.518 | 172.0 ms | **FAIL** |
| `shared-library-dependencies` | what shared libraries does this executable need at runtime | `intel-oneapi-compiler-shared-runtime` | `low-confidence` | 0.511 | 166.7 ms | **FAIL** |
| `detect-file-type-by-content` | tell me what kind of file this really is without trusting its extension | `shred` | `low-confidence` | 0.445 | 170.5 ms | **FAIL** |
| `split-large-file` | break one huge file into 100 MB pieces | `lwp-download` | `low-confidence` | 0.486 | 165.2 ms | **FAIL** |
| `second-column-text` | print only the second column from whitespace separated text | `column` | `low-confidence` | 0.387 | 196.3 ms | **FAIL** |
| `stdin-one-command-per-line` | run a command once for every line that comes from stdin | `hurl` | `low-confidence` | 0.464 | 195.5 ms | **FAIL** |
| `remove-adjacent-duplicates` | remove repeated neighboring lines from already sorted text | `comm` | `low-confidence` | 0.446 | 256.7 ms | **FAIL** |
| `epoch-to-readable-date` | turn unix epoch seconds into a normal human readable date | `numfmt` | `low-confidence` | 0.444 | 133.2 ms | **FAIL** |
| `survive-terminal-close` | keep this command running after i close the terminal | `wl-clip-persist` | `low-confidence` | 0.580 | 44.5 ms | **FAIL** |
| `count-file-lines` | count how many lines are in this file | `fincore` | `clear` | 0.884 | 92.9 ms | **FAIL** |
| `first-lines-only` | show me only the first twenty lines of a file | `vdir` | `low-confidence` | 0.505 | 133.7 ms | **FAIL** |
| `follow-growing-log` | keep printing new lines as they get appended to this log | `pr` | `low-confidence` | 0.438 | 220.5 ms | **FAIL** |
| `readable-strings-from-binary` | pull human readable text fragments out of a binary file | `accessdb` | `low-confidence` | 0.580 | 220.3 ms | **FAIL** |
| `inspect-file-as-hex` | inspect the raw bytes of a file in hexadecimal | `hexdump` | `low-confidence` | 0.580 | 144.9 ms | **PASS** |
| `owner-read-write-only` | make a file readable and writable only by its owner | `quilt` | `low-confidence` | 0.570 | 125.5 ms | **FAIL** |
| `change-file-owner` | change which user owns this file | `chown` | `low-confidence` | 0.580 | 119.6 ms | **PASS** |
| `create-symbolic-link` | make another path point to this file without copying the contents | `cupsfilter` | `low-confidence` | 0.495 | 129.9 ms | **FAIL** |
| `mounted-filesystem-free-space` | show how much free space is left on each mounted filesystem | `ideviceimagemounter` | `low-confidence` | 0.580 | 132.8 ms | **FAIL** |
| `mount-hierarchy` | show the filesystem mount hierarchy and where everything is mounted | `showmount` | `low-confidence` | 0.578 | 141.1 ms | **FAIL** |
| `block-device-tree` | show disks and partitions as a parent child tree | `cfdisk` | `low-confidence` | 0.520 | 141.5 ms | **FAIL** |
| `process-open-files` | show which files a running process currently has open | `lsof` | `low-confidence` | 0.580 | 156.5 ms | **PASS** |
| `process-parent-child-tree` | show running processes arranged by who spawned whom | `fc-conflist` | `low-confidence` | 0.548 | 101.1 ms | **FAIL** |
| `content-fingerprint` | make a fingerprint of this file so i can tell later if its contents changed | `mountpoint` | `low-confidence` | 0.428 | 117.4 ms | **FAIL** |
| `numeric-line-sort` | order these lines by their numeric value instead of alphabetically | `c_rehash` | `low-confidence` | 0.560 | 123.9 ms | **FAIL** |

## Failures

### `recent-files-by-time` — show me files that changed since yesterday
- `top1_any` expected `['find', 'fd', 'fdfind']`, got `era_invalidate`
- `top3_any` expected `['find', 'fd', 'fdfind']`, got `['era_invalidate', 'chacl', 'homectl']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `era_invalidate` | 0.5478 | `weak` | 0.6357 | `path+man` | `-` |
| 2 | `chacl` | 0.5128 | `weak` | 0.6015 | `path+man` | `-` |
| 3 | `homectl` | 0.5066 | `weak` | 0.5956 | `path+man` | `-` |
| 4 | `lsof` | 0.4104 | `weak` | 0.5377 | `path+man+expac+pkgfile` | `-` |
| 5 | `xtables-monitor` | 0.4634 | `weak` | 0.5335 | `path+man` | `-` |

### `repeat-command-periodically` — keep running the same command every two seconds so i can watch it change
- `top1_any` expected `['watch']`, got `oh-my-zsh-git`
- `top3_any` expected `['watch']`, got `['oh-my-zsh-git', 'modprobed-db', 'update-leap']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `oh-my-zsh-git` | 0.3978 | `weak` | 0.4178 | `expac` | `-` |
| 2 | `modprobed-db` | 0.3728 | `weak` | 0.3928 | `expac+pkgfile` | `-` |
| 3 | `update-leap` | 0.3178 | `weak` | 0.3888 | `path+man` | `-` |
| 4 | `lxc-device` | 0.3178 | `weak` | 0.3881 | `path+man` | `-` |
| 5 | `gsr-cli` | 0.3178 | `weak` | 0.3860 | `path+man` | `-` |

### `pid-from-process-name` — i know the process name but need its pid
- `top1_any` expected `['pgrep', 'pidof']`, got `htop`
- `top3_any` expected `['pgrep', 'pidof']`, got `['htop', 'btop', 'top']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `htop` | 0.4858 | `weak` | 0.6008 | `path+man+desktop` | `-` |
| 2 | `btop` | 0.4858 | `weak` | 0.5854 | `path+man+desktop` | `-` |
| 3 | `top` | 0.4858 | `weak` | 0.5829 | `path+man` | `-` |
| 4 | `systemd-tty-ask-password-agent` | 0.4858 | `weak` | 0.5706 | `path+man` | `-` |
| 5 | `getpcaps` | 0.4858 | `weak` | 0.5699 | `path+man` | `-` |

### `shared-library-dependencies` — what shared libraries does this executable need at runtime
- `top1_any` expected `['ldd']`, got `intel-oneapi-compiler-shared-runtime`
- `top3_any` expected `['ldd']`, got `['intel-oneapi-compiler-shared-runtime', 'intel-oneapi-compiler-shared-runtime-libs', 'intel-oneapi-compiler-shared']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `intel-oneapi-compiler-shared-runtime` | 0.5056 | `weak` | 0.5256 | `expac` | `-` |
| 2 | `intel-oneapi-compiler-shared-runtime-libs` | 0.5056 | `weak` | 0.5256 | `expac` | `-` |
| 3 | `intel-oneapi-compiler-shared` | 0.5032 | `weak` | 0.5232 | `expac` | `-` |
| 4 | `sysctl` | 0.4048 | `weak` | 0.4938 | `path+man` | `-` |
| 5 | `python-elastic-transport` | 0.4048 | `weak` | 0.4248 | `expac` | `-` |

### `detect-file-type-by-content` — tell me what kind of file this really is without trusting its extension
- `top1_any` expected `['file']`, got `shred`
- `top3_any` expected `['file']`, got `['shred', 'grub-mkrelpath', 'showttf']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `shred` | 0.3530 | `weak` | 0.4124 | `path+man` | `-` |
| 2 | `grub-mkrelpath` | 0.3416 | `weak` | 0.3972 | `path+man` | `-` |
| 3 | `showttf` | 0.2894 | `weak` | 0.3956 | `path+man` | `font-domain` |
| 4 | `mdeltree` | 0.2733 | `weak` | 0.3920 | `path+man` | `filesystem-format-domain` |
| 5 | `source-highlight-settings` | 0.2894 | `weak` | 0.3860 | `path+man` | `development-library-role` |

### `split-large-file` — break one huge file into 100 MB pieces
- `top1_any` expected `['split']`, got `lwp-download`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `lwp-download` | 0.3928 | `weak` | 0.4762 | `path+man` | `-` |
| 2 | `split` | 0.3778 | `weak` | 0.4488 | `path+man` | `-` |
| 3 | `parsort` | 0.3744 | `weak` | 0.4437 | `path+man` | `-` |
| 4 | `genbrk` | 0.3778 | `weak` | 0.4436 | `path+man` | `-` |
| 5 | `git-lfs` | 0.3535 | `weak` | 0.4004 | `expac+pkgfile` | `vcs-domain` |

### `second-column-text` — print only the second column from whitespace separated text
- `top1_any` expected `['awk', 'cut']`, got `column`
- `top3_any` expected `['awk', 'cut']`, got `['column', 'ts_print', 'ts_print_raw']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `column` | 0.3000 | `weak` | 0.3710 | `path+man` | `-` |
| 2 | `ts_print` | 0.3000 | `weak` | 0.3675 | `path+man` | `-` |
| 3 | `ts_print_raw` | 0.3000 | `weak` | 0.3671 | `path+man` | `-` |
| 4 | `ts_print_mt` | 0.3000 | `weak` | 0.3650 | `path+man` | `-` |
| 5 | `printf` | 0.3000 | `weak` | 0.3637 | `path+man` | `-` |

### `stdin-one-command-per-line` — run a command once for every line that comes from stdin
- `top1_any` expected `['xargs', 'parallel']`, got `hurl`
- `top3_any` expected `['xargs', 'parallel']`, got `['hurl', 'm', 'sort']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `hurl` | 0.3780 | `weak` | 0.3980 | `expac+pkgfile` | `-` |
| 2 | `m` | 0.3630 | `weak` | 0.3830 | `expac+pkgfile` | `-` |
| 3 | `sort` | 0.2994 | `weak` | 0.3669 | `path+man` | `-` |
| 4 | `echo` | 0.2994 | `weak` | 0.3658 | `path+man` | `-` |
| 5 | `gprofng-display-text` | 0.3066 | `weak` | 0.3657 | `path+man` | `-` |

### `remove-adjacent-duplicates` — remove repeated neighboring lines from already sorted text
- `top1_any` expected `['uniq']`, got `comm`
- `top3_any` expected `['uniq']`, got `['comm', 'cut', 'five-or-more']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `comm` | 0.3630 | `weak` | 0.4336 | `path+man` | `-` |
| 2 | `cut` | 0.3630 | `weak` | 0.4319 | `path+man` | `-` |
| 3 | `five-or-more` | 0.3630 | `weak` | 0.3830 | `expac+pkgfile` | `-` |
| 4 | `unifdef` | 0.3630 | `weak` | 0.3830 | `expac+pkgfile` | `-` |
| 5 | `homectl` | 0.3165 | `weak` | 0.3770 | `path+man` | `-` |

### `epoch-to-readable-date` — turn unix epoch seconds into a normal human readable date
- `top1_any` expected `['date']`, got `numfmt`
- `top3_any` expected `['date']`, got `['numfmt', 'winepath', 'man2html']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `numfmt` | 0.3974 | `weak` | 0.4857 | `path+man` | `-` |
| 2 | `winepath` | 0.3974 | `weak` | 0.4854 | `path+man` | `-` |
| 3 | `man2html` | 0.3974 | `weak` | 0.4174 | `expac+pkgfile` | `-` |
| 4 | `sensors-conf-convert` | 0.3308 | `weak` | 0.4014 | `path+man` | `-` |
| 5 | `ebook-convert` | 0.3308 | `weak` | 0.3986 | `path+man` | `-` |

### `survive-terminal-close` — keep this command running after i close the terminal
- `top1_any` expected `['nohup', 'tmux', 'screen']`, got `wl-clip-persist`
- `top3_any` expected `['nohup', 'tmux', 'screen']`, got `['wl-clip-persist', 'renice', 'kernel-modules-hook']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `wl-clip-persist` | 0.5728 | `weak` | 0.5928 | `expac+pkgfile` | `-` |
| 2 | `renice` | 0.4528 | `weak` | 0.5418 | `path+man` | `-` |
| 3 | `kernel-modules-hook` | 0.4528 | `weak` | 0.4728 | `expac` | `-` |
| 4 | `lxc-device` | 0.3328 | `weak` | 0.4034 | `path+man` | `-` |
| 5 | `gamemodelist` | 0.3328 | `weak` | 0.4031 | `path+man` | `-` |

### `count-file-lines` — count how many lines are in this file
- `top1_any` expected `['wc']`, got `fincore`
- `top3_any` expected `['wc']`, got `['fincore', 'sort', 'tokei']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `fincore` | 0.8657 | `strong` | 0.9459 | `path+man` | `-` |
| 2 | `sort` | 0.5183 | `weak` | 0.6055 | `path+man` | `-` |
| 3 | `tokei` | 0.5643 | `weak` | 0.6023 | `expac+pkgfile` | `-` |
| 4 | `rg` | 0.4895 | `weak` | 0.6005 | `path+man` | `-` |
| 5 | `diff` | 0.5002 | `weak` | 0.5926 | `path+man` | `-` |

### `first-lines-only` — show me only the first twenty lines of a file
- `top1_any` expected `['head']`, got `vdir`
- `top3_any` expected `['head']`, got `['vdir', 'ls', 'dir']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `vdir` | 0.4529 | `weak` | 0.5398 | `path+man` | `-` |
| 2 | `ls` | 0.4529 | `weak` | 0.5395 | `path+man` | `-` |
| 3 | `dir` | 0.4529 | `weak` | 0.5391 | `path+man` | `-` |
| 4 | `rdjpgcom` | 0.4692 | `weak` | 0.5288 | `path+man` | `-` |
| 5 | `xzmore` | 0.4223 | `weak` | 0.5150 | `path+man` | `archive-domain` |

### `follow-growing-log` — keep printing new lines as they get appended to this log
- `top1_any` expected `['tail']`, got `pr`
- `top3_any` expected `['tail']`, got `['pr', 'giftext', 'lxc-usernsexec']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `pr` | 0.3780 | `weak` | 0.4487 | `path+man` | `-` |
| 2 | `giftext` | 0.3780 | `weak` | 0.4431 | `path+man` | `-` |
| 3 | `lxc-usernsexec` | 0.3702 | `weak` | 0.4345 | `path+man` | `-` |
| 4 | `dbus-run-session` | 0.3702 | `weak` | 0.4342 | `path+man` | `-` |
| 5 | `grep` | 0.3630 | `weak` | 0.4329 | `path+man` | `-` |

### `readable-strings-from-binary` — pull human readable text fragments out of a binary file
- `top1_any` expected `['strings']`, got `accessdb`
- `top3_any` expected `['strings']`, got `['accessdb', 'numfmt', 'named-journalprint']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `accessdb` | 0.4465 | `weak` | 0.5345 | `path+man` | `-` |
| 2 | `numfmt` | 0.3702 | `weak` | 0.4412 | `path+man` | `-` |
| 3 | `named-journalprint` | 0.3702 | `weak` | 0.4408 | `path+man` | `-` |
| 4 | `edid-decode` | 0.3702 | `weak` | 0.4405 | `path+man` | `-` |
| 5 | `humanfriendly` | 0.3780 | `weak` | 0.3980 | `expac+pkgfile` | `-` |

### `owner-read-write-only` — make a file readable and writable only by its owner
- `top1_any` expected `['chmod']`, got `quilt`
- `top3_any` expected `['chmod']`, got `['quilt', 'rofiles-fuse', 'fc-conflist']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `quilt` | 0.4559 | `weak` | 0.4759 | `expac+pkgfile` | `-` |
| 2 | `rofiles-fuse` | 0.3780 | `weak` | 0.4490 | `path+man` | `-` |
| 3 | `fc-conflist` | 0.3780 | `weak` | 0.4322 | `path+man` | `-` |
| 4 | `pppdump` | 0.3530 | `weak` | 0.4236 | `path+man` | `-` |
| 5 | `mdu` | 0.2933 | `weak` | 0.4148 | `path+man` | `filesystem-format-domain` |

### `create-symbolic-link` — make another path point to this file without copying the contents
- `top1_any` expected `['ln']`, got `cupsfilter`
- `top3_any` expected `['ln']`, got `['cupsfilter', 'sndfile-convert', 'gfxstream']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `cupsfilter` | 0.3831 | `weak` | 0.4707 | `path+man` | `-` |
| 2 | `sndfile-convert` | 0.3218 | `weak` | 0.4529 | `path+man` | `audio-domain` |
| 3 | `gfxstream` | 0.4193 | `weak` | 0.4393 | `expac` | `-` |
| 4 | `ln` | 0.3831 | `weak` | 0.4391 | `path+man` | `-` |
| 5 | `mknod` | 0.3831 | `weak` | 0.4370 | `path+man` | `-` |

### `mounted-filesystem-free-space` — show how much free space is left on each mounted filesystem
- `top1_any` expected `['df', 'duf']`, got `ideviceimagemounter`
- `top3_any` expected `['df', 'duf']`, got `['ideviceimagemounter', 'xfs_spaceman', 'xfs_scrub']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `ideviceimagemounter` | 0.4870 | `weak` | 0.5687 | `path+man` | `-` |
| 2 | `xfs_spaceman` | 0.3906 | `weak` | 0.5470 | `path+man` | `filesystem-format-domain` |
| 3 | `xfs_scrub` | 0.3818 | `weak` | 0.5363 | `path+man` | `filesystem-format-domain` |
| 4 | `cd-drive` | 0.4168 | `weak` | 0.5002 | `path+man` | `-` |
| 5 | `dump.exfat` | 0.3397 | `weak` | 0.4860 | `path+man` | `filesystem-format-domain` |

### `mount-hierarchy` — show the filesystem mount hierarchy and where everything is mounted
- `top1_any` expected `['findmnt', 'mount']`, got `showmount`
- `top3_any` expected `['findmnt', 'mount']`, got `['showmount', 'xfs_scrub', 'ideviceimagemounter']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `showmount` | 0.5036 | `weak` | 0.5821 | `path+man` | `-` |
| 2 | `xfs_scrub` | 0.4356 | `weak` | 0.5749 | `path+man` | `filesystem-format-domain` |
| 3 | `ideviceimagemounter` | 0.4920 | `weak` | 0.5642 | `path+man` | `-` |
| 4 | `mountstats` | 0.4920 | `weak` | 0.5561 | `path+man` | `-` |
| 5 | `e2scrub` | 0.4920 | `weak` | 0.5540 | `path+man` | `-` |

### `block-device-tree` — show disks and partitions as a parent child tree
- `top1_any` expected `['lsblk']`, got `cfdisk`
- `top3_any` expected `['lsblk']`, got `['cfdisk', 'sfdisk', 'cd-drive']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `cfdisk` | 0.4870 | `weak` | 0.5676 | `path+man` | `-` |
| 2 | `sfdisk` | 0.4870 | `weak` | 0.5673 | `path+man` | `-` |
| 3 | `cd-drive` | 0.4168 | `weak` | 0.5012 | `path+man` | `-` |
| 4 | `ideviceimagemounter` | 0.4094 | `weak` | 0.4872 | `path+man` | `-` |
| 5 | `dump.exfat` | 0.3397 | `weak` | 0.4867 | `path+man` | `filesystem-format-domain` |

### `process-parent-child-tree` — show running processes arranged by who spawned whom
- `top1_any` expected `['pstree', 'ps']`, got `fc-conflist`
- `top3_any` expected `['pstree', 'ps']`, got `['fc-conflist', 'lsof', 'hwloc-ps']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `fc-conflist` | 0.4870 | `weak` | 0.5725 | `path+man` | `-` |
| 2 | `lsof` | 0.4870 | `weak` | 0.5470 | `expac+pkgfile` | `-` |
| 3 | `hwloc-ps` | 0.3799 | `weak` | 0.5414 | `path+man` | `topology-domain` |
| 4 | `htop` | 0.4247 | `weak` | 0.5376 | `path+man+desktop` | `-` |
| 5 | `btop` | 0.4247 | `weak` | 0.5250 | `path+man+desktop` | `-` |

### `content-fingerprint` — make a fingerprint of this file so i can tell later if its contents changed
- `top1_any` expected `['sha256sum', 'b2sum', 'cksum', 'md5sum']`, got `mountpoint`
- `top3_any` expected `['sha256sum', 'b2sum', 'cksum', 'md5sum']`, got `['mountpoint', 'ln', 'mknod']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `mountpoint` | 0.3530 | `weak` | 0.4240 | `path+man` | `-` |
| 2 | `ln` | 0.3530 | `weak` | 0.4135 | `path+man` | `-` |
| 3 | `mknod` | 0.3530 | `weak` | 0.4117 | `path+man` | `-` |
| 4 | `mkfs.cramfs` | 0.3177 | `weak` | 0.3990 | `path+man` | `archive-domain` |
| 5 | `grub-mkfont` | 0.2894 | `weak` | 0.3963 | `path+man` | `font-domain` |

### `numeric-line-sort` — order these lines by their numeric value instead of alphabetically
- `top1_any` expected `['sort']`, got `c_rehash`
- `top3_any` expected `['sort']`, got `['c_rehash', 'kate', 'nano']`

#### Forensic top candidates

| # | Candidate | Semantic fit | Tier | Utility | Sources | Semantic adjustments |
|---:|---|---:|---|---:|---|---|
| 1 | `c_rehash` | 0.4465 | `weak` | 0.4883 | `path+man` | `-` |
| 2 | `kate` | 0.3780 | `weak` | 0.4464 | `path+man+desktop` | `-` |
| 3 | `nano` | 0.3780 | `weak` | 0.4413 | `path+man` | `-` |
| 4 | `systemd-cgtop` | 0.3702 | `weak` | 0.4370 | `path+man` | `-` |
| 5 | `rankmirrors` | 0.3702 | `weak` | 0.4366 | `path+man` | `-` |
