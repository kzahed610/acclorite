#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace acclorite {

enum class ActionTargetKind {
    Option,
    Subcommand,
    Positional,
    Operation,
};

[[nodiscard]] inline std::string_view action_target_kind_name(const ActionTargetKind kind) {
    switch (kind) {
        case ActionTargetKind::Option: return "option";
        case ActionTargetKind::Subcommand: return "subcommand";
        case ActionTargetKind::Positional: return "positional";
        case ActionTargetKind::Operation: return "operation";
    }
    return "operation";
}

struct ActionTarget {
    ActionTargetKind kind{ActionTargetKind::Operation};
    std::optional<std::string> command;
    std::optional<std::string> literal;
    std::vector<std::string> terms;
    bool explicit_syntax{false};
};

} // namespace acclorite
