#include "acclorite/syntax/fish_completion_provider.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace acclorite {
namespace {

constexpr std::uintmax_t kMaxCompletionBytes = 2 * 1024 * 1024;

std::string trim(std::string value) {
    const auto not_space = [](const unsigned char ch) { return !std::isspace(ch); };
    const auto first = std::ranges::find_if(value, not_space);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if(value.rbegin(), value.rend(), not_space).base();
    return std::string(first, last);
}

bool safe_command_basename(std::string_view command) {
    if (command.empty() || command == "." || command == ".." || command.size() > 192 ||
        command.find('/') != std::string_view::npos || command.find('\\') != std::string_view::npos) {
        return false;
    }
    return std::ranges::all_of(command, [](const unsigned char ch) {
        return std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '+';
    });
}

void append_unique(std::vector<std::filesystem::path>& roots, std::filesystem::path root) {
    if (root.empty()) {
        return;
    }
    root = root.lexically_normal();
    if (std::ranges::find(roots, root) == roots.end()) {
        roots.push_back(std::move(root));
    }
}

void append_colon_roots(
    std::vector<std::filesystem::path>& roots,
    std::string_view list,
    std::string_view suffix
) {
    std::size_t start = 0;
    while (start <= list.size()) {
        const auto separator = list.find(':', start);
        const auto end = separator == std::string_view::npos ? list.size() : separator;
        const auto entry = list.substr(start, end - start);
        if (!entry.empty()) {
            append_unique(roots, std::filesystem::path(entry) / suffix);
        }
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
    }
}

std::vector<std::filesystem::path> default_completion_roots() {
    std::vector<std::filesystem::path> roots;

    // The first completion-syntax slice trusts only system/admin completion
    // roots by default. User completions can be supported later with an
    // explicit trust/precedence policy; the parser itself remains static-only.
    const char* raw_config_dirs = std::getenv("XDG_CONFIG_DIRS");
    append_colon_roots(
        roots,
        raw_config_dirs != nullptr && *raw_config_dirs != '\0'
            ? std::string_view(raw_config_dirs)
            : std::string_view("/etc/xdg"),
        "fish/completions"
    );
    append_unique(roots, "/etc/fish/completions");

    const char* raw_data_dirs = std::getenv("XDG_DATA_DIRS");
    const std::string_view data_dirs =
        raw_data_dirs != nullptr && *raw_data_dirs != '\0'
            ? std::string_view(raw_data_dirs)
            : std::string_view("/usr/local/share:/usr/share");
    append_colon_roots(roots, data_dirs, "fish/vendor_completions.d");
    append_colon_roots(roots, data_dirs, "fish/completions");

    return roots;
}

struct FishToken {
    std::string value;
};

std::optional<std::vector<FishToken>> tokenize_static_fish_line(std::string_view line) {
    std::vector<FishToken> tokens;
    std::string current;
    bool in_single = false;
    bool in_double = false;
    bool escaped = false;

    const auto flush = [&]() {
        if (!current.empty()) {
            tokens.push_back(FishToken{.value = std::move(current)});
            current.clear();
        }
    };

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];

        if (escaped) {
            current.push_back(ch);
            escaped = false;
            continue;
        }

        if (in_single) {
            if (ch == '\\' && i + 1 < line.size() && (line[i + 1] == '\\' || line[i + 1] == '\'')) {
                escaped = true;
                continue;
            }
            if (ch == '\'') {
                in_single = false;
                continue;
            }
            current.push_back(ch);
            continue;
        }

        if (in_double) {
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            // Fish expands variables and command substitutions inside double
            // quotes. Static grammar parsing must not interpret either form.
            if (ch == '$' || ch == '(' || ch == ')') {
                return std::nullopt;
            }
            if (ch == '"') {
                in_double = false;
                continue;
            }
            current.push_back(ch);
            continue;
        }

        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '\'') {
            in_single = true;
            continue;
        }
        if (ch == '"') {
            in_double = true;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch))) {
            flush();
            continue;
        }
        if (ch == '#' && current.empty()) {
            break;
        }
        // These are shell syntax outside quotes. Reject the declaration rather
        // than accidentally interpreting executable Fish code as static data.
        if (ch == '$' || ch == '(' || ch == ')' || ch == ';' || ch == '|' ||
            ch == '&' || ch == '<' || ch == '>') {
            return std::nullopt;
        }
        current.push_back(ch);
    }

    if (escaped || in_single || in_double) {
        return std::nullopt;
    }
    flush();
    return tokens;
}

