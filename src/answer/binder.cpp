#include "acclorite/answer/binder.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace acclorite {
namespace {

struct RawValue {
    std::string text;
    bool quoted{false};
};

enum class BindingRelation {
    None,
    Source,
    Destination,
    Name,
    Location,
};

enum class BindingKind {
    General,
    Path,
    Url,
    Number,
};

struct BindingValue {
    std::string text;
    BindingRelation relation{BindingRelation::None};
    BindingKind kind{BindingKind::General};
    bool quoted{false};
};

std::string normalized_word(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (const unsigned char ch : input) {
        if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    return out;
}

std::vector<RawValue> lexical_values(std::string_view input) {
    std::vector<RawValue> result;
    std::string current;
    char quote = '\0';
    bool quoted = false;

    auto flush = [&]() {
        if (current.empty()) {
            quoted = false;
            return;
        }
        result.push_back(RawValue{.text = std::move(current), .quoted = quoted});
        current.clear();
        quoted = false;
    };

    for (std::size_t i = 0; i < input.size(); ++i) {
        const char ch = input[i];
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
                quoted = true;
                continue;
            }
            // This is intentionally a data lexer, not a shell parser. Backslashes
            // and substitutions are never evaluated; they remain literal bytes.
            current.push_back(ch);
            continue;
        }

        if (ch == '\'' || ch == '"') {
            quote = ch;
            quoted = true;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch))) {
            flush();
            continue;
        }
        if ((ch == ',' || ch == ';' || ch == '?' || ch == '!') && !current.empty()) {
            flush();
            continue;
        }
        current.push_back(ch);
    }
    flush();
    return result;
}

bool syntax_literal(std::string_view value) {
    return value.size() >= 2 && value.front() == '-';
}

bool unsafe_positional_literal(std::string_view value) {
    // Quoting does not stop a leading-dash positional from being interpreted as
    // an option. Until a verified `--`/equivalent grammar path exists, refuse it.
    return value.empty() || value.front() == '-';
}

const std::unordered_set<std::string>& binding_scaffolding() {
    static const std::unordered_set<std::string> words{
        "a", "an", "and", "are", "as", "at", "be", "called", "can", "command",
        "could", "do", "does", "doing", "file", "files", "flag", "flags", "for",
        "fresh", "from", "get", "getting", "how", "i", "in", "into", "is", "it",
        "keeps", "make", "me", "my", "of", "on", "option", "options", "please",
        "put", "read", "reading", "response", "responses", "second", "seconds",
        "show", "shows", "start", "subcommand", "subcommands", "that", "the", "them",
        "this", "to", "too", "unit", "use", "using", "video", "what", "which", "with"
    };
    return words;
}

bool action_has_term(const Query& query, std::string_view term) {
    if (!query.action_target) {
        return false;
    }
    return std::ranges::any_of(query.action_target->terms, [&](const std::string& candidate) {
        return normalized_word(candidate) == term;
    });
}

