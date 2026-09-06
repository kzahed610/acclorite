#include "acclorite/guidance/tldr_provider.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "acclorite/system/executable.hpp"

namespace acclorite {
namespace {

constexpr std::uintmax_t kMaxPageBytes = 256 * 1024;

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

bool safe_page_basename(std::string_view command) {
    return !command.empty() && command != "." && command != ".." &&
        command.find('/') == std::string_view::npos &&
        command.find('\\') == std::string_view::npos;
}

std::optional<std::filesystem::path> home_directory() {
    const char* raw = std::getenv("HOME");
    if (raw == nullptr || *raw == '\0') {
        return std::nullopt;
    }
    return std::filesystem::path(raw);
}

void append_unique(
    std::vector<std::filesystem::path>& paths,
    std::filesystem::path path
) {
    if (path.empty()) {
        return;
    }
    if (std::ranges::find(paths, path) == paths.end()) {
        paths.push_back(std::move(path));
    }
}

std::filesystem::path expand_home(std::filesystem::path path) {
    const std::string text = path.string();
    if (text == "~") {
        if (const auto home = home_directory()) {
            return *home;
        }
    } else if (text.starts_with("~/")) {
        if (const auto home = home_directory()) {
            return *home / text.substr(2);
        }
    }
    return path;
}

std::string strip_toml_comment(std::string_view line) {
    bool in_single = false;
    bool in_double = false;
    bool escaped = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (in_double && escaped) {
            escaped = false;
            continue;
        }
        if (in_double && ch == '\\') {
            escaped = true;
            continue;
        }
        if (!in_double && ch == '\'') {
            in_single = !in_single;
            continue;
        }
        if (!in_single && ch == '"') {
            in_double = !in_double;
            continue;
        }
        if (!in_single && !in_double && ch == '#') {
            return std::string(line.substr(0, i));
        }
    }
    return std::string(line);
}

std::optional<std::string> parse_toml_string(std::string value) {
    value = trim(std::move(value));
    if (value.size() < 2 || (value.front() != '"' && value.front() != '\'')) {
        return std::nullopt;
    }
    const char quote = value.front();
    std::string result;
    bool escaped = false;

    for (std::size_t i = 1; i < value.size(); ++i) {
        const char ch = value[i];
        if (quote == '"' && escaped) {
            switch (ch) {
                case '\\': result.push_back('\\'); break;
                case '"': result.push_back('"'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                default: return std::nullopt;
            }
            escaped = false;
            continue;
        }
        if (quote == '"' && ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == quote) {
            if (!trim(value.substr(i + 1)).empty()) {
                return std::nullopt;
            }
            return result;
        }
        result.push_back(ch);
    }
    return std::nullopt;
}

std::filesystem::path tealdeer_config_path() {
    if (const char* override_dir = std::getenv("TEALDEER_CONFIG_DIR");
        override_dir != nullptr && *override_dir != '\0') {
        return std::filesystem::path(override_dir) / "config.toml";
    }
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::filesystem::path(xdg) / "tealdeer/config.toml";
    }
    if (const auto home = home_directory()) {
        return *home / ".config/tealdeer/config.toml";
    }
    return {};
}

std::optional<std::filesystem::path> configured_tealdeer_cache() {
    const auto config_path = tealdeer_config_path();
    if (config_path.empty()) {
        return std::nullopt;
    }

    std::ifstream config(config_path);
    if (!config) {
        return std::nullopt;
    }

    bool in_directories = false;
    std::string line;
    while (std::getline(config, line)) {
        line = trim(strip_toml_comment(line));
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            in_directories = trim(line.substr(1, line.size() - 2)) == "directories";
            continue;
        }
        if (!in_directories) {
            continue;
        }

        const auto equals = line.find('=');
        if (equals == std::string::npos || trim(line.substr(0, equals)) != "cache_dir") {
            continue;
        }
        const auto parsed = parse_toml_string(line.substr(equals + 1));
        if (!parsed || parsed->empty()) {
            return std::nullopt;
        }

        std::filesystem::path path = expand_home(std::filesystem::path(*parsed));
        if (path.is_relative()) {
            path = config_path.parent_path() / path;
        }
        return path.lexically_normal();
    }