bool safe_short_option(std::string_view value) {
    if (value.size() != 1) {
        return false;
    }
    const unsigned char ch = static_cast<unsigned char>(value.front());
    return !std::isspace(ch) && ch != '-';
}

bool safe_long_option(std::string_view value) {
    return !value.empty() && value.size() <= 96 &&
        std::ranges::all_of(value, [](const unsigned char ch) {
            return std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '?';
        });
}

bool dynamic_argument_expression(std::string_view value) {
    return value.find('$') != std::string_view::npos ||
        value.find('(') != std::string_view::npos ||
        value.find(')') != std::string_view::npos ||
        value.find(';') != std::string_view::npos ||
        value.find('|') != std::string_view::npos ||
        value.find('&') != std::string_view::npos;
}

enum class FishConditionKind {
    None,
    UseSubcommand,
    SeenSubcommands,
};

struct FishCondition {
    FishConditionKind kind{FishConditionKind::None};
    std::vector<std::string> subcommands;
};

struct StaticCompleteDeclaration {
    std::string command;
    std::vector<std::string> short_names;
    std::vector<std::string> long_names;
    std::string description;
    bool requires_parameter{false};
    FishCondition condition;
    std::vector<std::string> argument_expressions;
};

std::optional<FishCondition> parse_static_condition(std::string_view value) {
    const std::string stripped = trim(std::string(value));
    if (stripped == "__fish_use_subcommand") {
        return FishCondition{.kind = FishConditionKind::UseSubcommand, .subcommands = {}};
    }

    std::istringstream stream(stripped);
    std::string function;
    stream >> function;
    if (function != "__fish_seen_subcommand_from") {
        return std::nullopt;
    }

    FishCondition condition{.kind = FishConditionKind::SeenSubcommands, .subcommands = {}};
    std::string subcommand;
    while (stream >> subcommand) {
        if (!safe_command_basename(subcommand) || subcommand.starts_with('-')) {
            return std::nullopt;
        }
        if (std::ranges::find(condition.subcommands, subcommand) == condition.subcommands.end()) {
            condition.subcommands.push_back(std::move(subcommand));
        }
    }
    if (condition.subcommands.empty()) {
        return std::nullopt;
    }
    return condition;
}

bool append_short_option_name(StaticCompleteDeclaration& declaration, std::string value) {
    if (!safe_short_option(value)) {
        return false;
    }
    if (std::ranges::find(declaration.short_names, value) == declaration.short_names.end()) {
        declaration.short_names.push_back(std::move(value));
    }
    return true;
}

bool append_long_option_name(StaticCompleteDeclaration& declaration, std::string value) {
    if (!safe_long_option(value)) {
        return false;
    }
    if (std::ranges::find(declaration.long_names, value) == declaration.long_names.end()) {
        declaration.long_names.push_back(std::move(value));
    }
    return true;
}