bool operation_paraphrase_scaffolding(const Query& query, std::string_view word) {
    // Human-intent recovery canonicalizes phrases such as "go there" into the
    // `switch` operation and "new branch" into `create + branch`. The binder
    // must consume those original paraphrase words as structure as well, or they
    // look like extra user literals and make an otherwise proven value bind
    // ambiguous. Keep this conditional on the recovered operation so ordinary
    // data named `new`, `go`, etc. remains bindable in unrelated commands.
    if (action_has_term(query, "switch") &&
        (word == "go" || word == "there" || word == "checkout")) {
        return true;
    }
    if (action_has_term(query, "create") && action_has_term(query, "branch") &&
        (word == "new" || word == "fresh")) {
        return true;
    }
    if (action_has_term(query, "directory") &&
        (word == "directory" || word == "directories" || word == "folder" || word == "folders")) {
        return true;
    }
    if ((action_has_term(query, "rename") || action_has_term(query, "move")) &&
        (word == "rename" || word == "renaming" || word == "move" || word == "moving")) {
        return true;
    }
    if (action_has_term(query, "copy") &&
        (word == "copy" || word == "copying" || word == "duplicate" || word == "duplicating")) {
        return true;
    }
    if ((action_has_term(query, "remove") || action_has_term(query, "delete")) &&
        (word == "remove" || word == "removing" || word == "delete" || word == "deleting")) {
        return true;
    }
    if (action_has_term(query, "search") &&
        (word == "search" || word == "searching" || word == "find" || word == "finding")) {
        return true;
    }
    // Capability recovery may translate human descriptions of filtering into
    // grammar terms. If "skipping dotfiles" produced the verified
    // hidden-files operation, those source words are semantic scaffolding, not
    // a PATTERN/PATH pair for a root search synopsis. Keep this conditional so
    // unrelated commands can still bind literals named "dotfiles" or
    // "skipping" when no hidden-files operation was recovered.
    if (action_has_term(query, "hidden") && action_has_term(query, "files") &&
        (word == "dotfile" || word == "dotfiles" || word == "skip" ||
         word == "skips" || word == "skipping" || word == "search" ||
         word == "searching" || word == "include" || word == "including" ||
         word == "hidden")) {
        return true;
    }
    if (action_has_term(query, "output") &&
        (word == "save" || word == "saving" || word == "write" || word == "writing" ||
         word == "output" || word == "download" || word == "downloading")) {
        return true;
    }
    return false;
}

BindingKind classify_binding_kind(std::string_view value) {
    if (value.find("://") != std::string_view::npos) {
        return BindingKind::Url;
    }
    if (value.starts_with("~/") || value.starts_with("./") || value.starts_with("../") ||
        value.starts_with("/") || value.find('/') != std::string_view::npos) {
        return BindingKind::Path;
    }
    const bool numeric = !value.empty() && std::ranges::all_of(value, [](const unsigned char ch) {
        return std::isdigit(ch) || ch == '.' || ch == ':';
    });
    return numeric ? BindingKind::Number : BindingKind::General;
}

BindingRelation relation_from_word(std::string_view word) {
    if (word == "from") return BindingRelation::Source;
    if (word == "to" || word == "into" || word == "as") return BindingRelation::Destination;
    if (word == "called" || word == "named") return BindingRelation::Name;
    if (word == "in" || word == "inside" || word == "under" || word == "within") {
        return BindingRelation::Location;
    }
    return BindingRelation::None;
}

std::vector<BindingValue> extract_binding_entities(const Query& query) {
    std::vector<BindingValue> values;
    std::unordered_set<std::string> structural_terms;
    if (query.action_target) {
        if (query.action_target->command) {
            structural_terms.insert(normalized_word(*query.action_target->command));
        }
        if (query.action_target->literal) {
            structural_terms.insert(normalized_word(*query.action_target->literal));
        }
        for (const auto& term : query.action_target->terms) {
            structural_terms.insert(normalized_word(term));
        }
    }

    BindingRelation pending_relation = BindingRelation::None;
    for (const auto& raw : lexical_values(query.raw)) {
        if (raw.text.empty() || syntax_literal(raw.text)) {
            continue;
        }
        const std::string word = normalized_word(raw.text);
        if (!raw.quoted) {
            if (const auto relation = relation_from_word(word); relation != BindingRelation::None) {
                pending_relation = relation;
                continue;
            }
        }
        if (word.empty() || structural_terms.contains(word) ||
            (!raw.quoted && (binding_scaffolding().contains(word) ||
                             operation_paraphrase_scaffolding(query, word)))) {
            continue;
        }

        // Quoted text is always intentional user data. Unquoted literals are
        // admitted only after structural/filler removal; this keeps identifiers,
        // paths, ports, timestamps and names while dropping conversational prose.
        if (unsafe_positional_literal(raw.text)) {
            continue;
        }
        values.push_back(BindingValue{
            .text = raw.text,
            .relation = pending_relation,
            .kind = classify_binding_kind(raw.text),
            .quoted = raw.quoted,
        });
        pending_relation = BindingRelation::None;
    }
    return values;
}

std::vector<std::string> extract_binding_values(const Query& query) {
    std::vector<std::string> values;
    for (auto& value : extract_binding_entities(query)) {
        values.push_back(std::move(value.text));
    }
    return values;
}

