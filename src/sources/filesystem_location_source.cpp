#include "acclorite/sources/filesystem_location_source.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace acclorite {
namespace {

std::string lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool looks_like_config_query(const Query& query) {
    return std::ranges::any_of(query.targets, [](const std::string& target) {
        const std::string normalized = lower(target);
        return normalized == "config" || normalized == "configuration" ||
               normalized == "conf" || normalized == "settings";
    });
}

void add_if_existing(
    std::vector<LocationHit>& hits,
    std::unordered_set<std::string>& seen,
    const std::filesystem::path& path,
    const std::string_view kind,
    const double score
) {
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error) {
        return;
    }
    const std::string normalized = path.lexically_normal().string();
    if (!seen.insert(normalized).second) {
        return;
    }
    hits.push_back(LocationHit{
        .path = normalized,
        .kind = std::string(kind),
        .source = "filesystem",
        .score = score,
    });
}

void scan_directory_for_config(
    std::vector<LocationHit>& hits,
    std::unordered_set<std::string>& seen,
    const std::filesystem::path& directory,
    const std::string_view target,
    const double base_score
) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || error) {
        return;
    }

    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        const auto& entry = *it;
        if (!entry.is_regular_file(error) && !entry.is_symlink(error)) {
            error.clear();
            continue;
        }

        const std::string filename = lower(entry.path().filename().string());
        const bool configish = filename.find("config") != std::string::npos ||
                               filename.ends_with(".conf") || filename == "conf";
        if (!configish) {
            continue;
        }

        double score = base_score;
        if (!target.empty() && filename.find(std::string(target)) != std::string::npos) {
            score += 0.05;
        }
        add_if_existing(hits, seen, entry.path(), "config", std::min(1.0, score));
    }
}

} // namespace

FilesystemLocationSource::FilesystemLocationSource(std::filesystem::path system_config_root)
    : system_config_root_(std::move(system_config_root)) {}

bool FilesystemLocationSource::available() const {
    return true;
}

std::vector<LocationHit> FilesystemLocationSource::search(const Query& query) const {
    if (query.frame.frame != QueryFrame::Locate || query.targets.empty()) {
        return {};
    }

    std::string target = query.targets.front();
    if (target == "config" || target == "configuration" || target == "conf" || target == "settings") {
        return {};
    }

    if (!looks_like_config_query(query)) {
        return {};
    }

    const std::string normalized_target = lower(target);
    std::vector<LocationHit> hits;
    std::unordered_set<std::string> seen;

    const char* home_raw = std::getenv("HOME");
    const std::filesystem::path home = home_raw ? std::filesystem::path(home_raw) : std::filesystem::path{};
    const char* xdg_raw = std::getenv("XDG_CONFIG_HOME");
    const std::filesystem::path xdg = xdg_raw
        ? std::filesystem::path(xdg_raw)
        : (home.empty() ? std::filesystem::path{} : home / ".config");

    // Bounded, existing-path-only probes. This deliberately avoids recursive home
    // directory scanning and therefore remains fast, predictable, and privacy sane.
    if (!home.empty()) {
        add_if_existing(hits, seen, home / ("." + normalized_target) / "config", "user config", 1.00);
        if (!xdg.empty()) {
            add_if_existing(hits, seen, xdg / normalized_target / "config", "user config", 0.98);
            add_if_existing(hits, seen, xdg / (normalized_target + ".conf"), "user config", 0.96);
            scan_directory_for_config(hits, seen, xdg / normalized_target, normalized_target, 0.91);
        }
    }

    const std::filesystem::path system_dir = system_config_root_ / normalized_target;
    add_if_existing(hits, seen, system_config_root_ / (normalized_target + ".conf"), "system config", 0.92);
    add_if_existing(hits, seen, system_dir / "config", "system config", 0.91);
    add_if_existing(hits, seen, system_dir / (normalized_target + ".conf"), "system config", 0.94);
    add_if_existing(hits, seen, system_dir / (normalized_target + "_config"), "system config", 0.96);
    scan_directory_for_config(hits, seen, system_dir, normalized_target, 0.90);

    std::ranges::sort(hits, [](const LocationHit& left, const LocationHit& right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        return left.path < right.path;
    });
    if (hits.size() > 8) {
        hits.resize(8);
    }
    return hits;
}

} // namespace acclorite
