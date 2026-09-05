#include "acclorite/sources/path_source.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_set>

#include <unistd.h>

#include "acclorite/query/fuzzy.hpp"
#include "acclorite/query/lexicon.hpp"
#include "acclorite/ranking/evidence.hpp"
#include "acclorite/system/executable.hpp"

namespace acclorite {
namespace {

std::string lower(std::string_view input) {
    std::string out(input);
    std::ranges::transform(out, out.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

bool approximately_contains(const std::string_view haystack, const std::string_view needle) {
    if (needle.empty()) {
        return false;
    }
    if (haystack.find(needle) != std::string_view::npos) {
        return true;
    }

    // Cheap morphology tolerance for command names such as "archiver" vs
    // "archive". Real fuzzy matching comes later with RapidFuzz.
    const std::size_t common_needed = std::min<std::size_t>(4, needle.size());
    if (common_needed < 4 || haystack.size() < 4) {
        return false;
    }

    const std::size_t limit = std::min(haystack.size(), needle.size());
    std::size_t common = 0;
    while (common < limit && haystack[common] == needle[common]) {
        ++common;
    }
    return common >= 4;
}

} // namespace

bool PathSource::available() const {
    return !system::path_directories().empty();
}

std::vector<std::filesystem::path> PathSource::path_directories() {
    return system::path_directories();
}

double PathSource::score_name(const Query& query, const std::string& command) {
    if (query.normalized.empty()) {
        return 0.0;
    }

    const std::string command_lower = lower(command);

    if (command_lower == query.normalized) {
        return 1.0;
    }

    const auto terms = query::meaningful_terms(query);
    if (terms.empty()) {
        return 0.0;
    }

    if (terms.size() == 1) {
        const auto& term = terms.front();
        if (command_lower == term) {
            return 0.96;
        }
        if (command_lower.starts_with(term)) {
            return 0.78;
        }
        if (command_lower.find(term) != std::string::npos) {
            return 0.62;
        }

        // Single-token queries are often half-remembered command names. Keep this
        // below exact/prefix matches so typo tolerance never outranks certainty.
        const double similarity = query::fuzzy::similarity(command_lower, term);
        if (similarity >= 0.72) {
            return std::min(0.76, 0.32 + (0.58 * similarity));
        }
        return 0.0;
    }

    // With a natural-language multi-word query, executable-name coincidence is
    // weak evidence. PATH should support semantic sources, not let "text2image"
    // beat rg merely because the user wrote "search text".
    std::size_t matched = 0;
    for (const auto& term : terms) {
        if (approximately_contains(command_lower, term)) {
            ++matched;
        }
    }

    if (matched == 0) {
        return 0.0;
    }

    const double coverage = static_cast<double>(matched) / static_cast<double>(terms.size());
    if (matched == terms.size()) {
        return std::min(0.68, 0.42 + (0.26 * coverage));
    }

    return 0.16 + (0.24 * coverage);
}

std::vector<Candidate> PathSource::catalog() {
    std::vector<Candidate> candidates;
    std::unordered_set<std::string> seen_commands;

    for (const auto& directory : path_directories()) {
        std::error_code error;
        if (!std::filesystem::is_directory(directory, error)) {
            continue;
        }

        std::filesystem::directory_iterator iterator(
            directory,
            std::filesystem::directory_options::skip_permission_denied,
            error
        );

        if (error) {
            continue;
        }

        for (const auto& entry : iterator) {
            const auto path = entry.path();
            const std::string command = path.filename().string();

            if (command.empty() || seen_commands.contains(command)) {
                continue;
            }

            if (::access(path.c_str(), X_OK) != 0) {
                continue;
            }

            seen_commands.insert(command);
            candidates.push_back(Candidate{
                .command = command,
                .path = path.string(),
                .summary = "Executable available in PATH",
                .source = "path",
                .package = {},
                .repository = {},
                .package_version = {},
                .installed = true,
                .repository_available = false,
                .cli_capable = true,
                .gui_capable = false,
                .matched_terms = {},
                .provided_commands = {},
                .descriptive_evidence = {},
                .examples = {},
                .learning_resources = {},
                .evidence_trace = {},
                .base_merge_trace = {},
                .score = 0.0,
                .ranking = std::nullopt,
            });
        }
    }

    return candidates;
}

std::vector<Candidate> PathSource::search(const Query& query) const {
    std::vector<Candidate> result;
    for (auto candidate : catalog()) {
        candidate.score = score_name(query, candidate.command);
        candidate.semantic_fit = candidate.score;
        if (candidate.score > 0.0) {
            if (query.explain_ranking) {
                candidate.evidence_trace.push_back(ranking::opaque_evidence(
                    "path", "command-name match", candidate.score
                ));
            }
            result.push_back(std::move(candidate));
        }
    }
    return result;
}

} // namespace acclorite
