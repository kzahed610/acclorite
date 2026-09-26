#include "acclorite/syntax/man_provider.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
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

std::size_t indentation(std::string_view line) {
    std::size_t count = 0;
    while (count < line.size() && (line[count] == ' ' || line[count] == '\t')) {
        ++count;
    }
    return count;
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

std::string heading_name(std::string_view raw_line) {
    std::string heading = trim(std::string(raw_line));
    std::ranges::transform(heading, heading.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return heading;
}

bool options_heading(std::string_view heading) {
    return heading == "OPTIONS" || heading.ends_with(" OPTIONS");
}

bool option_like_heading(std::string_view heading) {
    // Some command families document executable grammar outside a literal
    // OPTIONS section. GNU find, for example, places predicates/actions under
    // TESTS/ACTIONS/OPERATORS. These still use the same conservative
    // dash-prefixed declaration shape and carry their original provenance.
    return options_heading(heading) ||
           heading == "TESTS" ||
           heading == "ACTIONS" ||
           heading == "OPERATORS" ||
           heading == "EXPRESSION" ||
           heading == "EXPRESSIONS";
}

std::optional<std::string> indented_expression_subheading(
    std::string_view raw_line,
    std::string_view current_section
) {
    // GNU find renders TESTS/ACTIONS/OPERATORS and the option groups inside
    // EXPRESSION as indented subsection headings. The generic top-level
    // heading detector intentionally rejects indented text, so recognize only
    // this small, explicit family while already inside find-style expression
    // documentation. Ordinary indented uppercase prose remains prose.
    const bool in_expression_family = current_section == "EXPRESSION" ||
        current_section == "EXPRESSIONS" ||
        current_section == "POSITIONAL OPTIONS" ||
        current_section == "GLOBAL OPTIONS" ||
        current_section == "TESTS" ||
        current_section == "ACTIONS" ||
        current_section == "OPERATORS";
    if (!in_expression_family || raw_line.empty() ||
        !std::isspace(static_cast<unsigned char>(raw_line.front()))) {
        return std::nullopt;
    }

    const std::string heading = heading_name(raw_line);
    if (heading == "POSITIONAL OPTIONS" || heading == "GLOBAL OPTIONS" ||
        heading == "TESTS" || heading == "ACTIONS" || heading == "OPERATORS") {
        return heading;
    }
    return std::nullopt;
}

bool commands_heading(std::string_view heading) {
    // Man pages vary between COMMANDS, SUBCOMMANDS, GIT COMMANDS, and grouped
    // headings such as HIGH-LEVEL COMMANDS (PORCELAIN). Accept the structural
    // noun but explicitly reject option-related sections. Individual entries
    // still have to pass the conservative declaration parser below.
    return (heading.find("COMMANDS") != std::string_view::npos ||
            heading.find("SUBCOMMANDS") != std::string_view::npos) &&
           heading.find("OPTION") == std::string_view::npos;
}

bool option_name_char(const unsigned char ch) {
    return std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '?';
}

std::string normalize_value_name(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2 &&
        ((value.front() == '<' && value.back() == '>') ||
         (value.front() == '[' && value.back() == ']'))) {
        value = trim(value.substr(1, value.size() - 2));
        if (!value.empty() && value.front() == '=') {
            value.erase(value.begin());
        }
    }
    return value;
}

bool compact_value_signature(std::string_view text) {
    if (text.empty() || text.size() > 72 || text.find("  ") != std::string_view::npos) {
        return false;
    }

    std::size_t words = 0;
    bool in_word = false;
    std::size_t second_word_start = std::string_view::npos;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (std::isspace(ch)) {
            in_word = false;
            continue;
        }
        if (!in_word) {
            ++words;
            if (words == 2) {
                second_word_start = i;
            }
            in_word = true;
        }
        if (!(std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.' || ch == '<' || ch == '>' ||
              ch == '[' || ch == ']' || ch == '(' || ch == ')' || ch == '/' || ch == '|' ||
              ch == '=' || ch == ',' || ch == ':')) {
            return false;
        }
    }

    if (words == 1) {
        return true;
    }
    if (words == 2 && second_word_start != std::string_view::npos) {
        // Admit signatures such as `position (input/output)` while rejecting
        // ordinary two-word prose like `enable feature` as unproven grammar.
        return text[second_word_start] == '(' && text.back() == ')';
    }
    return false;
}