std::string required_placeholder(const ArgumentSpec& argument) {
    std::string name = argument.name.empty() ? "VALUE" : argument.name;
    if (argument.variadic && !name.ends_with("...")) {
        name += "...";
    }
    return "<" + name + ">";
}

std::string option_placeholder(const CommandOption& option) {
    std::string name = option.value_name.value_or("VALUE");
    if (name.size() >= 2 &&
        ((name.front() == '<' && name.back() == '>') ||
         (name.front() == '[' && name.back() == ']'))) {
        name = name.substr(1, name.size() - 2);
    }
    return "<" + name + ">";
}

InvocationArgument literal_argument(std::string value) {
    return InvocationArgument{.value = std::move(value), .placeholder = false};
}

InvocationArgument placeholder_argument(std::string value) {
    return InvocationArgument{.value = std::move(value), .placeholder = true};
}

std::string normalize_synopsis_token(std::string token) {
    constexpr std::string_view kEllipsis = "…";
    std::size_t pos = 0;
    while ((pos = token.find(kEllipsis, pos)) != std::string::npos) {
        token.replace(pos, kEllipsis.size(), "...");
        pos += 3;
    }
    return token;
}

struct SynopsisToken {
    std::string body;
    bool optional{false};
};

SynopsisToken unwrap_synopsis_token(std::string token) {
    token = normalize_synopsis_token(std::move(token));
    bool optional = false;
    if (token.size() >= 5 && token.front() == '[' && token.ends_with("]...")) {
        optional = true;
        token = token.substr(1, token.size() - 5) + "...";
    }
    bool changed = true;
    while (changed && token.size() >= 2) {
        changed = false;
        if (token.front() == '[' && token.back() == ']') {
            optional = true;
            token = token.substr(1, token.size() - 2);
            changed = true;
        } else if (token.front() == '(' && token.back() == ')') {
            token = token.substr(1, token.size() - 2);
            changed = true;
        }
    }
    return SynopsisToken{.body = std::move(token), .optional = optional};
}

std::vector<std::string> split_synopsis_alternatives(std::string_view body) {
    std::vector<std::string> parts;
    std::size_t begin = 0;
    while (begin <= body.size()) {
        const auto separator = body.find('|', begin);
        const auto length = separator == std::string_view::npos
            ? std::string_view::npos
            : separator - begin;
        std::string part(body.substr(begin, length));
        if (!part.empty()) {
            parts.push_back(std::move(part));
        }
        if (separator == std::string_view::npos) {
            break;
        }
        begin = separator + 1;
    }
    return parts;
}

std::string synopsis_slot_name(std::string token) {
    auto unwrapped = unwrap_synopsis_token(std::move(token));
    token = std::move(unwrapped.body);
    if (token.ends_with("...")) {
        token.resize(token.size() - 3);
    }
    if (token.size() >= 2 && token.front() == '<' && token.back() == '>') {
        token = token.substr(1, token.size() - 2);
    }
    return normalized_word(token);
}

bool synopsis_token_contains_option(
    const SynopsisToken& token,
    const CommandOption& option
) {
    for (const auto& alternative : split_synopsis_alternatives(token.body)) {
        for (const auto& name : option.names) {
            if (alternative == name) {
                return true;
            }
            // Admit an attached-value spelling such as `--output=<path>` only
            // when the option name itself is still exact.
            if (alternative.starts_with(name + "=")) {
                return true;
            }
        }
    }
    return false;
}

bool synopsis_token_is_generic_options(const SynopsisToken& token) {
    const std::string word = synopsis_slot_name(token.body);
    return word == "option" || word == "options";
}

