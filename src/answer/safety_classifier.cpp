#include "acclorite/answer/safety_classifier.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace acclorite {
namespace {

std::string lower_words(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool previous_space = true;
    for (const unsigned char ch : text) {
        if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::tolower(ch)));
            previous_space = false;
        } else if (!previous_space) {
            out.push_back(' ');
            previous_space = true;
        }
    }
    if (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

std::unordered_set<std::string> words(std::string_view text) {
    const std::string normalized = lower_words(text);
    std::unordered_set<std::string> result;
    std::size_t start = 0;
    while (start < normalized.size()) {
        const auto end = normalized.find(' ', start);
        const auto count = end == std::string::npos ? normalized.size() - start : end - start;
        if (count > 0) {
            result.emplace(normalized.substr(start, count));
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return result;
}

bool contains_any(
    const std::unordered_set<std::string>& haystack,
    std::initializer_list<std::string_view> needles
) {
    return std::ranges::any_of(needles, [&](const std::string_view needle) {
        return haystack.contains(std::string(needle));
    });
}

bool contains_phrase(std::string_view text, std::initializer_list<std::string_view> phrases) {
    const std::string normalized = lower_words(text);
    return std::ranges::any_of(phrases, [&](const std::string_view phrase) {
        return normalized.find(std::string(phrase)) != std::string::npos;
    });
}

std::unordered_set<std::string> intent_words(
    const Query& query,
    const ActionableAnswer& answer
) {
    std::unordered_set<std::string> result;
    if (query.action_target) {
        for (const auto& term : query.action_target->terms) {
            const auto split = words(term);
            result.insert(split.begin(), split.end());
        }
    }
    for (const auto& option : answer.relevant_options) {
        for (const auto& name : option.names) {
            const auto split = words(name);
            result.insert(split.begin(), split.end());
        }
    }
    for (const auto& subcommand : answer.relevant_subcommands) {
        const auto split = words(subcommand.name);
        result.insert(split.begin(), split.end());
    }
    return result;
}

std::string specific_evidence(const Candidate& candidate, const ActionableAnswer& answer) {
    std::string evidence;
    const auto append = [&](const std::string& text) {
        if (text.empty()) return;
        if (!evidence.empty()) evidence.push_back(' ');
        evidence += text;
    };

    bool has_specific = false;
    for (const auto& option : answer.relevant_options) {
        if (!option.description.empty()) {
            append(option.description);
            has_specific = true;
        }
    }
    for (const auto& subcommand : answer.relevant_subcommands) {
        if (!subcommand.description.empty()) {
            append(subcommand.description);
            has_specific = true;
        }
    }

    if (!has_specific) {
        append(candidate.summary);
    }
    return evidence;
}

bool invocation_is_concrete_and_complete(const ActionableAnswer& answer) {
    if (!answer.invocation || !answer.invocation->complete) {
        return false;
    }
    return std::ranges::none_of(answer.invocation->arguments, [](const InvocationArgument& argument) {
        return argument.placeholder;
    });
}

bool invocation_has_network_locator(const ActionableAnswer& answer) {
    if (!answer.invocation) return false;
    return std::ranges::any_of(answer.invocation->arguments, [](const InvocationArgument& argument) {
        const std::string lowered = lower_words(argument.value);
        return lowered.starts_with("http ") || lowered == "http" ||
               lowered.starts_with("https ") || lowered == "https" ||
               argument.value.find("://") != std::string::npos;
    });
}

} // namespace

ActionSafety SafetyClassifier::classify(
    const Query& query,
    const Candidate& candidate,
    const ActionableAnswer& answer
) {
    // Safety labels describe a concrete copyable action. An explanation-only
    // answer, an incomplete template, or one containing placeholders does not
    // establish enough behavior to classify without guessing.
    if (!invocation_is_concrete_and_complete(answer)) {
        return ActionSafety::Unknown;
    }

    const std::string evidence = specific_evidence(candidate, answer);
    if (evidence.empty()) {
        return ActionSafety::Unknown;
    }

    const auto evidence_words = words(evidence);
    const auto intent = intent_words(query, answer);

    const bool destructive_intent = contains_any(intent, {
        "remove", "delete", "erase", "destroy", "wipe", "purge", "unlink", "truncate", "shred"
    });
    const bool destructive_evidence = contains_any(evidence_words, {
        "remove", "delete", "deletes", "deleting", "erase", "destroy", "wipe", "purge", "unlink", "truncate", "shred"
    });
    if (destructive_intent && destructive_evidence) {
        return ActionSafety::Destructive;
    }

    // Privilege is only claimed from explicit source-backed wording. Generic
    // words like "system" or a scary command name are never sufficient.
    if (contains_phrase(evidence, {
            "requires root", "require root", "root privileges", "root privilege",
            "superuser privileges", "superuser privilege", "administrator privileges",
            "administrator privilege", "must be root", "run as root"
        })) {
        return ActionSafety::Privileged;
    }

    const bool network_intent = contains_any(intent, {
        "network", "remote", "download", "upload", "fetch", "connect", "request", "send", "receive", "transfer"
    });
    const bool network_evidence = contains_any(evidence_words, {
        "network", "remote", "download", "upload", "fetch", "connect", "request", "transfer", "url", "http", "https", "server"
    });
    if (network_evidence && (network_intent || invocation_has_network_locator(answer))) {
        return ActionSafety::Network;
    }

    const bool mutating_intent = contains_any(intent, {
        "create", "copy", "move", "rename", "write", "output", "change", "modify", "update", "set", "switch", "make"
    });
    const bool mutating_evidence = contains_any(evidence_words, {
        "create", "creates", "created", "copy", "copies", "move", "moves", "rename", "renames",
        "write", "writes", "change", "changes", "modify", "modifies", "update", "updates", "set", "sets",
        "switch", "switches", "make", "makes"
    });
    if (mutating_intent && mutating_evidence) {
        return ActionSafety::Mutating;
    }

    const bool readonly_intent = contains_any(intent, {
        "search", "find", "show", "status", "list", "display", "print", "read", "inspect", "report", "query", "compare", "check", "locate"
    });
    const bool readonly_evidence = contains_any(evidence_words, {
        "search", "searches", "find", "finds", "show", "shows", "status", "list", "lists", "display", "displays",
        "print", "prints", "read", "reads", "inspect", "report", "reports", "query", "queries", "compare", "check",
        "match", "matches", "information"
    });
    if (readonly_intent && readonly_evidence && !mutating_evidence && !destructive_evidence) {
        return ActionSafety::ReadOnly;
    }

    return ActionSafety::Unknown;
}

} // namespace acclorite
