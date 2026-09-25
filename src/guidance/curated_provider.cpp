#include "acclorite/guidance/curated_provider.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <ranges>
#include <sstream>
#include <string_view>

#ifndef ACCLORITE_CURATED_GUIDANCE_BUILD_FILE
#define ACCLORITE_CURATED_GUIDANCE_BUILD_FILE ""
#endif

#ifndef ACCLORITE_CURATED_GUIDANCE_INSTALL_FILE
#define ACCLORITE_CURATED_GUIDANCE_INSTALL_FILE ""
#endif

namespace acclorite {
namespace {

constexpr std::size_t kMaxMetadataBytes = 256 * 1024;
constexpr std::size_t kMaxExamples = 2;
constexpr std::string_view kCurator = "acclorite-project";

std::string trim(std::string value) {
    const auto first = std::ranges::find_if(value, [](const unsigned char ch) {
        return !std::isspace(ch);
    });
    value.erase(value.begin(), first);
    const auto last = std::ranges::find_if(value | std::views::reverse, [](const unsigned char ch) {
        return !std::isspace(ch);
    }).base();
    value.erase(last, value.end());
    return value;
}

std::string lowercase(std::string value) {
    std::ranges::transform(value, value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool safe_command_name(std::string_view value) {
    if (value.empty() || value.size() > 128) {
        return false;
    }
    return std::ranges::all_of(value, [](const unsigned char ch) {
        return std::isalnum(ch) || ch == '-' || ch == '_' || ch == '+' || ch == '.';
    });
}

bool iso_date(std::string_view value) {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
        return false;
    }
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (i == 4 || i == 7) {
            continue;
        }
        if (!std::isdigit(static_cast<unsigned char>(value[i]))) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (start <= line.size()) {
        const auto tab = line.find('\t', start);
        const auto end = tab == std::string::npos ? line.size() : tab;
        fields.push_back(line.substr(start, end - start));
        if (tab == std::string::npos) {
            break;
        }
        start = tab + 1;
    }
    return fields;
}

bool http_target(std::string_view value) {
    return value.starts_with("https://") || value.starts_with("http://");
}

std::optional<std::filesystem::path> executable_relative_path() {
#ifdef __linux__
    std::error_code error;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error && !executable.empty()) {
        const auto candidate = executable.parent_path().parent_path() /
                               "share" / "acclorite" / "curated-guidance.tsv";
        if (std::filesystem::is_regular_file(candidate, error) && !error) {
            return candidate;
        }
    }
#endif
    return std::nullopt;
}

std::optional<std::filesystem::path> first_existing_path() {
    // Installed binaries should remain self-contained even when a package manager stages
    // or relocates the configured CMake prefix. /proc/self/exe resolves the actual binary
    // location on Linux, so ../share/acclorite follows /usr, /usr/local, ~/.local, and
    // DESTDIR layouts without embedding a runtime dependency on the build tree.
    if (const auto relative = executable_relative_path()) {
        return relative;
    }

    for (const char* raw : {ACCLORITE_CURATED_GUIDANCE_INSTALL_FILE,
                            ACCLORITE_CURATED_GUIDANCE_BUILD_FILE}) {
        if (raw == nullptr || *raw == '\0') {
            continue;
        }
        std::error_code error;
        const std::filesystem::path path(raw);
        if (std::filesystem::is_regular_file(path, error) && !error) {
            return path;
        }
    }
    return std::nullopt;
}

} // namespace

CuratedGuidanceProvider::CuratedGuidanceProvider(
    std::filesystem::path metadata_path,
    std::string verified_by
) {
    if (metadata_path.empty()) {
        if (const char* override_path = std::getenv("ACCLORITE_CURATED_GUIDANCE");
            override_path != nullptr && *override_path != '\0') {
            metadata_path = std::filesystem::path(override_path);
            verified_by = "local-override";
        } else if (const auto detected = first_existing_path()) {
            metadata_path = *detected;
            verified_by = std::string(kCurator);
        }
    } else if (verified_by.empty()) {
        verified_by = "local-curation";
    }
    verified_by_ = std::move(verified_by);
    metadata_path_ = std::move(metadata_path);
    if (metadata_path_.empty()) {
        return;
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(metadata_path_, error) || error) {
        return;
    }
    const auto size = std::filesystem::file_size(metadata_path_, error);
    if (error || size == 0 || size > kMaxMetadataBytes) {
        return;
    }

    std::ifstream file(metadata_path_);
    if (!file) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim(std::move(line));
        if (line.empty() || line.starts_with('#')) {
            continue;
        }

        auto fields = split_tabs(line);
        if (fields.size() != 7) {
            continue;
        }
        for (auto& field : fields) {
            field = trim(std::move(field));
        }

        const std::string command = lowercase(fields[0]);
        if (!safe_command_name(command) || fields[2].empty() || fields[4].empty() ||
            !http_target(fields[5]) || !iso_date(fields[6])) {
            continue;
        }

        Entry entry;
        if (fields[1] == "example") {
            entry.kind = Entry::Kind::Example;
        } else if (fields[1] == "resource") {
            entry.kind = Entry::Kind::Resource;
            if (fields[3].empty() || !http_target(fields[4])) {
                continue;
            }
        } else {
            continue;
        }

        entry.id = std::move(fields[2]);
        entry.label = std::move(fields[3]);
        entry.value = std::move(fields[4]);
        entry.source_reference = std::move(fields[5]);
        entry.verified_on = std::move(fields[6]);
        entries_[command].push_back(std::move(entry));
    }
}

bool CuratedGuidanceProvider::available() const {
    return !entries_.empty();
}

GuidanceBundle CuratedGuidanceProvider::guide(const Candidate& candidate) const {
    GuidanceBundle bundle;
    if (!candidate.cli_capable || candidate.command.empty()) {
        return bundle;
    }

    const auto found = entries_.find(lowercase(candidate.command));
    if (found == entries_.end()) {
        return bundle;
    }

    std::size_t examples = 0;
    for (const auto& entry : found->second) {
        if (entry.kind == Entry::Kind::Resource) {
            bundle.learning_resources.push_back(LearningResource{
                .label = entry.label,
                .target = entry.value,
                .source_kind = GuidanceSourceKind::Curated,
                .source_reference = entry.source_reference,
                .verified = true,
                .verified_by = verified_by_,
                .verified_on = entry.verified_on,
            });
            continue;
        }

        if (!candidate.examples.empty() || examples >= kMaxExamples) {
            continue;
        }
        bundle.examples.push_back(UsageExample{
            .text = entry.value,
            .source_kind = GuidanceSourceKind::Curated,
            .source_reference = entry.source_reference,
            .verified = true,
            .verified_by = verified_by_,
            .verified_on = entry.verified_on,
        });
        ++examples;
    }

    return bundle;
}

} // namespace acclorite
