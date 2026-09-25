<h1 align="center">Acclorite</h1>

<p align="center"><strong>Find the Linux tool you meant — from the knowledge already on your machine.</strong></p>

<p align="center">
  <a href="https://github.com/kzahed610/Acclorite/actions/workflows/verify.yml"><img alt="Verify" src="https://github.com/kzahed610/Acclorite/actions/workflows/verify.yml/badge.svg"></a>
  &nbsp; <code>v0.3.3</code> &nbsp; <code>C++20</code> &nbsp; <code>GPL-3.0-or-later</code>
</p>

Acclorite is a local-first Linux CLI for the moment when you know **what you want to do**, but not which command or application you should be looking for.

```text
$ acclorite "search text inside files"

Best match
────────────────────────────────────────
rg  —  recursively search the current directory for lines matching a pattern

  ✓ installed  ·  /usr/bin/rg
  Interface     CLI
  Matched       search · text · files
  Sources       PATH · man

Template
  rg {{pattern}}

Confidence  medium · broad match
```

No shell agent. No automatic execution. No network fetch during normal search.

## What it does

```text
"search text inside files"        → discover rg / grep / …
"what is fd"                      → explain fd
"where is ssh config"             → locate known SSH configuration paths
"difference between rg and grep"  → resolve and compare both tools
```

Acclorite searches local executables, manuals, desktop metadata, supported package-manager caches, and optional local guidance sources. Results are ranked deterministically and explained with source-backed evidence.

### Product promises

- **Local first** — ordinary queries stay offline.
- **Non-executing** — Acclorite recommends and explains; it never runs the suggested command.
- **Source backed** — missing syntax is preferred over invented syntax.
- **Graceful degradation** — optional integrations improve results without becoming startup requirements.
- **Machine friendly** — stable JSON and capability discovery are available for integrations.

## Install

### Arch Linux / CachyOS

Install the build dependencies:

```bash
sudo pacman -S --needed base-devel cmake sqlite
```

Then build and test:

```bash
git clone https://github.com/kzahed610/Acclorite.git
cd Acclorite

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Try it directly:

```bash
./build/acclorite "search text inside files"
```

Or install it system-wide:

```bash
sudo cmake --install build
```

Optional Arch integrations:

```bash
sudo pacman -S --needed man-db expac pkgfile tealdeer
```

The repository also contains the stable AUR packaging source under [`packaging/aur/`](packaging/aur/). The AUR package is published only after its matching upstream release tag exists.

For per-user installs, other supported distro families, updating, and uninstalling, see **[`docs/install.md`](docs/install.md)**.

## Use it

Ask naturally:

```bash
acclorite "archive files"
acclorite "search text inside files"
acclorite "what is rg"
acclorite "where is ssh config"
acclorite "difference between rg and grep"
```

Check local readiness:

```bash
acclorite doctor
```

Refresh the persistent local index:

```bash
acclorite --reindex
```

Use the machine interface:

```bash
acclorite --json "archive files"
acclorite --capabilities
```

And when you want the full CLI reference:

```bash
acclorite --help
man acclorite
```

## Knowledge sources

Depending on what is available locally, Acclorite can use:

| Source | Purpose |
| --- | --- |
| `PATH` | Installed command discovery |
| man pages | Local descriptions, guidance, and syntax evidence |
| `.desktop` metadata | Desktop application discovery |
| SQLite FTS5 | Fast persistent local search index |
| distro package metadata | Discover commands/packages not currently installed |
| Info / TLDR / Fish completions | Optional post-ranking guidance and syntax |
| bundled curated guidance | Small reviewed knowledge gaps |

Package discovery is read-only. Acclorite does **not** refresh repositories or install packages.

## Package backends

| Family | Backend | Runtime behavior |
| --- | --- | --- |
| Arch / CachyOS | pacman + optional `expac` / `pkgfile` | Reads synchronized local repository metadata |
| Debian / Ubuntu | APT / dpkg | Reads `apt-cache` and installed-package state |
| Fedora / RHEL | DNF / RPM | Uses cache-only DNF queries |
| openSUSE / SUSE | Zypper / RPM | Uses `--no-refresh` and bundled read-only configuration |
| Void Linux | XBPS | Reads synchronized on-disk XBPS indexes |

Other systems can still use the generic PATH/man/desktop layers without a dedicated package backend.

## Safety model

Acclorite never executes the command it recommends.

When verified syntax can be constructed from local evidence, Acclorite can classify an invocation as read-only, mutating, destructive, privileged, network-related, or unknown. Missing evidence is preferred over fabricated command forms.

This is guidance, not a sandbox: read a command before running it.

## Build options

```text
ACCLORITE_BUILD_TESTS=ON       Build the normal test suite (default: ON)
ACCLORITE_BUILD_RESEARCH=OFF   Build parked M11 research probes (default: OFF)
```

Experimental semantic-retrieval/ONNX work is intentionally opt-in and is **not part of the normal runtime or release build**. You do not need PyTorch, SentenceTransformers, ONNX Runtime, or a model download to build or use Acclorite.

## Project status

The deterministic standalone CLI is the release focus for `v0.3.3`.

Semantic-retrieval research is preserved in the repository but currently parked. It demonstrated useful local recall, while also establishing a strict boundary: semantic assistance may surface separate source-substantiated alternatives, but it does not replace deterministic ranking, syntax authority, provenance, or safety.

<details>
<summary><strong>Developer documentation</strong></summary>

- [`docs/architecture.md`](docs/architecture.md) — architecture
- [`docs/specification.md`](docs/specification.md) — product specification
- [`docs/machine-interface.md`](docs/machine-interface.md) — JSON/schema compatibility contract
- [`docs/release.md`](docs/release.md) — release checklist
- [`docs/m11-research.md`](docs/m11-research.md) — parked semantic research record
- [`benchmarks/README.md`](benchmarks/README.md) — benchmark harness and corpora

</details>

## Contributing and security

Contributions are welcome; start with [`CONTRIBUTING.md`](CONTRIBUTING.md). Security-sensitive reports should follow [`SECURITY.md`](SECURITY.md).

## License

Acclorite is licensed under **GPL-3.0-or-later**. See [`LICENSE`](LICENSE).
