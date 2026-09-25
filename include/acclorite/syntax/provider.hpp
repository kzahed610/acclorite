#pragma once

#include <optional>
#include <string_view>

#include "acclorite/core/actionable.hpp"
#include "acclorite/core/candidate.hpp"

namespace acclorite {

class CommandSyntaxProvider {
public:
    virtual ~CommandSyntaxProvider() = default;

    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual std::string_view diagnostic_name() const = 0;
    [[nodiscard]] virtual std::optional<CommandGrammar> grammar(const Candidate& candidate) const = 0;

    // Optional deeper grammar lookup for a subcommand already proven by trusted
    // root grammar. The proof may come from another provider; implementations
    // must treat the supplied identity only as authorization to inspect static/
    // documentary child evidence, never as permission to execute the target.
    [[nodiscard]] virtual std::optional<SubcommandSpec> subcommand_grammar(
        const Candidate&,
        const SubcommandSpec&
    ) const {
        return std::nullopt;
    }
};

} // namespace acclorite