struct ParsedOptionLine {
    CommandOption option;
    std::size_t declaration_indent{0};
};

std::optional<ParsedOptionLine> parse_option_line(
    std::string_view raw_line,
    const std::string& command,
    const std::string& section
) {
    const std::size_t indent = indentation(raw_line);
    const std::string line = trim(std::string(raw_line));
    if (line.size() < 2 || line.front() != '-') {
        return std::nullopt;
    }

    std::vector<std::string> names;
    std::optional<std::string> attached_value;
    bool attached_value_required = false;
    std::size_t cursor = 0;
    std::size_t whitespace_after_aliases = 0;

    while (cursor < line.size()) {
        while (cursor < line.size() && (line[cursor] == ',' || std::isspace(static_cast<unsigned char>(line[cursor])))) {
            ++cursor;
        }
        if (cursor >= line.size() || line[cursor] != '-') {
            break;
        }

        const std::size_t start = cursor;
        ++cursor;
        if (cursor < line.size() && line[cursor] == '-') {
            ++cursor;
        }
        const std::size_t name_body_start = cursor;
        while (cursor < line.size() && option_name_char(static_cast<unsigned char>(line[cursor]))) {
            ++cursor;
        }
        if (cursor == name_body_start) {
            return std::nullopt;
        }

        std::string name = line.substr(start, cursor - start);
        if (name == "-" || name == "--") {
            return std::nullopt;
        }
        names.push_back(std::move(name));

        if (cursor < line.size() && line[cursor] == '=') {
            const std::size_t value_start = ++cursor;
            while (cursor < line.size() && !std::isspace(static_cast<unsigned char>(line[cursor])) && line[cursor] != ',') {
                ++cursor;
            }
            if (cursor > value_start) {
                attached_value = normalize_value_name(line.substr(value_start, cursor - value_start));
                attached_value_required = true;
            }
        } else if (cursor < line.size() && line[cursor] == '[') {
            const auto close = line.find(']', cursor + 1);
            if (close != std::string::npos) {
                std::string content = line.substr(cursor + 1, close - cursor - 1);
                if (!content.empty() && content.front() == '=') {
                    content.erase(content.begin());
                }
                if (!content.empty()) {
                    attached_value = normalize_value_name(std::move(content));
                    attached_value_required = false;
                }
                cursor = close + 1;
            }
        }

        const std::size_t before_space = cursor;
        while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor]))) {
            ++cursor;
        }
        whitespace_after_aliases = cursor - before_space;

        if (cursor < line.size() && line[cursor] == ',') {
            ++cursor;
            continue;
        }

        // Some man pages repeat the same whitespace-separated value signature
        // for every alias, e.g. Git 2.55 renders
        // `-c <new-branch>, --create <new-branch>`. Treat the compact value
        // before a comma as part of the alias declaration only when the comma
        // is followed by another option spelling. This keeps ordinary prose
        // containing commas out of the grammar path.
        if (cursor < line.size() && line[cursor] != '-') {
            const auto comma = line.find(',', cursor);
            if (comma != std::string::npos) {
                std::size_t next_alias = comma + 1;
                while (next_alias < line.size() &&
                       std::isspace(static_cast<unsigned char>(line[next_alias]))) {
                    ++next_alias;
                }
                const std::string repeated_value = trim(line.substr(cursor, comma - cursor));
                if (next_alias < line.size() && line[next_alias] == '-' &&
                    compact_value_signature(repeated_value)) {
                    const bool optional_value = repeated_value.size() >= 2 &&
                        repeated_value.front() == '[' && repeated_value.back() == ']';
                    const std::string normalized_value = normalize_value_name(repeated_value);
                    if (normalized_value.empty()) {
                        return std::nullopt;
                    }
                    if (attached_value &&
                        (*attached_value != normalized_value ||
                         attached_value_required == optional_value)) {
                        // Aliases that disagree about their value grammar are
                        // not safe to collapse into one option fact.
                        return std::nullopt;
                    }
                    attached_value = normalized_value;
                    attached_value_required = !optional_value;
                    cursor = next_alias;
                    continue;
                }
            }
        }

        if (cursor < line.size() && line[cursor] == '-') {
            continue;
        }
        break;
    }

    if (names.empty()) {
        return std::nullopt;
    }

    // Wrapped prose in expression-oriented manuals can begin with a list of
    // option names without being a declaration. GNU find, for example, wraps
    // `... -size, -uid and -used) as` onto a line that otherwise looks like a
    // multi-alias option. Treat a conjunction immediately after two or more
    // parsed names as prose rather than inventing a synthetic `-size, -uid`
    // grammar item.
    if (names.size() >= 2 && cursor < line.size()) {
        const std::string tail = trim(line.substr(cursor));
        if (tail.starts_with("and ") || tail == "and" ||
            tail.starts_with("or ") || tail == "or") {
            return std::nullopt;
        }
    }

    CommandOption option{
        .names = std::move(names),
        .description = {},
        .value_name = attached_value,
        .takes_value = attached_value.has_value(),
        .value_required = attached_value.has_value() && attached_value_required,
        .provenance = SyntaxProvenance{
            .source_kind = SyntaxSourceKind::Man,
            .source_reference = "man:" + command,
            .section = section,
        },
    };

    if (cursor < line.size()) {
        std::string remainder = trim(line.substr(cursor));
        if (!remainder.empty()) {
            if (!option.takes_value && whitespace_after_aliases <= 1 && compact_value_signature(remainder)) {
                const bool optional_value = remainder.size() >= 2 &&
                    remainder.front() == '[' && remainder.back() == ']';
                option.value_name = normalize_value_name(std::move(remainder));
                option.takes_value = true;
                option.value_required = !optional_value;
            } else if (option.takes_value && whitespace_after_aliases <= 1 &&
                       compact_value_signature(remainder)) {
                // The final alias may repeat a value signature already proven
                // by an earlier alias: `-c <name>, --create <name>`. Consume it
                // as grammar rather than leaking `<name>` into the description.
                const bool optional_value = remainder.size() >= 2 &&
                    remainder.front() == '[' && remainder.back() == ']';
                const std::string normalized_value = normalize_value_name(remainder);
                if (!option.value_name || *option.value_name != normalized_value ||
                    option.value_required == optional_value) {
                    return std::nullopt;
                }
            } else {
                option.description = std::move(remainder);
            }
        }
    }

    return ParsedOptionLine{.option = std::move(option), .declaration_indent = indent};
}

