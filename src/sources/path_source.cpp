#include "acclorite/sources/path_source.hpp"

#include <algorithm>
#include <cstdlib>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_set>

#include <unistd.h>

namespace acclorite {
namespace {

std::string lower(std::string_view input) {
    std::string out(input);
    std::ranges::transform(out, out.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

} // namespace

bool PathSource::available() const {
    const char* path = std::getenv("PATH");
    return path != nullptr && *path != '\0';
}

std::vector<std::filesystem::path> PathSource::path_directories() {
    std::vector<std::filesystem::path> directories;

    const char* raw_path = std::getenv("PATH");
    if (!raw_path) {
        return directories;
    }

    std::string path(raw_path);
    std::size_t start = 0;

    while (start <= path.size()) {
        const std::size_t end = path.find(':', start);
        const std::string entry = path.substr(start, end - start);
        directories.emplace_back(entry.empty() ? "." : entry);

        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    return directories;
}

double PathSource::score_name(const Query& query, const std::string& command) {
    if (query.normalized.empty()) {
        return 0.0;
    }

    const std::string command_lower = lower(command);

    if (command_lower == query.normalized) {
        return 1.0;
    }

    if (command_lower.starts_with(query.normalized)) {
        return 0.86;
    }

    if (command_lower.find(query.normalized) != std::string::npos) {
        return 0.72;
    }

    double best = 0.0;
    std::size_t matches = 0;

    for (const auto& token : query.tokens) {
        if (token.empty()) {
            continue;
        }

        if (command_lower == token) {
            best = std::max(best, 0.96);
            ++matches;
        } else if (command_lower.starts_with(token)) {
            best = std::max(best, 0.78);
            ++matches;
        } else if (command_lower.find(token) != std::string::npos) {
            best = std::max(best, 0.62);
            ++matches;
        }
    }

    if (matches > 1) {
        best = std::min(0.95, best + 0.04 * static_cast<double>(matches - 1));
    }

    return best;
}

std::vector<Candidate> PathSource::search(const Query& query) const {
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

            const double score = score_name(query, command);
            if (score <= 0.0) {
                continue;
            }

            seen_commands.insert(command);
            candidates.push_back(Candidate{
                .command = command,
                .path = path.string(),
                .summary = "Executable available in PATH",
                .source = "path",
                .installed = true,
                .repository_available = false,
                .matched_terms = {},
                .score = score,
            });
        }
    }

    return candidates;
}

} // namespace acclorite
