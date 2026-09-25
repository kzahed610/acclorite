#pragma once

#include "acclorite/core/actionable.hpp"
#include "acclorite/core/candidate.hpp"
#include "acclorite/core/query.hpp"

namespace acclorite {

class SafetyClassifier {
public:
    [[nodiscard]] static ActionSafety classify(
        const Query& query,
        const Candidate& candidate,
        const ActionableAnswer& answer
    );
};

} // namespace acclorite
