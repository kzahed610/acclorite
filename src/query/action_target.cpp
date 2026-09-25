#include "acclorite/query/action_target.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace acclorite::query {
namespace {

bool wrapping_punctuation(const char ch) {
    switch (ch) {
        case '\'': case '"': case '`':
        case '(': case ')': case '[': case ']': case '{': case '}':
        case ',': case ';': case ':': case '.': case '?': case '!':
            return true;
        default:
            return false;
    }
}

bool punctuation_short_option(std::string_view token) {
    // POSIX-style short options are not limited to alphanumerics. Real tools
    // expose spellings such as ripgrep's `-.` and common `-?` help aliases.
    // Once surrounding sentence punctuation has been stripped down to exactly
    // `-X`, the second byte is syntax and must be preserved verbatim.
    return token.size() == 2 && token.front() == '-' && token != "--";
}

std::string clean_raw_token(std::string token) {
    while (!token.empty() && wrapping_punctuation(token.front())) {
        token.erase(token.begin());
    }
    while (!token.empty() && wrapping_punctuation(token.back()) &&
           !punctuation_short_option(token)) {
        token.pop_back();
    }
    return token;
}

std::vector<std::string> raw_tokens(const Query& query) {
    std::istringstream stream(query.raw);
    std::vector<std::string> out;
    std::string token;
    while (stream >> token) {
        token = clean_raw_token(std::move(token));
        if (!token.empty()) {
            out.push_back(std::move(token));
        }
    }
    return out;
}

std::string lower_copy(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const unsigned char ch : text) {
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

bool option_literal(std::string_view token) {
    return token.size() >= 2 && token.front() == '-' && token != "--";
}

bool syntax_scaffolding(std::string_view word) {
    static const std::unordered_set<std::string> words{
        "a", "an", "and", "are", "can", "command", "do", "does", "for", "flag",
        "flags", "i", "in", "is", "it", "make", "makes", "me", "of", "on", "option",
        "options", "please", "subcommand", "subcommands", "that", "the", "this", "to",
        "tool", "use", "used", "using", "what", "which", "with"
    };
    return words.contains(std::string(word));
}

bool capability_relation_word(std::string_view word) {
    static const std::unordered_set<std::string> words{
        "include", "includes", "including", "enable", "enables", "enabled", "allow", "allows",
        "allowing", "make", "makes", "making", "used", "use", "using"
    };
    return words.contains(std::string(word));
}

std::vector<std::string> normalized_tokens(const Query& query) {
    std::istringstream stream(query.normalized);
    std::vector<std::string> out;
    std::string token;
    while (stream >> token) {
        token = clean_raw_token(std::move(token));
        if (!token.empty()) {
            out.push_back(std::move(token));
        }
    }
    return out;
}


bool conversational_lead(std::string_view word) {
    static const std::unordered_set<std::string> words{
        "a", "an", "bro", "cmd", "command", "got", "how", "i", "my", "need",
        "please", "something", "the", "thing", "this", "tool", "what", "where",
        "which", "who", "why", "wht"
    };
    return words.contains(std::string(word));
}

bool command_predicate(std::string_view word) {
    static const std::unordered_set<std::string> words{
        "can", "copy", "copies", "create", "creates", "delete", "deletes",
        "does", "download", "downloads", "get", "gets", "is", "keeps", "make",
        "makes", "move", "moves", "read", "reads", "remove", "removes", "rename",
        "renames", "save", "saves", "search", "searches", "show", "shows", "start",
        "starts", "stop", "stops", "tell", "tells", "use", "uses", "write", "writes",
        "won't", "wont"
    };
    return words.contains(std::string(word));
}

std::optional<std::string> leading_command_hint(const Query& query) {
    const auto tokens = normalized_tokens(query);
    if (tokens.size() < 2) {
        return std::nullopt;
    }

    const std::string first = lower_copy(tokens.front());
    if (first.size() < 2 || first.front() == '-' || conversational_lead(first) ||
        syntax_scaffolding(first)) {
        return std::nullopt;
    }

    const std::string second = lower_copy(tokens[1]);
    if (!command_predicate(second)) {
        return std::nullopt;
    }
    return first;
}

bool contains_any(const std::vector<std::string>& tokens,
                  const std::initializer_list<std::string_view> needles) {
    return std::ranges::any_of(tokens, [&](const std::string& token) {
        return std::ranges::find(needles, std::string_view(token)) != needles.end();
    });
}

void append_unique(std::vector<std::string>& out, std::string value) {
    if (value.size() < 2 || std::ranges::find(out, value) != out.end()) {
        return;
    }
    out.push_back(std::move(value));
}

std::vector<std::string> implicit_operation_terms(
    const Query& query,
    const std::string& command
) {
    const auto tokens = normalized_tokens(query);
    std::vector<std::string> terms;

    // Phrase-level recovery translates human descriptions into grammar-facing
    // capability language. These mappings describe operations, never commands.
    if (contains_any(tokens, {"dotfile", "dotfiles"})) {
        append_unique(terms, "hidden");
        append_unique(terms, "files");
    }
    if (contains_any(tokens, {"redirect", "redirects"}) || contains_any(tokens, {"3xx"})) {
        if (contains_any(tokens, {"follow", "following"})) {
            append_unique(terms, "follow");
        }
        append_unique(terms, "redirect");
    }
    if (contains_any(tokens, {"doing"}) && contains_any(tokens, {"show", "shows", "status"})) {
        append_unique(terms, "status");
    }
    if (contains_any(tokens, {"second", "seconds", "timestamp", "time"}) &&
        contains_any(tokens, {"start", "reading", "read", "from"})) {
        append_unique(terms, "seek");
        append_unique(terms, "position");
    }
    if (contains_any(tokens, {"branch", "branches"}) &&
        contains_any(tokens, {"create", "fresh", "make", "new"})) {
        append_unique(terms, "create");
        append_unique(terms, "branch");

        // Multi-action branch requests commonly describe the second operation
        // without saying the literal subcommand: "put me on it", "go there",
        // or "switch to it". Preserve that requested state transition as a
        // capability term; verified child grammar still decides how it is done.
        const bool explicit_switch = contains_any(tokens, {"switch", "switches", "switching", "checkout"});
        const bool put_on_it = contains_any(tokens, {"put"}) && contains_any(tokens, {"on"});
        const bool go_there = contains_any(tokens, {"go"}) && contains_any(tokens, {"there"});
        if (explicit_switch || put_on_it || go_there) {
            append_unique(terms, "switch");
        }
    }
    if (terms.empty() && contains_any(tokens, {"rename", "renames", "renaming", "move", "moves", "moving"})) {
        append_unique(terms, "rename");
        append_unique(terms, "move");
    }
    if (terms.empty() && contains_any(tokens, {"copy", "copies", "copying", "duplicate", "duplicating"})) {
        append_unique(terms, "copy");
    }
    if (terms.empty() && contains_any(tokens, {"directory", "directories", "folder", "folders"}) &&
        contains_any(tokens, {"create", "creates", "make", "makes", "new"})) {
        append_unique(terms, "create");
        append_unique(terms, "directory");
    }
    if (terms.empty() && contains_any(tokens, {"remove", "removes", "removing", "delete", "deletes", "deleting"})) {
        append_unique(terms, "remove");
        append_unique(terms, "delete");
    }
    if (terms.empty() && contains_any(tokens, {"save", "saves", "saving", "write", "writes", "writing", "output"})) {
        append_unique(terms, "output");
    }
    if (terms.empty() && contains_any(tokens, {"search", "searches", "searching", "find", "finding"})) {
        append_unique(terms, "search");
    }

    // Phrase recovery deliberately returns a compact capability signature.
    // Mixing every surrounding noun back in would make grammar matching brittle
    // (for example `seek position` should not be diluted by `video seconds`).
    if (!terms.empty()) {
        return terms;
    }

    // Without an explicit parent-command anchor, only the conservative phrase
    // mappings above are allowed to create an operation target. The generic
    // fallback below exists to describe a command-addressed sentence and would
    // otherwise mistake arbitrary user literals for semantic capability terms.
    if (command.empty()) {
        return {};
    }

    static const std::unordered_set<std::string> filler{
        "a", "an", "and", "are", "called", "can", "does", "for", "from", "get",
        "getting", "here", "i", "in", "into", "is", "it", "keeps", "make", "me",
        "my", "of", "on", "please", "response", "responses", "some", "that", "the",
        "them", "this", "to", "too", "use", "using", "what", "which", "with"
    };

    for (const auto& raw : tokens) {
        const std::string word = lower_copy(raw);
        if (word == command || option_literal(raw) || filler.contains(word) ||
            syntax_scaffolding(word) || word.size() < 2) {
            continue;
        }
        if (raw.find("://") != std::string::npos || raw.starts_with("~/") ||
            raw.starts_with("./") || raw.starts_with("../") || raw.starts_with("/") ||
            raw.find('/') != std::string::npos || raw.find('.') != std::string::npos) {
            continue;
        }
        const bool numeric = std::ranges::all_of(word, [](const unsigned char ch) {
            return std::isdigit(ch);
        });
        if (numeric || word == "3xx" || word == "dotfile" || word == "dotfiles" ||
            word == "doing" || word == "seconds" || word == "second") {
            continue;
        }
        append_unique(terms, word);
        if (terms.size() >= 5) {
            break;
        }
    }
    return terms;
}

bool has_concrete_binding_literal(const Query& query) {
    for (const auto& token : raw_tokens(query)) {
        if (token.find("://") != std::string::npos || token.starts_with("~/") ||
            token.starts_with("./") || token.starts_with("../") || token.starts_with("/") ||
            token.find('/') != std::string::npos) {
            return true;
        }
        if (token.find('.') != std::string::npos && !option_literal(token)) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> neighboring_command(
    const std::vector<std::string>& tokens,
    const std::size_t pivot
) {
    if (pivot > 0) {
        for (std::size_t i = pivot; i > 0; --i) {
            const auto& token = tokens[i - 1];
            if (option_literal(token)) {
                continue;
            }
            const std::string lower = lower_copy(token);
            if (!syntax_scaffolding(lower)) {
                return lower;
            }
        }
    }

    for (std::size_t i = pivot + 1; i < tokens.size(); ++i) {
        const auto& token = tokens[i];
        if (option_literal(token)) {
            continue;
        }
        const std::string lower = lower_copy(token);
        if (!syntax_scaffolding(lower)) {
            return lower;
        }
    }
    return std::nullopt;
}

std::vector<std::string> capability_terms(
    const std::vector<std::string>& tokens,
    const std::optional<std::string>& command,
    const std::optional<std::string>& literal
) {
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    for (const auto& raw : tokens) {
        if (option_literal(raw)) {
            continue;
        }
        const std::string word = lower_copy(raw);
        if ((command && word == *command) || (literal && raw == *literal) ||
            syntax_scaffolding(word) || capability_relation_word(word)) {
            continue;
        }
        if (word.size() < 2) {
            continue;
        }
        if (seen.insert(word).second) {
            result.push_back(word);
        }
    }
    return result;
}

} // namespace

std::optional<ActionTarget> detect_action_target(const Query& query) {
    const auto raw = raw_tokens(query);
    if (raw.empty()) {
        return std::nullopt;
    }

    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (!option_literal(raw[i])) {
            continue;
        }
        const auto command = neighboring_command(raw, i);
        return ActionTarget{
            .kind = ActionTargetKind::Option,
            .command = command,
            .literal = raw[i],
            .terms = {},
            .explicit_syntax = true,
        };
    }

    const auto normalized = normalized_tokens(query);
    for (std::size_t i = 0; i < normalized.size(); ++i) {
        if (normalized[i] != "flag" && normalized[i] != "flags" &&
            normalized[i] != "option" && normalized[i] != "options") {
            continue;
        }
        const auto command = neighboring_command(normalized, i);
        auto terms = capability_terms(normalized, command, std::nullopt);
        if (!command || terms.empty()) {
            return std::nullopt;
        }
        return ActionTarget{
            .kind = ActionTargetKind::Option,
            .command = command,
            .literal = std::nullopt,
            .terms = std::move(terms),
            .explicit_syntax = false,
        };
    }

    for (std::size_t i = 0; i < normalized.size(); ++i) {
        if (normalized[i] != "subcommand" && normalized[i] != "subcommands") {
            continue;
        }
        const auto command = neighboring_command(normalized, i);
        auto terms = capability_terms(normalized, command, std::nullopt);
        if (!command || terms.empty()) {
            return std::nullopt;
        }
        return ActionTarget{
            .kind = ActionTargetKind::Subcommand,
            .command = command,
            .literal = std::nullopt,
            .terms = std::move(terms),
            .explicit_syntax = false,
        };
    }

    // Humans rarely name the syntax category they need. When a sentence starts
    // by addressing a plausible command ("rg is ...", "curl keeps ...",
    // "systemctl show ..."), preserve that command as an entity anchor and
    // describe the requested behavior as an Operation. Verified grammar later
    // decides whether the operation is implemented by an option or subcommand.
    if (const auto command = leading_command_hint(query)) {
        auto terms = implicit_operation_terms(query, *command);
        if (!terms.empty()) {
            return ActionTarget{
                .kind = ActionTargetKind::Operation,
                .command = command,
                .literal = std::nullopt,
                .terms = std::move(terms),
                .explicit_syntax = false,
            };
        }
    }

    // The actionable architecture begins after ranking: a human does not need
    // to know the command name first. For strong operation phrases with concrete
    // user data, preserve a command-agnostic Operation target and let the already
    // ranked top candidate supply the parent command. This intentionally does
    // not rewrite ranking or make vague discovery queries pay syntax cost.
    if (query.frame.frame == QueryFrame::Modify ||
        (query.frame.frame == QueryFrame::Discover && has_concrete_binding_literal(query))) {
        auto terms = implicit_operation_terms(query, "");
        if (!terms.empty()) {
            return ActionTarget{
                .kind = ActionTargetKind::Operation,
                .command = std::nullopt,
                .literal = std::nullopt,
                .terms = std::move(terms),
                .explicit_syntax = false,
            };
        }
    }

    return std::nullopt;
}

} // namespace acclorite::query
