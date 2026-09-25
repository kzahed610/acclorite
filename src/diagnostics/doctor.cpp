#include "acclorite/diagnostics/doctor.hpp"

#include <algorithm>
#include <filesystem>
#include <ranges>
#include <sstream>
#include <string_view>

#include "acclorite/sources/desktop_source.hpp"
#include "acclorite/sources/dnf_source.hpp"
#include "acclorite/sources/zypper_source.hpp"
#include "acclorite/sources/xbps_source.hpp"
#include "acclorite/sources/index_source.hpp"
#include "acclorite/system/distro.hpp"
#include "acclorite/system/executable.hpp"
#include "acclorite/system/process.hpp"

namespace acclorite::diagnostics {
namespace {

DoctorCheck check(
    std::string id,
    std::string section,
    std::string label,
    const DoctorState state,
    std::string detail,
    std::string hint = {}
) {
    return DoctorCheck{
        .id = std::move(id),
        .section = std::move(section),
        .label = std::move(label),
        .state = state,
        .detail = std::move(detail),
        .hint = std::move(hint),
    };
}

std::size_t existing_path_directory_count() {
    std::size_t count = 0;
    for (const auto& directory : system::path_directories()) {
        std::error_code error;
        if (std::filesystem::is_directory(directory, error)) {
            ++count;
        }
    }
    return count;
}

std::string executable_detail(std::string_view name) {
    const auto executable = system::find_executable(name);
    return executable ? executable->string() : std::string{};
}

bool expac_sync_ready() {
    if (!system::find_executable("expac")) {
        return false;
    }
    const auto probe = system::run_capture_stdout(
        {"expac", "-S", "-1", "%n", "pacman"},
        64 * 1024
    );
    return probe.exit_code == 0 && probe.stdout_text.find("pacman") != std::string::npos;
}

bool pacman_sync_ready() {
    if (!system::find_executable("pacman")) {
        return false;
    }
    const auto probe = system::run_capture_stdout(
        {"pacman", "--color", "never", "-Ss", "^pacman$"},
        128 * 1024
    );
    return !probe.stdout_text.empty();
}

bool pkgfile_metadata_ready() {
    if (!system::find_executable("pkgfile")) {
        return false;
    }

    // This is deliberately a bounded read-only probe. The pacman package exists
    // on Arch-family systems and contains a normal executable, making it a stable
    // sentinel for whether pkgfile's repo.files metadata is actually usable.
    const auto probe = system::run_capture_stdout(
        {"pkgfile", "--list", "--binaries", "pacman"},
        64 * 1024
    );
    return probe.exit_code == 0 && probe.stdout_text.find("/bin/pacman") != std::string::npos;
}

bool apt_cache_ready() {
    if (!system::find_executable("apt-cache")) {
        return false;
    }
    const auto probe = system::run_capture_stdout(
        {"apt-cache", "stats"},
        128 * 1024
    );
    return probe.exit_code == 0 && !probe.stdout_text.empty();
}

bool dpkg_query_ready() {
    if (!system::find_executable("dpkg-query")) {
        return false;
    }
    const auto probe = system::run_capture_stdout(
        {"dpkg-query", "-W", "-f=${Status}\n", "dpkg"},
        16 * 1024
    );
    return probe.exit_code == 0 && probe.stdout_text.find("install ok installed") != std::string::npos;
}

bool dnf_cache_ready(const std::string_view frontend) {
    if (frontend.empty()) {
        return false;
    }
    const auto probe = system::run_capture_stdout(
        {std::string(frontend), "--cacheonly", "repoquery",
         "--queryformat=%{name}\\n", "rpm"},
        64 * 1024
    );
    return probe.exit_code == 0 && probe.stdout_text.find("rpm") != std::string::npos;
}

bool rpm_query_ready() {
    if (!system::find_executable("rpm")) {
        return false;
    }
    const auto probe = system::run_capture_stdout(
        {"rpm", "-q", "--qf", "%{NAME}\\n", "rpm"},
        16 * 1024
    );
    return probe.exit_code == 0 && probe.stdout_text.find("rpm") != std::string::npos;
}

std::string desktop_detail() {
    std::vector<std::string> existing;
    for (const auto& directory : DesktopSource::application_directories()) {
        std::error_code error;
        if (std::filesystem::is_directory(directory, error)) {
            existing.push_back(directory.string());
        }
    }

    if (existing.empty()) {
        return {};
    }

    std::ostringstream out;
    if (existing.size() == 1) {
        out << "1 application metadata directory";
    } else {
        out << existing.size() << " application metadata directories";
    }
    return out.str();
}

} // namespace

std::string_view doctor_state_name(const DoctorState state) {
    switch (state) {
    case DoctorState::Ready:
        return "ready";
    case DoctorState::Warning:
        return "warning";
    case DoctorState::Optional:
        return "optional";
    case DoctorState::Error:
        return "error";
    }
    return "unknown";
}

bool DoctorReport::has_errors() const {
    return std::ranges::any_of(checks, [](const DoctorCheck& item) {
        return item.state == DoctorState::Error;
    });
}

bool DoctorReport::has_warnings() const {
    return std::ranges::any_of(checks, [](const DoctorCheck& item) {
        return item.state == DoctorState::Warning;
    });
}

std::string DoctorReport::status() const {
    if (has_errors()) {
        return "broken";
    }
    if (has_warnings()) {
        return "degraded";
    }
    return "healthy";
}

DoctorReport Doctor::run() const {
    DoctorReport report;

    const std::size_t path_dirs = existing_path_directory_count();
    if (path_dirs == 0) {
        report.checks.push_back(check(
            "path", "Core", "PATH discovery", DoctorState::Error,
            "No usable PATH directories were found.",
            "Restore PATH before using Acclorite."
        ));
    } else {
        report.checks.push_back(check(
            "path", "Core", "PATH discovery", DoctorState::Ready,
            std::to_string(path_dirs) + " usable PATH " + (path_dirs == 1 ? "directory" : "directories")
        ));
    }

    const auto index = IndexSource::probe();
    if (!index.sqlite_supported) {
        report.checks.push_back(check(
            "sqlite", "Core", "SQLite FTS", DoctorState::Optional,
            "Not compiled in; Acclorite will use live PATH/man/desktop sources."
        ));
    } else {
        report.checks.push_back(check(
            "sqlite", "Core", "SQLite FTS", DoctorState::Ready,
            "Compiled in"
        ));

        if (index.ready) {
            std::string detail = index.path.string();
            if (index.size_bytes > 0) {
                detail += " (" + std::to_string(index.size_bytes / 1024) + " KiB)";
            }

            if (index.stale) {
                detail += "; stale";
                if (!index.stale_sources.empty()) {
                    detail += ": ";
                    for (std::size_t i = 0; i < index.stale_sources.size(); ++i) {
                        if (i != 0) {
                            detail += ", ";
                        }
                        detail += index.stale_sources[i];
                    }
                }
                report.checks.push_back(check(
                    "index", "Core", "Persistent index", DoctorState::Warning,
                    std::move(detail),
                    index.fingerprinted
                        ? "Run: acclorite --reindex (normal searches also refresh stale indexes automatically)"
                        : "Run once: acclorite --reindex to add freshness fingerprints"
                ));
            } else {
                detail += "; fresh";
                report.checks.push_back(check(
                    "index", "Core", "Persistent index", DoctorState::Ready,
                    std::move(detail)
                ));
            }
        } else if (!index.exists) {
            report.checks.push_back(check(
                "index", "Core", "Persistent index", DoctorState::Warning,
                "Index is missing: " + index.path.string(),
                "Run: acclorite --reindex"
            ));
        } else {
            report.checks.push_back(check(
                "index", "Core", "Persistent index", DoctorState::Warning,
                "Index exists but is unreadable or has an incompatible schema: " + index.path.string(),
                "Run: acclorite --reindex"
            ));
        }
    }

    const auto apropos = executable_detail("apropos");
    const auto man = executable_detail("man");
    if (!apropos.empty()) {
        report.checks.push_back(check(
            "manuals", "Knowledge", "Manual-page lookup", DoctorState::Ready,
            "apropos: " + apropos
        ));
    } else if (!man.empty()) {
        report.checks.push_back(check(
            "manuals", "Knowledge", "Manual-page lookup", DoctorState::Ready,
            "man -k fallback: " + man
        ));
    } else {
        report.checks.push_back(check(
            "manuals", "Knowledge", "Manual-page lookup", DoctorState::Warning,
            "Neither apropos nor man was found; local semantic documentation is reduced."
        ));
    }

    const std::string desktop = desktop_detail();
    if (!desktop.empty()) {
        report.checks.push_back(check(
            "desktop", "Knowledge", "Desktop metadata", DoctorState::Ready,
            desktop
        ));
    } else {
        report.checks.push_back(check(
            "desktop", "Knowledge", "Desktop metadata", DoctorState::Optional,
            "No XDG application metadata directories were found."
        ));
    }

    const auto expac = executable_detail("expac");
    const auto pacman = executable_detail("pacman");
    const auto apt_cache = executable_detail("apt-cache");
    const auto dpkg_query = executable_detail("dpkg-query");
    const auto rpm = executable_detail("rpm");
    const auto dnf_frontend = DnfSource::frontend();
    const auto zypper = executable_detail("zypper");
    const auto xbps_query = executable_detail("xbps-query");
    const auto xbps_uhelper = executable_detail("xbps-uhelper");
    const bool arch_available = !expac.empty() || !pacman.empty();
    const bool apt_available = !apt_cache.empty();
    const bool dnf_available = dnf_frontend.has_value();
    const bool zypper_available = !zypper.empty() && ZypperSource{}.available();
    const bool xbps_available = !xbps_query.empty() && !xbps_uhelper.empty() && XbpsSource{}.available();
    const auto package_family = system::choose_package_family(
        system::detect_distro(),
        system::PackageBackendAvailability{
            .arch = arch_available,
            .debian = apt_available,
            .fedora = dnf_available,
            .suse = zypper_available,
            .void_linux = xbps_available,
        }
    );

    if (package_family == PackageFamily::Arch) {
        const bool expac_ready = !expac.empty() && expac_sync_ready();
        const bool pacman_ready = !pacman.empty() && pacman_sync_ready();

        if (expac.empty()) {
            report.checks.push_back(check(
                "expac", "Arch integration", "expac metadata", DoctorState::Optional,
                "expac is not installed; pacman presentation output remains available."
            ));
        } else if (expac_ready) {
            report.checks.push_back(check(
                "expac", "Arch integration", "expac metadata", DoctorState::Ready,
                "Sync metadata is readable: " + expac
            ));
        } else {
            report.checks.push_back(check(
                "expac", "Arch integration", "expac metadata", DoctorState::Warning,
                "expac is installed but could not read the synchronized package database: " + expac,
                "Refresh package databases using your normal system update workflow."
            ));
        }

        if (pacman.empty()) {
            report.checks.push_back(check(
                "pacman", "Arch integration", "pacman fallback", DoctorState::Optional,
                "pacman is not detected."
            ));
        } else if (pacman_ready) {
            report.checks.push_back(check(
                "pacman", "Arch integration", "pacman fallback", DoctorState::Ready,
                "Sync search is readable: " + pacman
            ));
        } else {
            report.checks.push_back(check(
                "pacman", "Arch integration", "pacman fallback", DoctorState::Warning,
                "pacman is installed but synchronized repository search returned no usable metadata: " + pacman,
                "Refresh package databases using your normal system update workflow."
            ));
        }

        if (expac_ready) {
            report.checks.push_back(check(
                "arch-search", "Arch integration", "Repository package search", DoctorState::Ready,
                "Using expac structured sync metadata" + std::string(pacman_ready ? " with pacman fallback" : "")
            ));
        } else if (pacman_ready) {
            report.checks.push_back(check(
                "arch-search", "Arch integration", "Repository package search", DoctorState::Ready,
                "Using pacman -Ss fallback",
                expac.empty()
                    ? "Optional: install expac for structured ALPM metadata."
                    : "expac probe failed; Acclorite can still use pacman fallback."
            ));
        } else {
            report.checks.push_back(check(
                "arch-search", "Arch integration", "Repository package search", DoctorState::Warning,
                "Arch helpers are installed, but no synchronized repository metadata could be read.",
                "Refresh package databases using your normal system update workflow."
            ));
        }

        const auto pkgfile = executable_detail("pkgfile");
        if (pkgfile.empty()) {
            report.checks.push_back(check(
                "pkgfile", "Arch integration", "pkgfile helper", DoctorState::Optional,
                "pkgfile is not installed; repository package → binary mapping is unavailable."
            ));
            report.checks.push_back(check(
                "pkgfile-metadata", "Arch integration", "pkgfile metadata", DoctorState::Optional,
                "Not checked because pkgfile is unavailable."
            ));
        } else {
            report.checks.push_back(check(
                "pkgfile", "Arch integration", "pkgfile helper", DoctorState::Ready,
                pkgfile
            ));

            if (pkgfile_metadata_ready()) {
                report.checks.push_back(check(
                    "pkgfile-metadata", "Arch integration", "pkgfile metadata", DoctorState::Ready,
                    "Repository .files metadata is usable"
                ));
            } else {
                report.checks.push_back(check(
                    "pkgfile-metadata", "Arch integration", "pkgfile metadata", DoctorState::Warning,
                    "pkgfile is installed, but repository .files metadata is unavailable or unusable.",
                    "Run: sudo pkgfile --update"
                ));
            }
        }
    } else if (package_family == PackageFamily::Debian) {
        const bool cache_ready = apt_cache_ready();
        const bool dpkg_ready = !dpkg_query.empty() && dpkg_query_ready();

        report.checks.push_back(check(
            "apt-cache", "Debian integration", "APT package metadata",
            cache_ready ? DoctorState::Ready : DoctorState::Warning,
            cache_ready
                ? "Local APT package metadata is readable: " + apt_cache
                : "apt-cache is present, but the local package cache could not be read: " + apt_cache,
            cache_ready ? std::string{} : "Refresh package metadata using your normal APT update workflow."
        ));

        if (dpkg_query.empty()) {
            report.checks.push_back(check(
                "dpkg-query", "Debian integration", "Installed package state", DoctorState::Optional,
                "dpkg-query is unavailable; repository discovery still works but installed-package state is reduced."
            ));
        } else if (dpkg_ready) {
            report.checks.push_back(check(
                "dpkg-query", "Debian integration", "Installed package state", DoctorState::Ready,
                "Installed package metadata is readable: " + dpkg_query
            ));
        } else {
            report.checks.push_back(check(
                "dpkg-query", "Debian integration", "Installed package state", DoctorState::Warning,
                "dpkg-query is present but could not read the package database: " + dpkg_query
            ));
        }

        report.checks.push_back(check(
            "apt-search", "Debian integration", "Repository package search",
            cache_ready ? DoctorState::Ready : DoctorState::Warning,
            cache_ready
                ? "Using apt-cache search against local package metadata"
                : "APT package discovery is available but local metadata is not ready.",
            cache_ready ? std::string{} : "Refresh package metadata using your normal APT update workflow."
        ));
    } else if (package_family == PackageFamily::Fedora) {
        const std::string frontend = dnf_frontend.value_or(std::string{});
        const bool cache_ready = dnf_cache_ready(frontend);
        const bool rpm_ready = !rpm.empty() && rpm_query_ready();

        report.checks.push_back(check(
            "dnf-cache", "Fedora integration", "DNF package metadata",
            cache_ready ? DoctorState::Ready : DoctorState::Warning,
            cache_ready
                ? "Local DNF repository metadata is readable through " + frontend + " --cacheonly"
                : "DNF is present, but cached repository metadata could not be read through " + frontend + ".",
            cache_ready ? std::string{} : "Refresh package metadata using your normal DNF update workflow."
        ));

        if (rpm.empty()) {
            report.checks.push_back(check(
                "rpm-query", "Fedora integration", "Installed package state", DoctorState::Optional,
                "rpm is unavailable; repository discovery still works but installed-package state is reduced."
            ));
        } else if (rpm_ready) {
            report.checks.push_back(check(
                "rpm-query", "Fedora integration", "Installed package state", DoctorState::Ready,
                "Installed RPM metadata is readable: " + rpm
            ));
        } else {
            report.checks.push_back(check(
                "rpm-query", "Fedora integration", "Installed package state", DoctorState::Warning,
                "rpm is present but could not read the installed package database: " + rpm
            ));
        }

        report.checks.push_back(check(
            "dnf-search", "Fedora integration", "Repository package search",
            cache_ready ? DoctorState::Ready : DoctorState::Warning,
            cache_ready
                ? "Using " + frontend + " --cacheonly search/repoquery against local metadata"
                : "DNF package discovery is available but cached metadata is not ready.",
            cache_ready ? std::string{} : "Refresh package metadata using your normal DNF update workflow."
        ));
    } else if (package_family == PackageFamily::Suse) {
        const bool cache_ready = ZypperSource::cached_metadata_ready();
        const bool rpm_ready = !rpm.empty() && rpm_query_ready();

        report.checks.push_back(check(
            "zypper-cache", "openSUSE integration", "Zypper package metadata",
            cache_ready ? DoctorState::Ready : DoctorState::Warning,
            cache_ready
                ? "Local Zypper repository metadata is readable with refresh disabled: " + zypper
                : "Zypper is present, but local repository metadata could not be read without refreshing: " + zypper,
            cache_ready ? std::string{} : "Refresh package metadata using your normal Zypper workflow."
        ));

        if (rpm.empty()) {
            report.checks.push_back(check(
                "rpm-query", "openSUSE integration", "Installed package state", DoctorState::Optional,
                "rpm is unavailable; repository discovery still works but installed-package state is reduced."
            ));
        } else if (rpm_ready) {
            report.checks.push_back(check(
                "rpm-query", "openSUSE integration", "Installed package state", DoctorState::Ready,
                "Installed RPM metadata is readable: " + rpm
            ));
        } else {
            report.checks.push_back(check(
                "rpm-query", "openSUSE integration", "Installed package state", DoctorState::Warning,
                "rpm is present but could not read the installed package database: " + rpm
            ));
        }

        report.checks.push_back(check(
            "zypper-search", "openSUSE integration", "Repository package search",
            cache_ready ? DoctorState::Ready : DoctorState::Warning,
            cache_ready
                ? "Using non-interactive XML search against local Zypper metadata with --no-refresh"
                : "Zypper package discovery is available but local metadata is not ready.",
            cache_ready ? std::string{} : "Refresh package metadata using your normal Zypper workflow."
        ));
    } else if (package_family == PackageFamily::Void) {
        const bool cache_ready = XbpsSource::cached_metadata_ready();
        const bool installed_ready = XbpsSource::installed_metadata_ready();

        report.checks.push_back(check(
            "xbps-cache", "Void integration", "XBPS package metadata",
            cache_ready ? DoctorState::Ready : DoctorState::Warning,
            cache_ready
                ? "Synchronized XBPS repository indexes are readable: " + xbps_query
                : "xbps-query is present, but no usable synchronized repository index could be read: " + xbps_query,
            cache_ready ? std::string{} : "Synchronize package metadata using your normal XBPS update workflow."
        ));

        report.checks.push_back(check(
            "xbps-pkgdb", "Void integration", "Installed package state",
            installed_ready ? DoctorState::Ready : DoctorState::Warning,
            installed_ready
                ? "Installed XBPS package metadata is readable"
                : "xbps-query could not read the installed package database.",
            installed_ready ? std::string{} : "Check the local XBPS package database before relying on installed-state metadata."
        ));

        report.checks.push_back(check(
            "xbps-search", "Void integration", "Repository package search",
            cache_ready ? DoctorState::Ready : DoctorState::Warning,
            cache_ready
                ? "Using xbps-query repository search against synchronized on-disk metadata; --memory-sync is never used"
                : "XBPS package discovery is available but synchronized repository metadata is not ready.",
            cache_ready ? std::string{} : "Synchronize package metadata using your normal XBPS update workflow."
        ));
    } else {
        report.checks.push_back(check(
            "package-search", "Package integration", "Repository package search", DoctorState::Optional,
            "No supported package backend was detected; generic Linux discovery remains available."
        ));
    }

    return report;
}

} // namespace acclorite::diagnostics