    return std::nullopt;
}

std::vector<std::filesystem::path> cache_roots() {
    std::vector<std::filesystem::path> roots;

    // Tealdeer. The deprecated environment variable still has precedence in
    // current tealdeer releases; the config-file override is the preferred path.
    if (const char* explicit_cache = std::getenv("TEALDEER_CACHE_DIR");
        explicit_cache != nullptr && *explicit_cache != '\0') {
        append_unique(roots, std::filesystem::path(explicit_cache) / "tldr-pages");
    }
    if (const auto configured = configured_tealdeer_cache()) {
        append_unique(roots, *configured / "tldr-pages");
    }

    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && *xdg != '\0') {
        append_unique(roots, std::filesystem::path(xdg) / "tealdeer/tldr-pages");
        append_unique(roots, std::filesystem::path(xdg) / "tldr");
    } else if (const auto home = home_directory()) {
        append_unique(roots, *home / ".cache/tealdeer/tldr-pages");
        append_unique(roots, *home / ".cache/tldr");
    }

    // The Python client can use a system-provided cache under XDG data dirs.
    const char* raw_data_dirs = std::getenv("XDG_DATA_DIRS");
    const std::string_view data_dirs =
        raw_data_dirs != nullptr && *raw_data_dirs != '\0'
            ? std::string_view(raw_data_dirs)
            : std::string_view("/usr/local/share:/usr/share");
    std::size_t start = 0;
    while (start <= data_dirs.size()) {
        const auto separator = data_dirs.find(':', start);
        const auto end = separator == std::string_view::npos ? data_dirs.size() : separator;
        const auto entry = data_dirs.substr(start, end - start);
        if (!entry.empty()) {
            append_unique(roots, std::filesystem::path(entry) / "tldr");
        }
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
    }

    // The Node client historically uses ~/.tldr. Treat it as another local
    // cache layout; exact page verification below still gates all guidance.
    if (const auto home = home_directory()) {
        append_unique(roots, *home / ".tldr");
    }

    return roots;
}

std::string locale_name(std::string value) {
    if (const auto dot = value.find('.'); dot != std::string::npos) {
        value.resize(dot);
    }
    if (const auto modifier = value.find('@'); modifier != std::string::npos) {
        value.resize(modifier);
    }
    if (value == "C" || value == "POSIX") {
        return {};
    }
    return value;
}

void append_language(std::vector<std::string>& languages, std::string language) {
    language = locale_name(std::move(language));
    if (language.empty()) {
        return;
    }
    if (std::ranges::find(languages, language) == languages.end()) {
        languages.push_back(language);
    }
    const auto underscore = language.find('_');
    const std::string base = language.substr(0, underscore);
    if (!base.empty() && std::ranges::find(languages, base) == languages.end()) {
        languages.push_back(base);
    }
}

std::vector<std::string> language_directories() {
    std::vector<std::string> languages;
    if (const char* raw = std::getenv("LANGUAGE"); raw != nullptr && *raw != '\0') {
        std::string_view list(raw);
        std::size_t start = 0;
        while (start <= list.size()) {
            const auto separator = list.find(':', start);
            const auto end = separator == std::string_view::npos ? list.size() : separator;
            append_language(languages, std::string(list.substr(start, end - start)));
            if (separator == std::string_view::npos) {
                break;
            }
            start = separator + 1;
        }
    }
    if (const char* raw = std::getenv("LANG"); raw != nullptr && *raw != '\0') {
        append_language(languages, raw);
    }
    append_language(languages, "en");

    std::vector<std::string> directories;
    for (const auto& language : languages) {
        const std::string directory = "pages." + language;
        if (std::ranges::find(directories, directory) == directories.end()) {
            directories.push_back(directory);
        }
        if (language == "en" && std::ranges::find(directories, std::string("pages")) == directories.end()) {
            // tldr-python and repository-style caches use `pages/` for English;
            // tealdeer uses `pages.en/`.
            directories.emplace_back("pages");
        }
    }
    return directories;
}

