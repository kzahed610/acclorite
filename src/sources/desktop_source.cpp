#include "acclorite/sources/desktop_source.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "acclorite/query/relevance.hpp"
#include "acclorite/ranking/evidence.hpp"
#include "acclorite/system/executable.hpp"

namespace acclorite {
namespace {

struct DesktopEntry {
    std::string name;
    std::string generic_name;
    std::string comment;
    std::string exec;
    std::string try_exec;
    bool application{false};
};

std::string trim(std::string value) {
    const auto not_space = [](const unsigned char ch) { return !std::isspace(ch); };
    const auto first = std::ranges::find_if(value, not_space);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if(value.rbegin(), value.rend(), not_space).base();
    return std::string(first, last);
}

std::vector<std::filesystem::path> split_paths(const char* raw, const char separator) {
    std::vector<std::filesystem::path> result;
    if (!raw) {
        return result;
    }

    std::string text(raw);
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find(separator, start);
        const auto item = text.substr(start, end - start);
        if (!item.empty()) {
            result.emplace_back(item);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return result;
}

DesktopEntry parse_desktop_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    DesktopEntry entry;
    if (!input) {
        return entry;
    }

    bool in_desktop_entry = false;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.starts_with('[') && line.ends_with(']')) {
            in_desktop_entry = line == "[Desktop Entry]";
            continue;
        }
        if (!in_desktop_entry || line.empty() || line.starts_with('#')) {
            continue;
        }

        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        const std::string key = line.substr(0, equals);
        const std::string value = trim(line.substr(equals + 1));

        // Ignore translated variants for now; the non-localized fields are stable
        // indexing material and avoid locale-dependent cache contents.
        if (key.find('[') != std::string::npos) {
            continue;
        }

        if (key == "Type") {
            entry.application = value == "Application";
        } else if (key == "Name") {
            entry.name = value;
        } else if (key == "GenericName") {
            entry.generic_name = value;
        } else if (key == "Comment") {
            entry.comment = value;
        } else if (key == "Exec") {
            entry.exec = value;
        } else if (key == "TryExec") {
            entry.try_exec = value;
        }
    }

    return entry;
}

std::vector<std::string> split_exec(std::string_view exec) {
    std::vector<std::string> tokens;
    std::string current;
    char quote = '\0';
    bool escaped = false;

    for (const char ch : exec) {
        if (escaped) {
            current.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            } else {
                current.push_back(ch);
            }
            continue;
        }
        if (ch == '\'' || ch == '"') {
            quote = ch;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!current.empty()) {
                tokens.push_back(std::move(current));
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }

    if (!current.empty()) {
        tokens.push_back(std::move(current));
    }
    return tokens;
}

bool environment_assignment(const std::string_view token) {
    const auto equals = token.find('=');
    return equals != std::string_view::npos && equals > 0 && !token.starts_with('%');
}

std::string executable_from_entry(const DesktopEntry& entry) {
    if (!entry.try_exec.empty()) {
        return std::filesystem::path(entry.try_exec).filename().string();
    }

    auto tokens = split_exec(entry.exec);
    if (tokens.empty()) {
        return {};
    }

    std::size_t index = 0;
    if (std::filesystem::path(tokens[index]).filename() == "env") {
        ++index;
        while (index < tokens.size() &&
               (tokens[index].starts_with('-') || environment_assignment(tokens[index]))) {
            ++index;
        }
    }

    while (index < tokens.size() && tokens[index].starts_with('%')) {
        ++index;
    }
    if (index >= tokens.size()) {
        return {};
    }

    return std::filesystem::path(tokens[index]).filename().string();
}

std::string description_for(const DesktopEntry& entry) {
    if (!entry.comment.empty()) {
        return entry.comment;
    }
    if (!entry.generic_name.empty()) {
        return entry.generic_name;
    }
    return entry.name;
}

} // namespace

std::vector<std::filesystem::path> DesktopSource::application_directories() {
    if (const char* override_dirs = std::getenv("ACCLORITE_DESKTOP_DIRS")) {
        return split_paths(override_dirs, ':');
    }

    std::vector<std::filesystem::path> result;

    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        result.emplace_back(std::filesystem::path(data_home) / "applications");
    } else if (const char* home = std::getenv("HOME")) {
        result.emplace_back(std::filesystem::path(home) / ".local/share/applications");
    }

    const char* data_dirs = std::getenv("XDG_DATA_DIRS");
    const auto roots = split_paths(data_dirs ? data_dirs : "/usr/local/share:/usr/share", ':');
    for (const auto& root : roots) {
        result.emplace_back(root / "applications");
    }

    return result;
}

bool DesktopSource::available() const {
    return std::ranges::any_of(application_directories(), [](const auto& directory) {
        std::error_code error;
        return std::filesystem::is_directory(directory, error);
    });
}

std::vector<Candidate> DesktopSource::catalog() {
    std::unordered_map<std::string, Candidate> candidates;

    for (const auto& directory : application_directories()) {
        std::error_code error;
        if (!std::filesystem::is_directory(directory, error)) {
            continue;
        }

        for (std::filesystem::directory_iterator it(
                 directory,
                 std::filesystem::directory_options::skip_permission_denied,
                 error
             ); !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
            const auto& path = it->path();
            if (path.extension() != ".desktop") {
                continue;
            }

            const DesktopEntry entry = parse_desktop_file(path);
            if (!entry.application) {
                continue;
            }

            const std::string command = executable_from_entry(entry);
            if (command.empty()) {
                continue;
            }

            const std::string description = description_for(entry);
            const auto executable = system::find_executable(command);
            Candidate incoming{
                .command = command,
                .path = executable ? executable->string() : std::string{},
                .summary = description.empty() ? entry.name : description,
                .source = "desktop",
                .package = {},
                .repository = {},
                .package_version = {},
                .installed = true,
                .repository_available = false,
                .cli_capable = executable.has_value(),
                .gui_capable = true,
                .matched_terms = {},
                .provided_commands = {},
                .descriptive_evidence = {},
                .examples = {},
                .learning_resources = {},
                .evidence_trace = {},
                .base_merge_trace = {},
                .score = 0.0,
                .ranking = std::nullopt,
            };

            auto [candidate_it, inserted] = candidates.try_emplace(command, incoming);
            if (!inserted && candidate_it->second.summary.empty() && !incoming.summary.empty()) {
                candidate_it->second.summary = std::move(incoming.summary);
            }
        }
    }

    std::vector<Candidate> result;
    result.reserve(candidates.size());
    for (auto& [_, candidate] : candidates) {
        result.push_back(std::move(candidate));
    }
    return result;
}

std::vector<Candidate> DesktopSource::search(const Query& query) const {
    std::vector<Candidate> result;
    for (auto candidate : catalog()) {
        const auto match = query::score_text(query, candidate.command, candidate.summary, 0.95, 0.92);
        if (match.score <= 0.0) {
            continue;
        }
        candidate.score = match.score;
        candidate.semantic_fit = match.semantic_fit;
        candidate.matched_terms = match.matched_terms;
        if (query.explain_ranking) {
            candidate.evidence_trace.push_back(ranking::semantic_evidence(
                "desktop", "desktop name/description semantic match", match, {}, candidate.score
            ));
        }
        result.push_back(std::move(candidate));
    }
    return result;
}

} // namespace acclorite