std::optional<StaticCompleteDeclaration> parse_static_complete_declaration(
    const std::vector<FishToken>& tokens,
    std::string_view expected_command
) {
    if (tokens.empty() || tokens.front().value != "complete") {
        return std::nullopt;
    }

    StaticCompleteDeclaration declaration;
    bool saw_condition = false;

    const auto take_value = [&](std::size_t& index) -> std::optional<std::string> {
        if (index + 1 >= tokens.size()) {
            return std::nullopt;
        }
        ++index;
        return tokens[index].value;
    };

    const auto apply_short_switch = [&](char flag, std::size_t& index) -> bool {
        switch (flag) {
            case 'r':
            case 'x':
                declaration.requires_parameter = true;
                return true;
            case 'f':
            case 'F':
            case 'k':
                // Completion UI behavior only.
                return true;
            case 'c': {
                auto value = take_value(index);
                if (!value) return false;
                declaration.command = std::move(*value);
                return true;
            }
            case 's': {
                auto value = take_value(index);
                return value && append_short_option_name(declaration, std::move(*value));
            }
            case 'l': {
                auto value = take_value(index);
                return value && append_long_option_name(declaration, std::move(*value));
            }
            case 'd': {
                auto value = take_value(index);
                if (!value) return false;
                declaration.description = std::move(*value);
                return true;
            }
            case 'n': {
                auto value = take_value(index);
                if (!value || saw_condition) return false;
                auto condition = parse_static_condition(*value);
                if (!condition) return false;
                declaration.condition = std::move(*condition);
                saw_condition = true;
                return true;
            }
            case 'a': {
                auto value = take_value(index);
                if (!value || dynamic_argument_expression(*value)) return false;
                declaration.argument_expressions.push_back(std::move(*value));
                return true;
            }
            default:
                return false;
        }
    };

    for (std::size_t i = 1; i < tokens.size(); ++i) {
        const std::string& token = tokens[i].value;
        if (token == "-c" || token == "--command") {
            auto value = take_value(i);
            if (!value) return std::nullopt;
            declaration.command = std::move(*value);
        } else if (token.starts_with("--command=")) {
            declaration.command = token.substr(std::string("--command=").size());
        } else if (token == "-s" || token == "--short-option") {
            auto value = take_value(i);
            if (!value || !append_short_option_name(declaration, std::move(*value))) return std::nullopt;
        } else if (token.starts_with("--short-option=")) {
            if (!append_short_option_name(
                    declaration,
                    token.substr(std::string("--short-option=").size())
                )) {
                return std::nullopt;
            }
        } else if (token == "-l" || token == "--long-option") {
            auto value = take_value(i);
            if (!value || !append_long_option_name(declaration, std::move(*value))) return std::nullopt;
        } else if (token.starts_with("--long-option=")) {
            if (!append_long_option_name(
                    declaration,
                    token.substr(std::string("--long-option=").size())
                )) {
                return std::nullopt;
            }
        } else if (token == "-d" || token == "--description") {
            auto value = take_value(i);
            if (!value) return std::nullopt;
            declaration.description = std::move(*value);
        } else if (token.starts_with("--description=")) {
            declaration.description = token.substr(std::string("--description=").size());
        } else if (token == "-r" || token == "--require-parameter" ||
                   token == "-x" || token == "--exclusive") {
            declaration.requires_parameter = true;
        } else if (token == "-n" || token == "--condition") {
            auto value = take_value(i);
            if (!value || saw_condition) return std::nullopt;
            auto condition = parse_static_condition(*value);
            if (!condition) return std::nullopt;
            declaration.condition = std::move(*condition);
            saw_condition = true;
        } else if (token.starts_with("--condition=")) {
            if (saw_condition) return std::nullopt;
            auto condition = parse_static_condition(
                token.substr(std::string("--condition=").size())
            );
            if (!condition) return std::nullopt;
            declaration.condition = std::move(*condition);
            saw_condition = true;
        } else if (token == "-a" || token == "--arguments") {
            auto value = take_value(i);
            if (!value || dynamic_argument_expression(*value)) return std::nullopt;
            declaration.argument_expressions.push_back(std::move(*value));
        } else if (token.starts_with("--arguments=")) {
            std::string value = token.substr(std::string("--arguments=").size());
            if (dynamic_argument_expression(value)) {
                return std::nullopt;
            }
            declaration.argument_expressions.push_back(std::move(value));
        } else if (token == "-f" || token == "--no-files" ||
                   token == "-F" || token == "--force-files" ||
                   token == "-k" || token == "--keep-order") {
            // Completion UI behavior, not command grammar.
            continue;
        } else if (token.size() > 2 && token.front() == '-' && token[1] != '-') {
            // Fish accepts compact short switches such as `-xs c` and `-xa ARG`.
            // Interpret only our tiny known switch set, and require any
            // argument-taking switch to be the final character in the cluster.
            for (std::size_t cluster_index = 1; cluster_index < token.size(); ++cluster_index) {
                const char flag = token[cluster_index];
                const bool takes_switch_argument =
                    flag == 'c' || flag == 's' || flag == 'l' ||
                    flag == 'd' || flag == 'n' || flag == 'a';
                if (takes_switch_argument && cluster_index + 1 != token.size()) {
                    return std::nullopt;
                }
                if (!apply_short_switch(flag, i)) {
                    return std::nullopt;
                }
                if (takes_switch_argument) {
                    break;
                }
            }
        } else {
            // Unknown Fish complete syntax is not proof we understand the line.
            return std::nullopt;
        }
    }

    if (declaration.command != expected_command) {
        return std::nullopt;
    }
    return declaration;
}

std::optional<CommandOption> option_from_declaration(
    const StaticCompleteDeclaration& declaration,
    const std::filesystem::path& source,
    std::string section
) {
    if (declaration.short_names.empty() && declaration.long_names.empty()) {
        return std::nullopt;
    }

    std::vector<std::string> names;
    names.reserve(declaration.short_names.size() + declaration.long_names.size());
    for (const auto& short_name : declaration.short_names) {
        names.push_back("-" + short_name);
    }
    for (const auto& long_name : declaration.long_names) {
        names.push_back("--" + long_name);
    }

    return CommandOption{
        .names = std::move(names),
        .description = declaration.description,
        .value_name = declaration.requires_parameter ? std::optional<std::string>("VALUE") : std::nullopt,
        .value_shape_known = declaration.requires_parameter,
        .takes_value = declaration.requires_parameter,
        .value_required = declaration.requires_parameter,
        .provenance = SyntaxProvenance{
            .source_kind = SyntaxSourceKind::Completion,
            .source_reference = "fish:" + source.string(),
            .section = std::move(section),
        },
    };
}