bool page_heading_matches(std::string_view command, std::string_view text) {
    std::istringstream stream{std::string(text)};
    std::string line;
    bool first_line = true;
    while (std::getline(stream, line)) {
        if (first_line && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xef &&
            static_cast<unsigned char>(line[1]) == 0xbb &&
            static_cast<unsigned char>(line[2]) == 0xbf) {
            line.erase(0, 3);
        }
        first_line = false;
        line = trim(std::move(line));
        if (line.empty()) {
            continue;
        }
        if (!line.starts_with("# ")) {
            return false;
        }
        return equal_ascii_case_insensitive(trim(line.substr(2)), command);
    }
    return false;
}

struct LocalPage {
    std::filesystem::path path;
    std::string text;
};

std::optional<LocalPage> read_verified_page(const std::filesystem::path& path, std::string_view command) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return std::nullopt;
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > kMaxPageBytes) {
        return std::nullopt;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (!page_heading_matches(command, text)) {
        return std::nullopt;
    }
    return LocalPage{.path = path, .text = std::move(text)};
}

std::optional<LocalPage> find_local_page(std::string_view command) {
    if (!safe_page_basename(command)) {
        return std::nullopt;
    }

    const std::string filename = lowercase(std::string(command)) + ".md";
    static constexpr std::string_view platforms[] = {"linux", "common"};
    const auto languages = language_directories();

    for (const auto& root : cache_roots()) {
        for (const auto& language : languages) {
            for (const auto platform : platforms) {
                if (auto page = read_verified_page(root / language / platform / filename, command)) {
                    return page;
                }
            }
        }
    }
    return std::nullopt;
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
    if (line.starts_with("doas ")) {
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

std::vector<UsageExample> extract_examples(
    std::string_view command,
    const LocalPage& page
) {
    std::vector<UsageExample> examples;
    std::istringstream stream(page.text);
    std::string line;

    while (std::getline(stream, line)) {
        line = trim(std::move(line));
        if (line.size() < 3 || !line.starts_with('`') || !line.ends_with('`') ||
            line.starts_with("```")) {
            continue;
        }

        std::string candidate = strip_prompt(line.substr(1, line.size() - 2));
        if (candidate.empty() || candidate.size() > 240 || !starts_with_command(candidate, command)) {
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
            .source_kind = GuidanceSourceKind::Tldr,
            .source_reference = "tldr:" + page.path.string(),
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

bool any_cache_root_exists() {
    for (const auto& root : cache_roots()) {
        std::error_code error;
        if (std::filesystem::is_directory(root, error) && !error) {
            return true;
        }
    }
    return false;
}

} // namespace

bool TldrGuidanceProvider::available() const {
    return any_cache_root_exists();
}

GuidanceBundle TldrGuidanceProvider::guide(const Candidate& candidate) const {
    GuidanceBundle bundle;
    if (candidate.command.empty() || !candidate.cli_capable) {
        return bundle;
    }

    const auto page = find_local_page(candidate.command);
    if (!page) {
        return bundle;
    }

    const auto tldr = system::find_executable("tldr");
    bundle.learning_resources.push_back(LearningResource{
        .label = "Local TLDR page",
        .target = tldr ? "tldr " + candidate.command : page->path.string(),
        .source_kind = GuidanceSourceKind::Tldr,
        .source_reference = "tldr:" + page->path.string(),
        .verified = true,
        .verified_by = {},
        .verified_on = {},
    });

    // TLDR is lower priority than man and Info. An exact cached page remains a
    // learning resource, but do not duplicate examples already verified by a
    // higher-priority local documentation provider.
    if (!candidate.examples.empty()) {
        return bundle;
    }

    bundle.examples = extract_examples(candidate.command, *page);
    return bundle;
}

} // namespace acclorite
