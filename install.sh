#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

prefix="/usr/local"
build_dir="build-install"
run_tests=1
jobs=""
user_install=0

usage() {
    cat <<'EOF'
Acclorite source installer

Usage:
  ./install.sh [options]

Options:
  --user              Install under ~/.local without sudo
  --prefix PATH       Install under PATH instead of /usr/local
  --build-dir PATH    Use PATH as the CMake build directory
  --jobs N            Build with N parallel jobs
  --no-test           Skip CTest
  -h, --help          Show this help

The installer builds and installs Acclorite. It never installs distro
packages, refreshes repositories, or modifies package-manager metadata.
EOF
}

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

note() {
    printf '==> %s\n' "$*"
}

while (($#)); do
    case "$1" in
        --user)
            user_install=1
            prefix="${HOME}/.local"
            shift
            ;;
        --prefix)
            (($# >= 2)) || die "--prefix requires a path"
            prefix="$2"
            user_install=0
            shift 2
            ;;
        --build-dir)
            (($# >= 2)) || die "--build-dir requires a path"
            build_dir="$2"
            shift 2
            ;;
        --jobs)
            (($# >= 2)) || die "--jobs requires a positive integer"
            [[ "$2" =~ ^[1-9][0-9]*$ ]] || die "--jobs requires a positive integer"
            jobs="$2"
            shift 2
            ;;
        --no-test)
            run_tests=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown option: $1 (try --help)"
            ;;
    esac
done

if [[ "$user_install" -eq 1 && "$prefix" != "${HOME}/.local" ]]; then
    die "--user cannot be combined with another prefix"
fi

if [[ -z "$jobs" ]]; then
    if command -v nproc >/dev/null 2>&1; then
        jobs="$(nproc)"
    elif command -v getconf >/dev/null 2>&1; then
        jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')"
    else
        jobs=1
    fi
fi

compiler=""
for candidate in c++ g++ clang++; do
    if command -v "$candidate" >/dev/null 2>&1; then
        compiler="$candidate"
        break
    fi
done

missing=()
command -v cmake >/dev/null 2>&1 || missing+=("cmake")
[[ -n "$compiler" ]] || missing+=("C++ compiler")
if [[ "$run_tests" -eq 1 ]]; then
    command -v python3 >/dev/null 2>&1 || missing+=("python3")
fi

print_dependency_hint() {
    local distro=""
    local distro_like=""
    if [[ -r /etc/os-release ]]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        distro="${ID:-}"
        distro_like="${ID_LIKE:-}"
    fi

    printf '\nInstall the missing build dependencies with your distro package manager.\n'
    case " ${distro} ${distro_like} " in
        *" arch "*|*" cachyos "*)
            printf '  Arch/CachyOS: sudo pacman -S --needed base-devel cmake sqlite python\n'
            ;;
        *" debian "*|*" ubuntu "*)
            printf '  Debian/Ubuntu: sudo apt install build-essential cmake libsqlite3-dev python3\n'
            ;;
        *" fedora "*|*" rhel "*)
            printf '  Fedora/RHEL: sudo dnf install gcc-c++ cmake sqlite-devel python3\n'
            ;;
        *" opensuse "*|*" suse "*)
            printf '  openSUSE: sudo zypper install gcc-c++ cmake sqlite3-devel python3\n'
            ;;
        *" void "*)
            printf '  Void: sudo xbps-install -S base-devel cmake sqlite-devel python3\n'
            ;;
        *)
            printf '  Required: C++20 compiler + CMake'
            [[ "$run_tests" -eq 1 ]] && printf ' + Python 3'
            printf '\n'
            printf '  SQLite development files are recommended for the persistent FTS index.\n'
            ;;
    esac
}

if ((${#missing[@]})); then
    printf 'Missing required build tools:\n' >&2
    for item in "${missing[@]}"; do
        printf '  - %s\n' "$item" >&2
    done
    print_dependency_hint >&2
    exit 2
fi

if [[ "$build_dir" != /* ]]; then
    build_dir="${SCRIPT_DIR}/${build_dir}"
fi

note "Source: ${SCRIPT_DIR}"
note "Build directory: ${build_dir}"
note "Install prefix: ${prefix}"
note "Parallel jobs: ${jobs}"

cmake_args=(
    -S "$SCRIPT_DIR"
    -B "$build_dir"
    -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_INSTALL_PREFIX=${prefix}"
)

note "Configuring Release build"
cmake "${cmake_args[@]}"

note "Building Acclorite"
cmake --build "$build_dir" -j"$jobs"

if [[ "$run_tests" -eq 1 ]]; then
    note "Running test suite"
    ctest --test-dir "$build_dir" --output-on-failure
else
    note "Skipping tests (--no-test)"
fi

install_cmd=(cmake --install "$build_dir")

needs_sudo=0
if [[ "${EUID}" -ne 0 ]]; then
    case "$prefix" in
        "$HOME"|"$HOME"/*)
            needs_sudo=0
            ;;
        *)
            probe="$prefix"
            while [[ ! -e "$probe" && "$probe" != "/" ]]; do
                probe="$(dirname "$probe")"
            done
            [[ -w "$probe" ]] || needs_sudo=1
            ;;
    esac
fi

note "Installing Acclorite"
if [[ "$needs_sudo" -eq 1 ]]; then
    command -v sudo >/dev/null 2>&1 || die "install prefix requires elevated permissions, but sudo is unavailable"
    sudo "${install_cmd[@]}"
else
    "${install_cmd[@]}"
fi

binary="${prefix}/bin/acclorite"
if [[ -x "$binary" ]]; then
    printf '\nInstalled successfully:\n'
    "$binary" --version
    printf '  %s\n' "$binary"
else
    printf '\nInstallation completed, but %s was not found.\n' "$binary" >&2
    printf 'Inspect the CMake install output above for the final binary path.\n' >&2
fi

if [[ "$prefix" == "${HOME}/.local" ]]; then
    case ":${PATH}:" in
        *":${HOME}/.local/bin:"*) ;;
        *)
            printf '\n~/.local/bin is not currently on PATH.\n'
            printf 'For Fish:\n'
            printf '  fish_add_path ~/.local/bin\n'
            printf 'For POSIX shells, add this to your shell profile:\n'
            printf '  export PATH="$HOME/.local/bin:$PATH"\n'
            ;;
    esac
fi

printf '\nTry:\n'
printf '  acclorite "How to use grep"\n'
printf '  acclorite doctor\n'
