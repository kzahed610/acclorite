#pragma once

#include "acclorite/syntax/provider.hpp"

namespace acclorite {

class ManCommandSyntaxProvider final : public CommandSyntaxProvider {
public:
    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::string_view diagnostic_name() const override { return "man"; }
    [[nodiscard]] std::optional<CommandGrammar> grammar(const Candidate& candidate) const override;
    [[nodiscard]] std::optional<SubcommandSpec> subcommand_grammar(
        const Candidate& candidate,
        const SubcommandSpec& subcommand
    ) const override;
};

} // namespace acclorite
