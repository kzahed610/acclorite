#pragma once

#include <vector>

#include "acclorite/core/candidate.hpp"
#include "acclorite/core/confidence.hpp"
#include "acclorite/core/query.hpp"

namespace acclorite::ranking {

struct ConfidenceAssessment {
    Confidence confidence;
    std::vector<ClarificationOption> clarifications;
};

// Confidence is observational: it consumes the already-ranked candidate list and
// must never change candidate order, scores, or retrieval behavior.
[[nodiscard]] ConfidenceAssessment assess_confidence(
    const Query& query,
    const std::vector<Candidate>& ranked_candidates
);

} // namespace acclorite::ranking
