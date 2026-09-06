#include "acclorite/guidance/man_provider.hpp"

#include <algorithm>
#include <cctype>
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

std::string strip_terminal_formatting(std::string_view input) {
    std::string out;
    out.reserve(input.size());

    for (std::size_t i = 0; i < input.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(input[i]);
        if (ch == '\b') {
            if (!out.empty()) {
                out.pop_back();
            }
            continue;
        }
        if (ch == 0x1b && i + 1 < input.size() && input[i + 1] == '[') {
            i += 2;
            while (i < input.size()) {
                const unsigned char c = static_cast<unsigned char>(input[i]);
                if (c >= 0x40 && c <= 0x7e) {
                    break;
                }
                ++i;
            }
            continue;
        }
        if (ch != '\r') {
            out.push_back(static_cast<char>(ch));
        }
    }

    return out;
}

bool uppercase_heading(std::string_view raw_line) {
    if (raw_line.empty() || std::isspace(static_cast<unsigned char>(raw_line.front()))) {
        return false;
    }
    const std::string line = trim(std::string(raw_line));
    if (line.empty() || line.size() > 96) {
        return false;
    }

    bool has_alpha = false;
    for (const unsigned char ch : line) {
        if (std::isalpha(ch)) {
            has_alpha = true;
            if (std::islower(ch)) {
                return false;
            }
        } else if (!(std::isdigit(ch) || std::isspace(ch) || ch == '-' || ch == '/' ||
                     ch == '&' || ch == '_' || ch == '(' || ch == ')' || ch == ':')) {
            return false;
        }
    }
    return has_alpha;
}

bool example_heading(std::string_view raw_line) {
    if (!uppercase_heading(raw_line)) {
        return false;
    }
    std::string line = trim(std::string(raw_line));
    std::ranges::transform(line, line.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return line.find("EXAMPLE") != std::string::npos;
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
        // Keep this conservative: accept `env VAR=x command ...` only when the
        // exact command token is still present after a whitespace boundary.
        const std::string needle = " " + std::string(command);
        const auto pos = line.find(needle);
        if (pos != std::string_view::npos) {
            return token_match(line.substr(pos + 1));
        }
    }
    return false;
}

std::vector<UsageExample> extract_examples(
    const std::string_view command,
    const std::string_view man_text
) {
    std::vector<UsageExample> examples;
    const std::string clean = strip_terminal_formatting(man_text);
    std::istringstream stream(clean);
    std::string line;
    bool in_examples = false;

    while (std::getline(stream, line)) {
        if (example_heading(line)) {
            in_examples = true;
            continue;
        }
        if (in_examples && uppercase_heading(line)) {
            break;
        }
        if (!in_examples) {
            continue;
        }

        std::string candidate = strip_prompt(line);
        if (candidate.empty() || !starts_with_command(candidate, command)) {
            continue;
        }
        if (candidate.size() > 240) {
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
            .source_kind = GuidanceSourceKind::Man,
            .source_reference = "man:" + std::string(command),
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

} // namespace

bool ManGuidanceProvider::available() const {
    return system::find_executable("man").has_value();
}

GuidanceBundle ManGuidanceProvider::guide(const Candidate& candidate) const {
    GuidanceBundle bundle;

    // The ranking/index pipeline already proved a local manual exists. Guidance
    // must not run arbitrary candidate commands merely to discover help syntax.
    if (!source_contains(candidate.source, "man") || candidate.command.empty()) {
        return bundle;
    }

    bundle.learning_resources.push_back(LearningResource{
        .label = "Local manual",
        .target = "man " + candidate.command,
        .source_kind = GuidanceSourceKind::Man,
        .source_reference = "man:" + candidate.command,
        .verified = true,
        .verified_by = {},
        .verified_on = {},
    });

    const auto result = system::run_capture_stdout({"man", "--", candidate.command}, 512 * 1024);
    if (result.exit_code != 0 || result.stdout_text.empty()) {
        return bundle;
    }

    bundle.examples = extract_examples(candidate.command, result.stdout_text);
    return bundle;
}

} // namespace acclorite