std::optional<std::vector<std::string>> static_subcommand_candidates(
    const StaticCompleteDeclaration& declaration
) {
    if (declaration.condition.kind != FishConditionKind::UseSubcommand ||
        !declaration.short_names.empty() || !declaration.long_names.empty() ||
        declaration.argument_expressions.empty()) {
        return std::nullopt;
    }

    std::vector<std::string> result;
    for (const auto& expression : declaration.argument_expressions) {
        std::istringstream stream(expression);
        std::string candidate;
        bool saw_candidate = false;
        while (stream >> candidate) {
            saw_candidate = true;
            if (!safe_command_basename(candidate) || candidate.starts_with('-')) {
                return std::nullopt;
            }
            if (std::ranges::find(result, candidate) == result.end()) {
                result.push_back(std::move(candidate));
            }
        }
        if (!saw_candidate) {
            return std::nullopt;
        }
    }
    if (result.empty()) {
        return std::nullopt;
    }
    return result;
}

bool same_option_identity(const CommandOption& left, const CommandOption& right) {
    for (const auto& left_name : left.names) {
        if (std::ranges::find(right.names, left_name) != right.names.end()) {
            return true;
        }
    }
    return false;
}

std::optional<std::filesystem::path> exact_completion_file(
    const std::vector<std::filesystem::path>& roots,
    std::string_view command
) {
    if (!safe_command_basename(command)) {
        return std::nullopt;
    }
    for (const auto& root : roots) {
        std::error_code ec;
        const auto path = root / (std::string(command) + ".fish");
        if (!std::filesystem::is_regular_file(path, ec) || ec) {
            continue;
        }
        const auto size = std::filesystem::file_size(path, ec);
        if (ec || size > kMaxCompletionBytes) {
            continue;
        }
        return path;
    }
    return std::nullopt;
}

std::optional<std::vector<StaticCompleteDeclaration>> read_static_declarations(
    const std::filesystem::path& path,
    std::string_view command
) {
    std::ifstream input(path);
    if (!input) {
        return std::nullopt;
    }

    std::vector<StaticCompleteDeclaration> declarations;
    std::string line;
    while (std::getline(input, line)) {
        if (line.size() > 64 * 1024) {
            continue;
        }
        const std::string stripped = trim(line);
        if (stripped.empty() || stripped.front() == '#') {
            continue;
        }
        // Multi-line Fish statements require shell parsing and are outside the
        // static subset. Never join or evaluate them.
        if (stripped.ends_with('\\')) {
            continue;
        }
        const auto tokens = tokenize_static_fish_line(stripped);
        if (!tokens) {
            continue;
        }
        auto declaration = parse_static_complete_declaration(*tokens, command);
        if (declaration) {
            declarations.push_back(std::move(*declaration));
        }
    }
    return declarations;
}

} // namespace

FishCompletionSyntaxProvider::FishCompletionSyntaxProvider()
    : roots_(default_completion_roots()) {}

FishCompletionSyntaxProvider::FishCompletionSyntaxProvider(std::vector<std::filesystem::path> roots)
    : roots_(std::move(roots)) {}

bool FishCompletionSyntaxProvider::available() const {
    return std::ranges::any_of(roots_, [](const std::filesystem::path& root) {
        std::error_code ec;
        return std::filesystem::is_directory(root, ec) && !ec;
    });
}