bool subcommand_name_char(const unsigned char ch) {
    return std::islower(ch) || std::isdigit(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '+';
}

bool man_reference_suffix(std::string_view token, const std::size_t open) {
    if (open == std::string_view::npos || open + 2 >= token.size() || token.back() != ')') {
        return false;
    }
    for (std::size_t i = open + 1; i + 1 < token.size(); ++i) {
        if (!std::isalnum(static_cast<unsigned char>(token[i]))) {
            return false;
        }
    }
    return true;
}

std::string strip_man_reference(std::string token) {
    const auto open = token.rfind('(');
    if (man_reference_suffix(token, open)) {
        token.erase(open);
    }
    return token;
}

bool conservative_subcommand_tail(std::string_view tail_view) {
    std::string tail = trim(std::string(tail_view));
    // groff/man may render repetition with U+2026 rather than three ASCII dots.
    // It is presentation syntax, not a different grammar fact; normalize it
    // before the conservative signature check.
    constexpr std::string_view kEllipsis = "…";
    std::size_t ellipsis = 0;
    while ((ellipsis = tail.find(kEllipsis, ellipsis)) != std::string::npos) {
        tail.replace(ellipsis, kEllipsis.size(), "...");
        ellipsis += 3;
    }
    if (tail.empty()) {
        return true;
    }
    if (tail.size() > 160 || tail.find("  ") != std::string_view::npos) {
        return false;
    }

    // We intentionally do not interpret the tail yet. It is admitted only when
    // it looks like a compact argument/option signature rather than prose.
    // Examples: `[PATTERN...]`, `UNIT...`, `<command> [<args>]`.
    std::istringstream words{std::string(tail)};
    std::string word;
    while (words >> word) {
        bool structural = false;
        for (const unsigned char ch : word) {
            if (ch == '[' || ch == ']' || ch == '<' || ch == '>' || ch == '.' ||
                ch == '|' || ch == '=' || ch == '-' || ch == '_' || ch == '/' ||
                ch == ':' || ch == ',' || ch == '+' || ch == '*') {
                structural = true;
                continue;
            }
            if (std::isupper(ch) || std::isdigit(ch)) {
                structural = true;
                continue;
            }
            if (!std::islower(ch)) {
                return false;
            }
        }
        if (!structural && !(word.size() >= 2 && word.front() == '<' && word.back() == '>')) {
            return false;
        }
    }
    return true;
}


std::vector<ArgumentSpec> parse_positional_tail(
    std::string_view tail_view,
    const std::string& command,
    const std::string& section
) {
    std::string tail = trim(std::string(tail_view));
    constexpr std::string_view kEllipsis = "…";
    std::size_t ellipsis = 0;
    while ((ellipsis = tail.find(kEllipsis, ellipsis)) != std::string::npos) {
        tail.replace(ellipsis, kEllipsis.size(), "...");
        ellipsis += 3;
    }

    struct ParsedSlotAtom {
        std::string name;
        bool variadic{false};
    };

    const auto parse_slot_atom = [](std::string token) -> std::optional<ParsedSlotAtom> {
        token = trim(std::move(token));
        if (token.empty() || token.front() == '-') {
            return std::nullopt;
        }

        bool variadic = false;
        if (token.ends_with("...")) {
            variadic = true;
            token.resize(token.size() - 3);
        }

        const bool angle_wrapped = token.size() >= 2 && token.front() == '<' && token.back() == '>';
        if (angle_wrapped) {
            token = token.substr(1, token.size() - 2);
        }
        token = trim(std::move(token));
        if (token.empty()) {
            return std::nullopt;
        }

        bool looks_like_slot = false;
        for (const unsigned char ch : token) {
            if (std::isupper(ch) || std::isdigit(ch) || ch == '_' || ch == '-' || ch == '.') {
                looks_like_slot = true;
                continue;
            }
            if (std::islower(ch)) {
                // Lower-case names are accepted only when the man page marked
                // them structurally with angle brackets.
                continue;
            }
            return std::nullopt;
        }
        const bool lower_only = std::ranges::all_of(token, [](const unsigned char ch) {
            return std::islower(ch) || std::isdigit(ch) || ch == '_' || ch == '-';
        });
        if (!looks_like_slot && !(angle_wrapped && lower_only)) {
            return std::nullopt;
        }

        return ParsedSlotAtom{.name = std::move(token), .variadic = variadic};
    };

    std::vector<ArgumentSpec> positionals;
    std::istringstream stream(tail);
    std::string token;
    while (stream >> token) {
        if (token.empty()) {
            continue;
        }

        // Some generated man pages contain a redundant trailing close bracket
        // on an otherwise balanced optional positional, e.g. systemctl currently
        // renders `status [PATTERN...|PID...]]`. Treat only that exact shape as
        // source-formatting noise: removing the final `]` must leave a balanced
        // bracket expression. Nested/otherwise malformed syntax is left untouched
        // and will continue to fail the conservative slot parser below.
        if (token.size() >= 3 && token.front() == '[' && token.ends_with("]]")) {
            int depth = 0;
            bool malformed = false;
            for (std::size_t i = 0; i + 1 < token.size(); ++i) {
                if (token[i] == '[') {
                    ++depth;
                } else if (token[i] == ']') {
                    --depth;
                    if (depth < 0) {
                        malformed = true;
                        break;
                    }
                }
            }
            if (!malformed && depth == 0) {
                token.pop_back();
            }
        }

        bool required = true;
        if (token.size() >= 2 && token.front() == '[' && token.back() == ']') {
            required = false;
            token = token.substr(1, token.size() - 2);
        }
        if (token.empty() || token.front() == '-') {
            // Optional/required flags inside a command signature are not
            // positionals. This slice does not merge child option grammar into
            // the declaration tail.
            continue;
        }

        std::string slot_name;
        bool variadic = false;
        const auto alternative = token.find('|');
        if (alternative != std::string::npos) {
            // Some manuals express one argv position as a union of accepted
            // value kinds, e.g. systemctl `status [PATTERN...|PID...]`. A flat
            // binder can safely collapse this only when every arm is itself a
            // conservative slot and all arms have the same repetition shape.
            // Literal command alternatives such as `start|stop` remain
            // unrepresentable and therefore yield no positional grammar.
            std::size_t begin = 0;
            std::optional<bool> union_variadic;
            while (begin <= token.size()) {
                const auto separator = token.find('|', begin);
                const auto length = separator == std::string::npos
                    ? std::string::npos
                    : separator - begin;
                auto arm = parse_slot_atom(token.substr(begin, length));
                if (!arm) {
                    return {};
                }
                if (union_variadic && *union_variadic != arm->variadic) {
                    return {};
                }
                union_variadic = arm->variadic;
                if (!slot_name.empty()) {
                    slot_name.push_back('|');
                }
                slot_name += arm->name;
                if (separator == std::string::npos) {
                    break;
                }
                begin = separator + 1;
            }
            variadic = union_variadic.value_or(false);
        } else {
            auto parsed = parse_slot_atom(std::move(token));
            if (!parsed) {
                return {};
            }
            slot_name = std::move(parsed->name);
            variadic = parsed->variadic;
        }

        positionals.push_back(ArgumentSpec{
            .name = std::move(slot_name),
            .required = required,
            .variadic = variadic,
            .provenance = SyntaxProvenance{
                .source_kind = SyntaxSourceKind::Man,
                .source_reference = "man:" + command,
                .section = section,
            },
        });
    }
    return positionals;
}

struct ParsedSubcommandLine {
    SubcommandSpec subcommand;
    std::size_t declaration_indent{0};
};

std::optional<ParsedSubcommandLine> parse_subcommand_line(
    std::string_view raw_line,
    const std::string& command,
    const std::string& section
) {
    const std::size_t indent = indentation(raw_line);
    const std::string line = trim(std::string(raw_line));
    if (line.empty() || line.front() == '-') {
        return std::nullopt;
    }

    const auto space = line.find_first_of(" 	");
    std::string token = line.substr(0, space);
    token = strip_man_reference(std::move(token));

    const std::string command_prefix = command + "-";
    if (token.starts_with(command_prefix) && token.size() > command_prefix.size()) {
        token.erase(0, command_prefix.size());
    }

    if (token.empty() || !std::islower(static_cast<unsigned char>(token.front())) ||
        !std::ranges::all_of(token, [](const unsigned char ch) { return subcommand_name_char(ch); })) {
        return std::nullopt;
    }

    const std::string tail = space == std::string::npos ? std::string{} : trim(line.substr(space + 1));
    if (!conservative_subcommand_tail(tail)) {
        return std::nullopt;
    }

    return ParsedSubcommandLine{
        .subcommand = SubcommandSpec{
            .name = std::move(token),
            .description = {},
            .options = {},
            .positionals = parse_positional_tail(tail, command, section),
            .provenance = SyntaxProvenance{
                .source_kind = SyntaxSourceKind::Man,
                .source_reference = "man:" + command,
                .section = section,
            },
            .synopsis = {},
        },
        .declaration_indent = indent,
    };
}

void append_subcommand_description(SubcommandSpec& subcommand, std::string text) {
    text = trim(std::move(text));
    if (text.empty()) {
        return;
    }
    constexpr std::string_view kGroffWrapHyphen = "‐";
    if (subcommand.description.ends_with(kGroffWrapHyphen)) {
        subcommand.description.erase(subcommand.description.size() - kGroffWrapHyphen.size());
    } else if (!subcommand.description.empty()) {
        subcommand.description.push_back(' ');
    }
    subcommand.description += text;
}

void append_description(CommandOption& option, std::string text) {
    text = trim(std::move(text));
    if (text.empty()) {
        return;
    }

    // man/groff uses U+2010 at wrapped word boundaries on some systems. Joining
    // the next indented line verbatim would leak artifacts such as `par‐ ticularly`
    // into both terminal output and semantic option matching. Only remove that
    // explicit formatting hyphen at a line boundary; ordinary ASCII hyphens stay.
    constexpr std::string_view kGroffWrapHyphen = "‐";
    const bool wrapped_word = option.description.ends_with(kGroffWrapHyphen);
    if (wrapped_word) {
        option.description.erase(option.description.size() - kGroffWrapHyphen.size());
    } else if (!option.description.empty()) {
        option.description.push_back(' ');
    }
    option.description += text;
}

CommandGrammar parse_man_grammar(const std::string& command, std::string_view man_text) {
    CommandGrammar grammar;
    grammar.command = command;

    const std::string clean = strip_terminal_formatting(man_text);
    std::istringstream stream(clean);
    std::string line;
    std::string section;
    std::string synopsis_text;
    std::optional<std::size_t> synopsis_indent;
    std::optional<ParsedOptionLine> pending_option;
    std::optional<ParsedSubcommandLine> pending_subcommand;

    const auto flush_synopsis = [&]() {
        const std::string text = trim(synopsis_text);
        if (!text.empty()) {
            const auto duplicate = std::ranges::find_if(grammar.synopsis, [&](const SynopsisAlternative& existing) {
                return existing.text == text;
            });
            if (duplicate == grammar.synopsis.end()) {
                grammar.synopsis.push_back(SynopsisAlternative{
                    .text = text,
                    .provenance = SyntaxProvenance{
                        .source_kind = SyntaxSourceKind::Man,
                        .source_reference = "man:" + command,
                        .section = "SYNOPSIS",
                    },
                });
            }
        }
        synopsis_text.clear();
        synopsis_indent.reset();
    };

    const auto flush_option = [&]() {
        if (!pending_option) {
            return;
        }
        const auto& names = pending_option->option.names;
        const auto duplicate = std::ranges::find_if(grammar.global_options, [&](const CommandOption& existing) {
            return std::ranges::any_of(existing.names, [&](const std::string& name) {
                return std::ranges::find(names, name) != names.end();
            });
        });
        if (duplicate == grammar.global_options.end()) {
            grammar.global_options.push_back(std::move(pending_option->option));
        }
        pending_option.reset();
    };

    const auto flush_subcommand = [&]() {
        if (!pending_subcommand) {
            return;
        }
        const std::string& name = pending_subcommand->subcommand.name;
        const auto duplicate = std::ranges::find_if(grammar.subcommands, [&](const SubcommandSpec& existing) {
            return existing.name == name;
        });
        if (duplicate == grammar.subcommands.end()) {
            grammar.subcommands.push_back(std::move(pending_subcommand->subcommand));
        }
        pending_subcommand.reset();
    };

    while (std::getline(stream, line)) {
        if (const auto subsection = indented_expression_subheading(line, section)) {
            flush_synopsis();
            flush_option();
            flush_subcommand();
            section = *subsection;
            continue;
        }

        if (uppercase_heading(line)) {
            flush_synopsis();
            flush_option();
            flush_subcommand();
            section = heading_name(line);
            continue;
        }

        const std::string trimmed = trim(line);
        if (section == "SYNOPSIS") {
            if (trimmed.empty()) {
                flush_synopsis();
                continue;
            }

            const std::size_t current_indent = indentation(line);
            if (synopsis_text.empty()) {
                synopsis_indent = current_indent;
                synopsis_text = trimmed;
            } else if (synopsis_indent && current_indent <= *synopsis_indent) {
                // Man pages commonly print one independently valid invocation
                // per line at the same indentation. Preserve those alternatives
                // separately so later binding can prove one exact argv path.
                // More-indented lines remain conservative continuations of the
                // preceding alternative.
                flush_synopsis();
                synopsis_indent = current_indent;
                synopsis_text = trimmed;
            } else {
                // groff may insert U+2010 at a wrapped word boundary. Avoid
                // leaking artifacts such as `Frame‐ works` into human usage
                // output while preserving ordinary ASCII hyphens.
                constexpr std::string_view kGroffWrapHyphen = "‐";
                if (synopsis_text.ends_with(kGroffWrapHyphen)) {
                    synopsis_text.erase(synopsis_text.size() - kGroffWrapHyphen.size());
                } else {
                    synopsis_text.push_back(' ');
                }
                synopsis_text += trimmed;
            }
            if (synopsis_text.size() > 1024) {
                flush_synopsis();
            }
            continue;
        }

        if (commands_heading(section)) {
            if (pending_subcommand && !trimmed.empty() &&
                indentation(line) > pending_subcommand->declaration_indent) {
                append_subcommand_description(pending_subcommand->subcommand, trimmed);
                continue;
            }

            if (auto parsed = parse_subcommand_line(line, command, section)) {
                flush_subcommand();
                pending_subcommand = std::move(parsed);
                continue;
            }

            // A same-level category/prose line ends the previous command entry
            // but is not itself promoted into grammar.
            if (!trimmed.empty() && pending_subcommand &&
                indentation(line) <= pending_subcommand->declaration_indent) {
                flush_subcommand();
            }
            continue;
        }

        if (!option_like_heading(section)) {
            continue;
        }

        if (pending_option && !trimmed.empty() && indentation(line) > pending_option->declaration_indent) {
            // Wrapped prose may legitimately begin with another option spelling
            // (`--proxy-user`, `-i`, ...). Greater indentation proves it is a
            // continuation of the current declaration rather than a new option.
            append_description(pending_option->option, trimmed);
            continue;
        }

        if (auto parsed = parse_option_line(line, command, section)) {
            flush_option();
            pending_option = std::move(parsed);
            continue;
        }
    }

    flush_synopsis();
    flush_option();
    flush_subcommand();
    return grammar;
}

} // namespace

