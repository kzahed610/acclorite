#include "acclorite/diagnostics/doctor.hpp"

#include <algorithm>
#include <filesystem>
#include <ranges>
#include <sstream>
#include <string_view>

#include "acclorite/sources/desktop_source.hpp"
#include "acclorite/sources/index_source.hpp"
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
    const bool arch_available = !expac.empty() || !pacman.empty();

    const bool expac_ready = !expac.empty() && expac_sync_ready();
    const bool pacman_ready = !pacman.empty() && pacman_sync_ready();

    if (expac.empty()) {
        report.checks.push_back(check(
            "expac", "Arch integration", "expac metadata", DoctorState::Optional,
            arch_available
                ? "expac is not installed; pacman presentation output remains available."
                : "expac is not detected; this is optional outside Arch-family integration."
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
    } else if (arch_available) {
        report.checks.push_back(check(
            "arch-search", "Arch integration", "Repository package search", DoctorState::Warning,
            "Arch helpers are installed, but no synchronized repository metadata could be read.",
            "Refresh package databases using your normal system update workflow."
        ));
    } else {
        report.checks.push_back(check(
            "arch-search", "Arch integration", "Repository package search", DoctorState::Optional,
            "Arch-family package discovery is not detected; generic Linux discovery remains available."
        ));
    }

    const auto pkgfile = executable_detail("pkgfile");
    if (pkgfile.empty()) {
        report.checks.push_back(check(
            "pkgfile", "Arch integration", "pkgfile helper", DoctorState::Optional,
            arch_available
                ? "pkgfile is not installed; repository package → binary mapping is unavailable."
                : "pkgfile is not installed; this is optional outside the Arch package backend."
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

    return report;
}

} // namespace acclorite::diagnostics
