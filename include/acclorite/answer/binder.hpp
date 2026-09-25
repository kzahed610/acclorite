#pragma once

#include <optional>

#include "acclorite/core/actionable.hpp"
#include "acclorite/core/query.hpp"

namespace acclorite {

// Deterministically maps user-provided literals into already-verified grammar.
// It never invents values or syntax and never executes the target command.
class ArgumentBinder {
public:
    [[nodiscard]] static std::optional<CommandInvocation> bind_root(
        const Query& query,
        const CommandGrammar& grammar
    );

    [[nodiscard]] static std::optional<CommandInvocation> bind_option(
        const Query& query,
        const CommandGrammar& grammar,
        const CommandOption& option
    );

    [[nodiscard]] static std::optional<CommandInvocation> bind_root_option(
        const Query& query,
        const CommandGrammar& grammar,
        const CommandOption& option
    );

    [[nodiscard]] static std::optional<CommandInvocation> bind_subcommand(
        const Query& query,
        const CommandGrammar& grammar,
        const SubcommandSpec& subcommand
    );

    [[nodiscard]] static std::optional<CommandInvocation> bind_subcommand_option(
        const Query& query,
        const CommandGrammar& grammar,
        const SubcommandSpec& subcommand,
        const CommandOption& option
    );
};

} // namespace acclorite
