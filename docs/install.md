# Installing Acclorite

Acclorite is local-first and offline at runtime. Installing the program naturally requires obtaining source/package data, but ordinary search, Doctor, indexing, guidance, and capability discovery do not fetch from the network.

## Arch Linux / CachyOS — source build

Required build/runtime packages for the indexed build:

```bash
sudo pacman -S --needed base-devel cmake sqlite
```

Optional integrations:

```bash
sudo pacman -S --needed man-db expac pkgfile tealdeer
```

The optional packages are not required for Acclorite to start. They add local manual discovery, richer Arch repository metadata, package-to-command mapping, and local TLDR guidance respectively. Acclorite never runs repository refreshes or `tldr --update` on its own.

Build, test, and install under `/usr/local`:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

For an unprivileged per-user install, configure the intended prefix before building:

```bash
cmake -S . -B build-user \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build-user -j"$(nproc)"
ctest --test-dir build-user --output-on-failure
cmake --install build-user
```

Make sure `~/.local/bin` is on `PATH` for the latter.

After installation:

```bash
acclorite --version
acclorite --capabilities
acclorite doctor
acclorite --reindex
acclorite "what is rg"
```

`--reindex` is safe to run manually: rebuilds are staged and validated before promotion, and a failed rebuild preserves the last usable index.

## Debian / Ubuntu — source build

Required build/runtime packages for the indexed build:

```bash
sudo apt install build-essential cmake libsqlite3-dev
```

`apt-cache` and `dpkg-query` are provided by the normal APT/dpkg stack on Debian-family systems and are used read-only for package discovery and installed-package state. Optional local manual/TLDR integrations may be installed through the distribution's normal packages when available; Acclorite does not require them to start.

Build, test, and install under `/usr/local`:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

After installation, `acclorite --capabilities` should report `apt_packages: true` when the local APT package cache is available. `acclorite doctor` reports whether `apt-cache` metadata and `dpkg-query` state are readable. Acclorite never runs `apt update`, `apt-get`, or package installation commands.

## Fedora / RHEL family — source build

Required build/runtime packages for the indexed build:

```bash
sudo dnf install gcc-c++ cmake sqlite-devel
```

On current Fedora releases the `dnf` command may be backed by DNF5; Acclorite also recognizes an explicit `dnf5` executable and prefers it when both are present. The normal RPM database provides installed-package state. Optional manual/TLDR integrations can be installed through the distribution's normal packages when available.

Build, test, and install under `/usr/local`:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

After installation, `acclorite --capabilities` should report `dnf_packages: true` when a DNF frontend is available and selected. `acclorite doctor` verifies whether cache-only DNF repository metadata and the local RPM database are readable. Acclorite always invokes DNF with `--cacheonly` and never runs `dnf makecache`, refreshes metadata, or performs package transactions.

## openSUSE / SUSE family — source build

Install a C++ compiler, CMake, and the SQLite development package through the normal openSUSE package workflow. (`zypper install PACKAGE...` is the standard package-install form.) Zypper and the RPM database provide Acclorite's repository and installed-package metadata; no separate Acclorite service is required.

Build, test, and install under `/usr/local`:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

After installation, `acclorite --capabilities` should report `zypper_packages: true` when Zypper is available and the openSUSE backend is selected. `acclorite doctor` verifies cached Zypper metadata and the local RPM database. Acclorite invokes repository searches with `--no-refresh`, uses machine-readable XML, and supplies its own read-only Zypper configuration with `runSearchPackages = never`, so the optional extended search-packages plugin is not called. It never performs a Zypper refresh or package transaction.

## Void Linux / XBPS — source build

Install the normal C/C++ build toolchain, CMake, and SQLite development headers using Void's package workflow. A typical setup uses the `base-devel`, `cmake`, and `sqlite-devel` packages:

```bash
sudo xbps-install -S base-devel cmake sqlite-devel
```

Build, test, and install under `/usr/local`:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

After installation, `acclorite --capabilities` should report `xbps_packages: true` when `xbps-query` and `xbps-uhelper` are available and the Void backend is selected. `acclorite doctor` checks the registered on-disk repository indexes and local installed-package database. Acclorite searches with `xbps-query` but never enables `-M`/`--memory-sync` and never invokes `xbps-install`; repository synchronization therefore remains an explicit administrator/user action in the normal Void workflow. If local indexes are absent or stale, Acclorite degrades instead of fetching replacements.

## AUR package

The repository ships the stable AUR packaging source under `packaging/aur/`. Once the matching `v0.3.5` upstream release tag has been pushed and the package published to the AUR, installation is the normal AUR workflow (for example with an AUR helper or by cloning the AUR package repository and running `makepkg -si`).

The package installs to `/usr`, runs the full test suite in `check()`, and keeps `man-db`, `expac`, `pkgfile`, and `tealdeer` as optional integrations. Its installed payload includes:

```text
/usr/bin/acclorite
/usr/share/acclorite/curated-guidance.tsv
/usr/share/acclorite/zypper-readonly.conf
/usr/share/man/man1/acclorite.1
/usr/share/doc/acclorite/...
```

## Offline behavior

Acclorite itself does not update package databases, TLDR caches, or curated metadata during normal operation. External package-manager/TLDR caches may be updated by the user using their own tools; Acclorite only reads the already-local state.

## Updating a source install

Pull or check out the desired release, rebuild, run the tests, and install from the same configured build tree:

```bash
git pull --ff-only
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

For release-to-release updates, checking out the new `v<version>` tag before rebuilding gives a reproducible source state. Acclorite's cache/index lives outside the install prefix and does not need to be deleted for a normal upgrade; run `acclorite --reindex` if you explicitly want a fresh index.

## Uninstalling a CMake install

Each configured build tree records exactly which files it installed. After installing from that build tree, remove them with:

```bash
sudo cmake --build build --target uninstall
```

For a per-user install, omit `sudo` and use the matching build directory, for example:

```bash
cmake --build build-user --target uninstall
```

The uninstall target removes only files listed in that build tree's `install_manifest.txt`. It intentionally does not delete user caches such as `~/.cache/acclorite`; remove those separately only if you also want to discard the local index. Package-manager installs should be removed through the package manager instead.