std::optional<CommandGrammar> FishCompletionSyntaxProvider::grammar(const Candidate& candidate) const {
    const auto path = exact_completion_file(roots_, candidate.command);
    if (!path) {
        return std::nullopt;
    }

    const auto declarations = read_static_declarations(*path, candidate.command);
    if (!declarations) {
        return std::nullopt;
    }

    CommandGrammar grammar;
    grammar.command = candidate.command;

    // First prove literal subcommands. We intentionally do not interpret Fish
    // variables, command substitutions, chained conditions, or arbitrary helper
    // code. `__fish_use_subcommand` plus literal `-a/--arguments` data is the
    // only accepted subcommand declaration shape in this slice.
    for (const auto& declaration : *declarations) {
        const auto subcommands = static_subcommand_candidates(declaration);
        if (!subcommands) {
            continue;
        }
        for (const auto& name : *subcommands) {
            const auto duplicate = std::ranges::find_if(grammar.subcommands, [&](const SubcommandSpec& existing) {
                return existing.name == name;
            });
            if (duplicate != grammar.subcommands.end()) {
                continue;
            }
            grammar.subcommands.push_back(SubcommandSpec{
                .name = name,
                .description = declaration.description,
                .options = {},
                .positionals = {},
                .provenance = SyntaxProvenance{
                    .source_kind = SyntaxSourceKind::Completion,
                    .source_reference = "fish:" + path->string(),
                    .section = "complete / __fish_use_subcommand",
                },
                .synopsis = {},
            });
        }
    }

    // Then accept unconditional options as global grammar and positive
    // `__fish_seen_subcommand_from <literal...>` options only for subcommands
    // already proven above. This prevents a condition string from inventing a
    // command hierarchy on its own.
    for (const auto& declaration : *declarations) {
        if (declaration.short_names.empty() && declaration.long_names.empty()) {
            continue;
        }

        if (declaration.condition.kind == FishConditionKind::None) {
            auto option = option_from_declaration(declaration, *path, "complete");
            if (!option) {
                continue;
            }
            const auto duplicate = std::ranges::find_if(grammar.global_options, [&](const CommandOption& existing) {
                return same_option_identity(existing, *option);
            });
            if (duplicate == grammar.global_options.end()) {
                grammar.global_options.push_back(std::move(*option));
            }
            continue;
        }

        if (declaration.condition.kind != FishConditionKind::SeenSubcommands) {
            continue;
        }
        for (const auto& scoped_name : declaration.condition.subcommands) {
            auto subcommand = std::ranges::find_if(grammar.subcommands, [&](const SubcommandSpec& existing) {
                return existing.name == scoped_name;
            });
            if (subcommand == grammar.subcommands.end()) {
                continue;
            }
            auto option = option_from_declaration(
                declaration,
                *path,
                "complete / __fish_seen_subcommand_from"
            );
            if (!option) {
                continue;
            }
            const auto duplicate = std::ranges::find_if(subcommand->options, [&](const CommandOption& existing) {
                return same_option_identity(existing, *option);
            });
            if (duplicate == subcommand->options.end()) {
                subcommand->options.push_back(std::move(*option));
            }
        }
    }

    if (grammar.global_options.empty() && grammar.subcommands.empty()) {
        return std::nullopt;
    }
    return grammar;
}

std::optional<SubcommandSpec> FishCompletionSyntaxProvider::subcommand_grammar(
    const Candidate& candidate,
    const SubcommandSpec& subcommand
) const {
    if (candidate.command.empty() || subcommand.name.empty() ||
        !safe_command_basename(subcommand.name) || subcommand.name.starts_with('-')) {
        return std::nullopt;
    }

    const auto path = exact_completion_file(roots_, candidate.command);
    if (!path) {
        return std::nullopt;
    }
    const auto declarations = read_static_declarations(*path, candidate.command);
    if (!declarations) {
        return std::nullopt;
    }

    // The parent subcommand may have been proven by a higher-priority syntax
    // provider (for example man). Fish conditions are then allowed to add
    // syntax scoped to that already-proven identity. The condition itself still
    // cannot create a subcommand: SearchEngine only calls this resolver with a
    // root grammar fact that some trusted provider already accepted.
    SubcommandSpec enriched = subcommand;
    bool proved_scoped_grammar = false;
    for (const auto& declaration : *declarations) {
        if ((declaration.short_names.empty() && declaration.long_names.empty()) ||
            declaration.condition.kind != FishConditionKind::SeenSubcommands ||
            std::ranges::find(declaration.condition.subcommands, subcommand.name) ==
                declaration.condition.subcommands.end()) {
            continue;
        }

        auto option = option_from_declaration(
            declaration,
            *path,
            "complete / __fish_seen_subcommand_from"
        );
        if (!option) {
            continue;
        }
        proved_scoped_grammar = true;
        const auto duplicate = std::ranges::find_if(enriched.options, [&](const CommandOption& existing) {
            return same_option_identity(existing, *option);
        });
        if (duplicate == enriched.options.end()) {
            enriched.options.push_back(std::move(*option));
        }
    }

    return proved_scoped_grammar
        ? std::optional<SubcommandSpec>(std::move(enriched))
        : std::nullopt;
}

} // namespace acclorite
