#include "acclorite/guidance/info_provider.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "acclorite/system/executable.hpp"
#include "acclorite/system/process.hpp"

namespace acclorite {
namespace {

std::string trim(std::string value) {
    const auto not_space = [](const unsigned char ch) { return !std::isspace(ch); };
    const auto first = std::ranges::find_if(value, not_space);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if(value.rbegin(), value.rend(), not_space).base();
    return std::string(first, last);
}

std::string lowercase(std::string value) {
    std::ranges::transform(value, value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string strip_prompt(std::string line) {
    line = trim(std::move(line));
    if (line.size() >= 2 && (line[0] == '$' || line[0] == '%' || line[0] == '#') &&
        std::isspace(static_cast<unsigned char>(line[1]))) {
        line = trim(line.substr(2));
    }
    return line;
}

bool starts_with_command(std::string_view line, std::string_view command) {
    const auto token_match = [&](std::string_view candidate) {
        if (!candidate.starts_with(command)) {
            return false;
        }
        if (candidate.size() == command.size()) {
            return true;
        }
        const unsigned char next = static_cast<unsigned char>(candidate[command.size()]);
        return std::isspace(next) || next == ';' || next == '|' || next == '&';
    };

    if (token_match(line)) {
        return true;
    }
    if (line.starts_with("sudo ")) {
        return token_match(line.substr(5));
    }
    if (line.starts_with("env ")) {
        const std::string needle = " " + std::string(command);
        const auto pos = line.find(needle);
        if (pos != std::string_view::npos) {
            return token_match(line.substr(pos + 1));
        }
    }
    return false;
}

bool underline(std::string_view raw_line) {
    const std::string line = trim(std::string(raw_line));
    if (line.size() < 3) {
        return false;
    }
    return std::ranges::all_of(line, [](const char ch) {
        return ch == '=' || ch == '-' || ch == '*' || ch == '.';
    });
}

bool example_heading(std::string_view raw_line, std::string_view next_line) {
    std::string line = trim(std::string(raw_line));
    if (line.empty() || line.size() > 120) {
        return false;
    }
    if (!line.empty() && line.back() == ':') {
        line.pop_back();
        line = trim(std::move(line));
    }

    const std::string lower = lowercase(line);
    const auto example_word = [&]() {
        std::size_t pos = 0;
        while ((pos = lower.find("example", pos)) != std::string::npos) {
            const bool left_boundary = pos == 0 ||
                !std::isalnum(static_cast<unsigned char>(lower[pos - 1]));
            const std::size_t word_end = pos + std::string_view("example").size();
            const bool plural = word_end < lower.size() && lower[word_end] == 's';
            const std::size_t after = word_end + (plural ? 1 : 0);
            const bool right_boundary = after == lower.size() ||
                !std::isalnum(static_cast<unsigned char>(lower[after]));
            if (left_boundary && right_boundary) {
                return true;
            }
            pos = word_end;
        }
        return false;
    };

    if (!example_word()) {
        return false;
    }

    // Texinfo prefixes chapter/section numbers (for example `1 Examples` or
    // `2.3 Usage examples`) and marks headings with an underline. Exact
    // one-word headings are also accepted for small hand-authored Info files.
    return lower == "example" || lower == "examples" || underline(next_line);
}

bool likely_section_heading(std::string_view raw_line, std::string_view next_line) {
    const std::string line = trim(std::string(raw_line));
    if (line.empty() || line.size() > 120 || !underline(next_line)) {
        return false;
    }
    return std::ranges::any_of(line, [](const unsigned char ch) {
        return std::isalpha(ch);
    });
}

std::vector<UsageExample> extract_examples(
    const std::string_view command,
    const std::string_view info_text
) {
    std::vector<std::string> lines;
    std::istringstream stream{std::string(info_text)};
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(std::move(line));
    }

    std::vector<UsageExample> examples;
    bool in_examples = false;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string_view next = i + 1 < lines.size() ? std::string_view(lines[i + 1]) : std::string_view{};

        if (example_heading(lines[i], next)) {
            in_examples = true;
            if (underline(next)) {
                ++i;
            }
            continue;
        }
        if (in_examples && likely_section_heading(lines[i], next)) {
            break;
        }
        if (!in_examples || underline(lines[i])) {
            continue;
        }

        std::string candidate = strip_prompt(lines[i]);
        if (candidate.empty() || !starts_with_command(candidate, command) || candidate.size() > 240) {
            continue;
        }

        const auto duplicate = std::ranges::find_if(examples, [&](const UsageExample& existing) {
            return existing.text == candidate;
        });
        if (duplicate != examples.end()) {
            continue;
        }

        examples.push_back(UsageExample{
            .text = std::move(candidate),
            .source_kind = GuidanceSourceKind::Info,
            .source_reference = "info:" + std::string(command),
            .verified = true,
            .verified_by = {},
            .verified_on = {},
        });
        if (examples.size() >= 2) {
            break;
        }
    }

    return examples;
}


std::vector<std::filesystem::path> info_directories() {
    std::vector<std::filesystem::path> directories;

    const auto append = [&](std::filesystem::path directory) {
        if (directory.empty()) {
            return;
        }
        const auto duplicate = std::ranges::find(directories, directory);
        if (duplicate == directories.end()) {
            directories.push_back(std::move(directory));
        }
    };
    const auto append_defaults = [&]() {
        if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
            append(std::filesystem::path(home) / ".local/share/info");
        }
        append("/usr/local/share/info");
        append("/usr/local/info");
        append("/usr/share/info");
        append("/usr/info");
    };
    const auto append_path_derived = [&]() {
        const char* raw_path = std::getenv("PATH");
        if (raw_path == nullptr) {
            return;
        }
        std::string_view path_list(raw_path);
        std::size_t start = 0;
        while (start <= path_list.size()) {
            const auto separator = path_list.find(':', start);
            const auto end = separator == std::string_view::npos ? path_list.size() : separator;
            const auto entry = path_list.substr(start, end - start);
            if (!entry.empty()) {
                const std::filesystem::path bin(entry);
                const auto prefix = bin.parent_path();
                append(prefix / "share/info");
                append(prefix / "info");
            }
            if (separator == std::string_view::npos) {
                break;
            }
            start = separator + 1;
        }
    };

