#pragma once

#include <filesystem>
#include <vector>

#include "acclorite/syntax/provider.hpp"

namespace acclorite {

class FishCompletionSyntaxProvider final : public CommandSyntaxProvider {
public:
    FishCompletionSyntaxProvider();
    explicit FishCompletionSyntaxProvider(std::vector<std::filesystem::path> roots);

    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::string_view diagnostic_name() const override { return "fish-completion"; }
    [[nodiscard]] std::optional<CommandGrammar> grammar(const Candidate& candidate) const override;
    [[nodiscard]] std::optional<SubcommandSpec> subcommand_grammar(
        const Candidate& candidate,
        const SubcommandSpec& subcommand
    ) const override;

private:
    std::vector<std::filesystem::path> roots_;
};

} // namespace acclorite
