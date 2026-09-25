# Acclorite release checklist

This checklist covers stable Acclorite releases. Arch/CachyOS remains the reference benchmark/AUR target; cross-distro backends add their own backend-specific smoke gates.

## 1. Freeze the tree

- choose the release version in `CMakeLists.txt` and CLI contract tests;
- make sure `README.md`, `docs/install.md`, the man page, and machine-interface docs agree with the CLI;
- review additions to `data/curated-guidance.tsv` for authoritative source references and verification dates;
- ensure the top-level `LICENSE` remains `GPL-3.0-or-later` unless the project deliberately changes licensing.

## 2. Validate locally

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
python3 benchmarks/run.py \
  --binary ./build/acclorite \
  --json-out benchmark-report.json \
  --markdown-out benchmark-report.md \
  --strict
```

The Arch/CachyOS release authority is the frozen benchmark corpus on a representative real machine, not absolute timing from CI/container environments. Cross-distro backend work must preserve that corpus unchanged.

For `v0.3.0+`, also run a Debian/Ubuntu smoke test (a real system or isolated image with local package metadata):

```bash
./build/acclorite --capabilities   # apt_packages: true, arch_packages: false
./build/acclorite doctor           # Debian integration section
./build/acclorite "duplicate files"
```

Do not run `apt update` merely to satisfy Acclorite; the backend is required to consume whatever local APT metadata the system already has.

For `v0.3.1+`, also run a Fedora/RHEL-family smoke test with pre-existing local DNF metadata:

```bash
./build/acclorite --capabilities   # dnf_packages: true, arch_packages/apt_packages: false
./build/acclorite doctor           # Fedora integration section
./build/acclorite "duplicate files"
```

Do not run `dnf makecache` merely for Acclorite. Every backend invocation is required to use `--cacheonly` and consume the already-local cache.

For `v0.3.2+`, also run an openSUSE-family smoke test with existing local Zypper metadata:

```bash
./build/acclorite --capabilities   # zypper_packages: true; other package backends false
./build/acclorite doctor           # openSUSE integration section
./build/acclorite "duplicate files"
```

Do not run `zypper refresh` merely for Acclorite. Repository queries must use `--no-refresh` and the bundled `runSearchPackages = never` policy so neither repository refresh nor the optional extended network search hook is needed.

For `v0.3.3+`, also run a Void Linux smoke test with already-synchronized local XBPS repository indexes:

```bash
./build/acclorite --capabilities   # xbps_packages: true; other package backends false
./build/acclorite doctor           # Void integration section
./build/acclorite "duplicate files"
```

Do not run `xbps-install -S` merely for Acclorite. The backend is required to use `xbps-query` without `-M`/`--memory-sync` and consume the existing on-disk indexes. Acclorite must never invoke `xbps-install` during search or Doctor.

Also verify:

```bash
./build/acclorite --help
./build/acclorite --capabilities
./build/acclorite doctor
./build/acclorite --reindex
```

## 3. Validate installation

The CTest suite includes a staged-install test, but also perform one real install/package test before publishing:

```bash
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-release -j"$(nproc)"
DESTDIR="$PWD/pkgroot" cmake --install build-release
"$PWD/pkgroot/usr/bin/acclorite" --capabilities
```

The staged binary must report bundled curated guidance as available from the staged `/usr/share/acclorite` tree, and the staged data directory must contain `zypper-readonly.conf`; it must not depend on files from the source/build directory.

## 4. Tag upstream

Commit the release, then create/push `v<version>` (for this slice, `v0.3.3`). The AUR package intentionally targets a fixed release tag rather than a moving branch.

## 5. Validate the AUR recipe

From `packaging/aur/` on Arch/CachyOS:

```bash
makepkg -si
namcap PKGBUILD acclorite-*.pkg.tar.*   # when namcap is installed
makepkg --printsrcinfo > .SRCINFO
```

Confirm the installed package:

```bash
acclorite --version
acclorite --capabilities
acclorite doctor
```

## 6. Review the public repository

Before publishing, view the repository as a first-time visitor:

- verify the README build/install commands on a clean checkout;
- confirm the Verify workflow is green;
- check that `acclorite --help`, the README example, and the man page use the same terminology;
- confirm issue templates, contribution guidance, and the security-reporting path are present;
- ensure local build trees, benchmark outputs, model/index files, and virtual environments are not staged;
- verify the parked M11 research targets remain disabled in the default build.

## 7. Publish

Push the upstream tag/release first. Create the GitHub release from the matching changelog entry and include concise install/update notes. Then update the separate AUR package Git repository with only the reviewed `PKGBUILD` and generated `.SRCINFO`.

Normal Acclorite runtime remains offline after installation. Release/download hosting and AUR source retrieval are distribution concerns, not runtime search dependencies.