bool synopsis_proves_complete_subcommand_option(
    const SubcommandSpec& subcommand,
    const CommandOption& option,
    const CommandInvocation& invocation
) {
    if (subcommand.synopsis.empty() || invocation.arguments.size() < 2 ||
        std::ranges::any_of(invocation.arguments, [](const InvocationArgument& argument) {
            return argument.placeholder;
        })) {
        return false;
    }

    const std::string expected_value_name = option.value_name
        ? synopsis_slot_name(*option.value_name)
        : std::string{};

    for (const auto& alternative : subcommand.synopsis) {
        std::istringstream stream(alternative.text);
        std::vector<std::string> tokens;
        std::string token;
        while (stream >> token) {
            tokens.push_back(std::move(token));
        }
        if (tokens.empty()) {
            continue;
        }

        // A child manual may spell the program either as `git switch ...` or
        // as one parent-derived executable/page identity such as `git-switch ...`.
        std::optional<std::size_t> subcommand_index;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            const auto current = unwrap_synopsis_token(tokens[i]);
            if (current.body == subcommand.name ||
                current.body.ends_with("-" + subcommand.name)) {
                subcommand_index = i;
                break;
            }
        }
        if (!subcommand_index) {
            continue;
        }

        bool saw_selected_option = false;
        bool consumed_option_value = !option.takes_value;
        bool invalid_required_piece = false;

        for (std::size_t i = *subcommand_index + 1; i < tokens.size(); ++i) {
            const SynopsisToken current = unwrap_synopsis_token(tokens[i]);
            if (current.body.empty()) {
                continue;
            }
            if (synopsis_token_is_generic_options(current)) {
                // Generic `<options>` groups establish allowance, not the exact
                // path selected by the composer. Completeness requires an
                // explicit occurrence of the chosen option elsewhere.
                continue;
            }
            if (synopsis_token_contains_option(current, option)) {
                saw_selected_option = true;
                continue;
            }

            const std::string slot_name = synopsis_slot_name(current.body);
            if (saw_selected_option && option.takes_value && !consumed_option_value &&
                !slot_name.empty() && slot_name == expected_value_name) {
                consumed_option_value = true;
                continue;
            }

            if (current.optional) {
                continue;
            }

            // Any other mandatory literal/slot means this synopsis branch needs
            // syntax or user data that the current structured invocation does
            // not contain. Refuse completeness rather than silently assuming it.
            invalid_required_piece = true;
            break;
        }

        if (saw_selected_option && consumed_option_value && !invalid_required_piece) {
            return true;
        }
    }
    return false;
}

bool generic_root_options_token(const SynopsisToken& token) {
    std::string word = synopsis_slot_name(token.body);
    return word == "option" || word == "options" || word == "opts";
}

struct RootSlot {
    std::string name;
    bool required{true};
    bool variadic{false};
};

struct RootSynopsisShape {
    std::vector<RootSlot> slots;
    bool permits_options{false};
};

std::optional<RootSlot> root_slot_from_token(SynopsisToken token) {
    if (token.body.empty() || token.body.front() == '-') {
        return std::nullopt;
    }

    std::string body = std::move(token.body);
    bool variadic = false;
    if (body.ends_with("...")) {
        variadic = true;
        body.resize(body.size() - 3);
    }

    // A union at one argv position is safe only when every arm is a structural
    // value slot with the same repetition shape. Literal choices remain too
    // expressive for this flat root binder.
    std::vector<std::string> arms = split_synopsis_alternatives(body);
    if (arms.empty()) {
        arms.push_back(body);
    }

    std::string combined_name;
    std::optional<bool> union_variadic;
    for (std::string arm : arms) {
        bool arm_variadic = variadic;
        if (arm.ends_with("...")) {
            arm_variadic = true;
            arm.resize(arm.size() - 3);
        }
        if (arm.size() >= 2 && arm.front() == '<' && arm.back() == '>') {
            arm = arm.substr(1, arm.size() - 2);
        }
        const std::string normalized = normalized_word(arm);
        if (normalized.empty()) {
            return std::nullopt;
        }

        // Root synopsis values are normally uppercase metavariables or angle-
        // bracketed lower-case names. Plain lower-case literals are command
        // grammar, not bindable user data.
        const bool angle_wrapped = body.find('<') != std::string::npos;
        const bool has_upper = std::ranges::any_of(arm, [](const unsigned char ch) {
            return std::isupper(ch);
        });
        if (!angle_wrapped && !has_upper) {
            return std::nullopt;
        }
        if (union_variadic && *union_variadic != arm_variadic) {
            return std::nullopt;
        }
        union_variadic = arm_variadic;
        if (!combined_name.empty()) combined_name.push_back('|');
        combined_name += arm;
    }

    return RootSlot{
        .name = std::move(combined_name),
        .required = !token.optional,
        .variadic = union_variadic.value_or(variadic),
    };
}