    // GNU Info always has a build-time default documentation directory and can
    // augment it with INFOPATH. These cover the standard Arch/CachyOS layout,
    // common local installs, and explicit user additions without spawning Info
    // merely to reject a command that has no plausible local topic.
    append_defaults();

    const char* raw = std::getenv("INFOPATH");
    if (raw == nullptr) {
        return directories;
    }

    std::string_view path_list(raw);
    std::size_t start = 0;
    while (start <= path_list.size()) {
        const auto separator = path_list.find(':', start);
        const auto end = separator == std::string_view::npos ? path_list.size() : separator;
        const auto entry = path_list.substr(start, end - start);
        if (entry == "PATH") {
            append_path_derived();
        } else if (!entry.empty()) {
            append(std::filesystem::path(entry));
        }
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
    }

    return directories;
}

bool safe_info_basename(std::string_view command) {
    return !command.empty() && command != "." && command != ".." &&
        command.find('/') == std::string_view::npos &&
        command.find('\\') == std::string_view::npos;
}

bool direct_info_file_exists(const std::filesystem::path& directory, std::string_view command) {
    if (!safe_info_basename(command)) {
        return false;
    }

    static constexpr std::string_view suffixes[] = {
        ".info", ".info.gz", ".info.xz", ".info.bz2", ".info.lz", ".info.zst",
    };
    for (const auto suffix : suffixes) {
        std::error_code error;
        const auto path = directory / (std::string(command) + std::string(suffix));
        if (std::filesystem::is_regular_file(path, error) && !error) {
            return true;
        }
    }
    return false;
}

bool equal_ascii_case_insensitive(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(left[i])) !=
            std::tolower(static_cast<unsigned char>(right[i]))) {
            return false;
        }
    }
    return true;
}

bool info_dir_menu_contains(const std::filesystem::path& directory, std::string_view command) {
    std::ifstream menu(directory / "dir");
    if (!menu) {
        return false;
    }

    std::string line;
    while (std::getline(menu, line)) {
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] != '*') {
            continue;
        }
        const auto label_start = line.find_first_not_of(" \t", first + 1);
        if (label_start == std::string::npos) {
            continue;
        }
        const auto colon = line.find(':', label_start);
        if (colon == std::string::npos) {
            continue;
        }
        if (equal_ascii_case_insensitive(trim(line.substr(label_start, colon - label_start)), command)) {
            return true;
        }
    }
    return false;
}

bool plausible_info_topic(std::string_view command) {
    for (const auto& directory : info_directories()) {
        if (direct_info_file_exists(directory, command) || info_dir_menu_contains(directory, command)) {
            return true;
        }
    }
    return false;
}

bool verified_info_topic(const std::string& command) {
    const auto where = system::run_capture_stdout({"info", "--where", command}, 16 * 1024);
    return where.exit_code == 0 && !trim(where.stdout_text).empty();
}

} // namespace

bool InfoGuidanceProvider::available() const {
    return system::find_executable("info").has_value();
}

GuidanceBundle InfoGuidanceProvider::guide(const Candidate& candidate) const {
    GuidanceBundle bundle;

    // Info is guidance-only: verify an exact local topic, but never feed that
    // discovery back into source provenance, recall, ranking, or confidence.
    if (candidate.command.empty() || !candidate.installed || !candidate.cli_capable ||
        !plausible_info_topic(candidate.command) || !verified_info_topic(candidate.command)) {
        return bundle;
    }

    bundle.learning_resources.push_back(LearningResource{
        .label = "Local Info manual",
        .target = "info " + candidate.command,
        .source_kind = GuidanceSourceKind::Info,
        .source_reference = "info:" + candidate.command,
        .verified = true,
        .verified_by = {},
        .verified_on = {},
    });

    // Preserve source priority. If a higher-priority provider (currently man)
    // already found verified examples, Info remains a learning link and avoids
    // formatting an entire manual on the hot path.
    if (!candidate.examples.empty()) {
        return bundle;
    }

    const auto rendered = system::run_capture_stdout(
        {"info", "--output=-", candidate.command},
        512 * 1024
    );
    if (rendered.exit_code != 0 || rendered.stdout_text.empty()) {
        return bundle;
    }

    bundle.examples = extract_examples(candidate.command, rendered.stdout_text);
    return bundle;
}

} // namespace acclorite
