#include "acclorite/syntax/grammar_merge.hpp"

#include <algorithm>
#include <ranges>
#include <string>

namespace acclorite {
namespace {

bool same_option_identity(const CommandOption& lhs, const CommandOption& rhs) {
    return std::ranges::any_of(lhs.names, [&](const std::string& lhs_name) {
        return std::ranges::find(rhs.names, lhs_name) != rhs.names.end();
    });
}

bool incoming_option_is_structurally_stronger(
    const CommandOption& existing,
    const CommandOption& incoming
) {
    return !existing.value_shape_known && incoming.value_shape_known;
}

void merge_options(std::vector<CommandOption>& target, const std::vector<CommandOption>& incoming) {
    for (const auto& option : incoming) {
        auto existing = std::ranges::find_if(target, [&](const CommandOption& current) {
            return same_option_identity(current, option);
        });
        if (existing == target.end()) {
            target.push_back(option);
            continue;
        }

        // A lower-priority source may replace an arity-unknown fact with a fact
        // that actually proves the option's value shape. The replacement is
        // whole-fact, so the surviving provenance remains truthful. Otherwise
        // the earlier provider keeps authority, including on conflicts.
        if (incoming_option_is_structurally_stronger(*existing, option)) {
            *existing = option;
        }
    }
}

void merge_synopsis(
    std::vector<SynopsisAlternative>& target,
    const std::vector<SynopsisAlternative>& incoming
) {
    for (const auto& alternative : incoming) {
        const auto duplicate = std::ranges::find_if(target, [&](const SynopsisAlternative& existing) {
            return existing.text == alternative.text;
        });
        if (duplicate == target.end()) {
            target.push_back(alternative);
        }
    }
}

} // namespace

void merge_subcommand_grammar(SubcommandSpec& target, const SubcommandSpec& incoming) {
    if (target.name.empty()) {
        target = incoming;
        return;
    }
    if (incoming.name.empty() || target.name != incoming.name) {
        return;
    }

    merge_options(target.options, incoming.options);
    merge_synopsis(target.synopsis, incoming.synopsis);

    // The flat positional model cannot safely represent contradictory layouts
    // from separate sources. Keep the higher-priority sequence unless it is
    // absent; identical lower-priority sequences need no additional merge.
    if (target.positionals.empty() && !incoming.positionals.empty()) {
        target.positionals = incoming.positionals;
    }
}

void merge_command_grammar(CommandGrammar& target, const CommandGrammar& incoming) {
    if (incoming.command.empty()) {
        return;
    }
    if (target.command.empty()) {
        target.command = incoming.command;
    }
    if (target.command != incoming.command) {
        return;
    }

    merge_options(target.global_options, incoming.global_options);
    merge_synopsis(target.synopsis, incoming.synopsis);

    if (target.positionals.empty() && !incoming.positionals.empty()) {
        target.positionals = incoming.positionals;
    }

    for (const auto& subcommand : incoming.subcommands) {
        auto existing = std::ranges::find_if(target.subcommands, [&](const SubcommandSpec& current) {
            return current.name == subcommand.name;
        });
        if (existing == target.subcommands.end()) {
            target.subcommands.push_back(subcommand);
            continue;
        }
        merge_subcommand_grammar(*existing, subcommand);
    }
}

} // namespace acclorite
