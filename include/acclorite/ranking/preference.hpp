#pragma once

#include "acclorite/core/candidate.hpp"
#include "acclorite/core/query.hpp"
#include "acclorite/ranking/trace.hpp"

namespace acclorite::ranking {

// Semantic fit is the primary intent-match signal. Recommendation preferences
// may decide within a fit tier, but cannot promote a weaker semantic tier above
// a clearly more precise candidate. Query-aware fit additionally discounts
// unrequested domain specialization (PDF-only, archive-only, VCS-only, etc.).
struct SemanticFitAssessment {
    double raw_fit{0.0};
    double effective_fit{0.0};
    std::vector<RankingAdjustment> adjustments;
};

[[nodiscard]] double effective_semantic_fit(const Candidate& candidate);
[[nodiscard]] double effective_semantic_fit(const Query& query, const Candidate& candidate);
[[nodiscard]] SemanticFitAssessment assess_semantic_fit(
    const Query& query,
    const Candidate& candidate
);
[[nodiscard]] int semantic_fit_tier(double fit);
[[nodiscard]] const char* semantic_fit_tier_name(int tier);

// Apply small, deterministic recommendation priors after semantic retrieval.
// These are tie-breakers only: strong semantic evidence must always dominate.
[[nodiscard]] RankingBreakdown explain_preference_prior(
    const Query& query,
    const Candidate& candidate,
    double score
);

[[nodiscard]] double apply_preference_prior_utility(
    const Query& query,
    const Candidate& candidate,
    double score
);

[[nodiscard]] double apply_preference_prior(
    const Query& query,
    const Candidate& candidate,
    double score
);

} // namespace acclorite::ranking