bool ManCommandSyntaxProvider::available() const {
    return system::find_executable("man").has_value();
}

std::optional<CommandGrammar> ManCommandSyntaxProvider::grammar(const Candidate& candidate) const {
    // Mirror ManGuidanceProvider's trust boundary: Acclorite only parses a local
    // manual after an existing knowledge source has already proved that manual.
    if (!source_contains(candidate.source, "man") || candidate.command.empty()) {
        return std::nullopt;
    }

    const auto result = system::run_capture_stdout({"man", "--", candidate.command}, 768 * 1024);
    if (result.exit_code != 0 || result.stdout_text.empty()) {
        return std::nullopt;
    }

    CommandGrammar parsed = parse_man_grammar(candidate.command, result.stdout_text);
    if (parsed.synopsis.empty() && parsed.global_options.empty() && parsed.subcommands.empty()) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<SubcommandSpec> ManCommandSyntaxProvider::subcommand_grammar(
    const Candidate& candidate,
    const SubcommandSpec& subcommand
) const {
    // Child manuals are eligible only after the same root man page proved the
    // subcommand identity. This prevents arbitrary `man <user text>` probing and
    // keeps the trust chain parent-man -> proven child name -> child-man.
    if (!source_contains(candidate.source, "man") || candidate.command.empty() ||
        subcommand.name.empty() ||
        subcommand.provenance.source_kind != SyntaxSourceKind::Man ||
        subcommand.provenance.source_reference != "man:" + candidate.command ||
        !std::ranges::all_of(subcommand.name, [](const unsigned char ch) {
            return subcommand_name_char(ch);
        })) {
        return std::nullopt;
    }

    const std::string child_page = candidate.command + "-" + subcommand.name;
    const auto result = system::run_capture_stdout({"man", "--", child_page}, 768 * 1024);
    if (result.exit_code != 0 || result.stdout_text.empty()) {
        return std::nullopt;
    }

    const CommandGrammar parsed = parse_man_grammar(child_page, result.stdout_text);
    if (parsed.global_options.empty() && parsed.synopsis.empty()) {
        return std::nullopt;
    }

    SubcommandSpec enriched = subcommand;
    enriched.options = parsed.global_options;
    enriched.synopsis = parsed.synopsis;
    return enriched;
}

} // namespace acclorite