std::optional<RootSynopsisShape> parse_root_synopsis_shape(
    const SynopsisAlternative& alternative,
    std::string_view command,
    const CommandOption* selected_option
) {
    std::istringstream stream(alternative.text);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token) {
        tokens.push_back(normalize_synopsis_token(std::move(token)));
    }
    if (tokens.empty()) {
        return std::nullopt;
    }

    // Locate the root command identity conservatively. Man pages sometimes
    // prefix a synopsis with lightweight wrappers, but we never skip arbitrary
    // required syntax after the command itself.
    std::optional<std::size_t> command_index;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const auto current = unwrap_synopsis_token(tokens[i]);
        if (current.body == command) {
            command_index = i;
            break;
        }
    }
    if (!command_index) {
        return std::nullopt;
    }

    RootSynopsisShape shape;
    for (std::size_t i = *command_index + 1; i < tokens.size(); ++i) {
        SynopsisToken current = unwrap_synopsis_token(tokens[i]);
        if (current.body.empty()) {
            continue;
        }

        if (generic_root_options_token(current)) {
            shape.permits_options = true;
            continue;
        }

        if (current.body.front() == '-') {
            bool selected = false;
            if (selected_option && synopsis_token_contains_option(current, *selected_option)) {
                selected = true;
                shape.permits_options = true;
            }

            // An optional unrelated option is ignorable. A required unrelated
            // option means this alternative needs syntax we are not constructing.
            if (!selected && !current.optional) {
                return std::nullopt;
            }
            continue;
        }

        if (auto slot = root_slot_from_token(current)) {
            shape.slots.push_back(std::move(*slot));
            continue;
        }

        // Optional opaque groups are allowed but cannot contribute completeness.
        // Required literals/groups would require interpreting richer grammar.
        if (!current.optional) {
            return std::nullopt;
        }
    }

    if (selected_option && !shape.permits_options) {
        return std::nullopt;
    }
    return shape;
}

std::size_t required_values_after(const std::vector<RootSlot>& slots, const std::size_t index) {
    std::size_t required = 0;
    for (std::size_t i = index + 1; i < slots.size(); ++i) {
        if (slots[i].required) {
            ++required;
        }
    }
    return required;
}

int option_value_score(const BindingValue& value, const CommandOption& option) {
    const std::string name = normalized_word(option.value_name.value_or("value"));
    int score = 0;
    if ((name.find("url") != std::string::npos || name.find("uri") != std::string::npos) &&
        value.kind == BindingKind::Url) {
        score += 8;
    }
    if ((name.find("path") != std::string::npos || name.find("file") != std::string::npos ||
         name.find("output") != std::string::npos || name.find("dest") != std::string::npos) &&
        value.relation == BindingRelation::Destination) {
        score += 9;
    }
    if ((name.find("path") != std::string::npos || name.find("file") != std::string::npos) &&
        value.kind == BindingKind::Path) {
        score += 4;
    }
    if ((name.find("branch") != std::string::npos || name.find("name") != std::string::npos) &&
        value.relation == BindingRelation::Name) {
        score += 8;
    }
    if ((name.find("position") != std::string::npos || name.find("time") != std::string::npos ||
         name.find("second") != std::string::npos || name.find("count") != std::string::npos ||
         name.find("port") != std::string::npos || name.find("pid") != std::string::npos) &&
        value.kind == BindingKind::Number) {
        score += 7;
    }
    if (value.quoted) {
        score += 1;
    }
    return score;
}

