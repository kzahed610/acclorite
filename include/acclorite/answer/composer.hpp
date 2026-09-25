#pragma once

#include <functional>
#include <optional>

#include "acclorite/core/actionable.hpp"
#include "acclorite/core/candidate.hpp"
#include "acclorite/core/query.hpp"

namespace acclorite {

class AnswerComposer {
public:
    using SubcommandGrammarResolver = std::function<std::optional<SubcommandSpec>(const SubcommandSpec&)>;

    [[nodiscard]] static bool may_compose(const Query& query);
    [[nodiscard]] static std::optional<ActionableAnswer> compose(
        const Query& query,
        const Candidate& candidate,
        const CommandGrammar& grammar,
        const SubcommandGrammarResolver& subcommand_resolver = {}
    );
};

} // namespace acclorite