std::optional<std::size_t> select_option_value(
    const std::vector<BindingValue>& values,
    const CommandOption& option
) {
    if (values.empty()) {
        return std::nullopt;
    }
    if (values.size() == 1) {
        return 0;
    }

    int best_score = -1;
    std::optional<std::size_t> best;
    bool tied = false;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const int score = option_value_score(values[i], option);
        if (score > best_score) {
            best_score = score;
            best = i;
            tied = false;
        } else if (score == best_score) {
            tied = true;
        }
    }
    if (best_score <= 0 || tied) {
        return std::nullopt;
    }
    return best;
}

std::optional<CommandInvocation> bind_root_from_entities(
    const CommandGrammar& grammar,
    std::vector<BindingValue> values,
    const CommandOption* selected_option
) {
    if (grammar.command.empty() || grammar.synopsis.empty()) {
        return std::nullopt;
    }

    std::optional<InvocationArgument> option_value;
    if (selected_option) {
        if (selected_option->names.empty() || !selected_option->value_shape_known) {
            return std::nullopt;
        }
        if (selected_option->takes_value) {
            if (const auto index = select_option_value(values, *selected_option)) {
                option_value = literal_argument(values[*index].text);
                values.erase(values.begin() + static_cast<std::ptrdiff_t>(*index));
            } else if (selected_option->value_required) {
                option_value = placeholder_argument(option_placeholder(*selected_option));
            }
        }
    }

    std::optional<CommandInvocation> best;
    std::size_t best_placeholders = static_cast<std::size_t>(-1);
    for (const auto& alternative : grammar.synopsis) {
        auto shape = parse_root_synopsis_shape(alternative, grammar.command, selected_option);
        if (!shape) {
            continue;
        }

        CommandInvocation invocation{.command = grammar.command, .arguments = {}, .complete = true};
        if (selected_option) {
            invocation.arguments.push_back(literal_argument(selected_option->names.front()));
            if (option_value) {
                invocation.arguments.push_back(*option_value);
            }
        }

        std::size_t value_index = 0;
        std::size_t placeholder_count = option_value && option_value->placeholder ? 1 : 0;
        bool valid = true;
        for (std::size_t slot_index = 0; slot_index < shape->slots.size(); ++slot_index) {
            const auto& slot = shape->slots[slot_index];
            const std::size_t minimum_after = required_values_after(shape->slots, slot_index);
            const std::size_t available = values.size() - value_index;

            if (slot.variadic) {
                const std::size_t can_take = available > minimum_after ? available - minimum_after : 0;
                if (can_take == 0 && slot.required) {
                    invocation.arguments.push_back(placeholder_argument("<" + slot.name + "...>"));
                    ++placeholder_count;
                    invocation.complete = false;
                } else {
                    for (std::size_t count = 0; count < can_take; ++count) {
                        invocation.arguments.push_back(literal_argument(values[value_index++].text));
                    }
                }
                continue;
            }

            if (available > minimum_after) {
                invocation.arguments.push_back(literal_argument(values[value_index++].text));
            } else if (slot.required) {
                invocation.arguments.push_back(placeholder_argument("<" + slot.name + ">"));
                ++placeholder_count;
                invocation.complete = false;
            }
        }

        if (value_index != values.size()) {
            valid = false;
        }
        if (!valid) {
            continue;
        }

        if (selected_option) {
            if (option_value && option_value->placeholder) {
                invocation.complete = false;
            }
            // Independent option grammar plus a root synopsis generic OPTIONS
            // group is enough to prove syntactic allowance. We do not require
            // the selected option spelling to be duplicated in SYNOPSIS.
            if (!shape->permits_options) {
                invocation.complete = false;
            }
        }

        if (!best || placeholder_count < best_placeholders ||
            (placeholder_count == best_placeholders && invocation.complete && !best->complete)) {
            best = std::move(invocation);
            best_placeholders = placeholder_count;
        }
    }
    return best;
}

} // namespace

std::optional<CommandInvocation> ArgumentBinder::bind_root(
    const Query& query,
    const CommandGrammar& grammar
) {
    auto values = extract_binding_entities(query);
    if (values.empty()) {
        return std::nullopt;
    }
    return bind_root_from_entities(grammar, std::move(values), nullptr);
}

std::optional<CommandInvocation> ArgumentBinder::bind_option(
    const Query& query,
    const CommandGrammar& grammar,
    const CommandOption& option
) {
    if (grammar.command.empty() || option.names.empty() || !option.value_shape_known || !option.takes_value) {
        return std::nullopt;
    }

    CommandInvocation invocation{
        .command = grammar.command,
        .arguments = {literal_argument(option.names.front())},
        .complete = false,
    };

    const auto values = extract_binding_values(query);
    if (values.size() == 1) {
        invocation.arguments.push_back(literal_argument(values.front()));
    } else if (option.value_required) {
        invocation.arguments.push_back(placeholder_argument(option_placeholder(option)));
    }

    // An option fact alone does not prove the command's root positional grammar.
    // Even when its own value is bound, the invocation remains an explicit
    // partial/template until a synopsis path proves the rest of argv.
    return invocation;
}

std::optional<CommandInvocation> ArgumentBinder::bind_root_option(
    const Query& query,
    const CommandGrammar& grammar,
    const CommandOption& option
) {
    auto values = extract_binding_entities(query);
    if (values.empty() && (!option.takes_value || !option.value_required)) {
        return std::nullopt;
    }
    return bind_root_from_entities(grammar, std::move(values), &option);
}

std::optional<CommandInvocation> ArgumentBinder::bind_subcommand(
    const Query& query,
    const CommandGrammar& grammar,
    const SubcommandSpec& subcommand
) {
    if (grammar.command.empty() || subcommand.name.empty() || subcommand.positionals.empty()) {
        return std::nullopt;
    }

    const auto values = extract_binding_values(query);
    std::size_t value_index = 0;
    bool complete = true;

    CommandInvocation invocation{
        .command = grammar.command,
        .arguments = {literal_argument(subcommand.name)},
        .complete = true,
    };

    for (const auto& positional : subcommand.positionals) {
        if (positional.variadic) {
            if (value_index < values.size()) {
                while (value_index < values.size()) {
                    invocation.arguments.push_back(literal_argument(values[value_index++]));
                }
            } else if (positional.required) {
                invocation.arguments.push_back(placeholder_argument(required_placeholder(positional)));
                complete = false;
            }
            continue;
        }

        if (value_index < values.size()) {
            invocation.arguments.push_back(literal_argument(values[value_index++]));
        } else if (positional.required) {
            invocation.arguments.push_back(placeholder_argument(required_placeholder(positional)));
            complete = false;
        }
    }

    // Unconsumed user values mean the flat positional grammar is insufficient to
    // prove their role. Refuse the bind rather than silently dropping them.
    if (value_index != values.size()) {
        return std::nullopt;
    }

    invocation.complete = complete;
    return invocation;
}

std::optional<CommandInvocation> ArgumentBinder::bind_subcommand_option(
    const Query& query,
    const CommandGrammar& grammar,
    const SubcommandSpec& subcommand,
    const CommandOption& option
) {
    if (grammar.command.empty() || subcommand.name.empty() || option.names.empty() || !option.value_shape_known) {
        return std::nullopt;
    }

    CommandInvocation invocation{
        .command = grammar.command,
        .arguments = {
            literal_argument(subcommand.name),
            literal_argument(option.names.front()),
        },
        // Child option grammar proves this local syntax fragment, but until a
        // compatible child synopsis path is structurally selected we do not
        // claim the whole command line is complete.
        .complete = false,
    };

    const auto values = extract_binding_values(query);
    if (option.takes_value) {
        if (values.size() == 1) {
            invocation.arguments.push_back(literal_argument(values.front()));
        } else if (option.value_required && values.empty()) {
            invocation.arguments.push_back(placeholder_argument(option_placeholder(option)));
        } else {
            // Multiple unassigned literals require a selected child synopsis
            // branch with independently proven positional roles. This binder
            // still refuses that shape rather than assigning by guesswork.
            return std::nullopt;
        }
    } else if (!values.empty()) {
        return std::nullopt;
    }

    invocation.complete = synopsis_proves_complete_subcommand_option(
        subcommand, option, invocation
    );
    return invocation;
}

} // namespace acclorite
