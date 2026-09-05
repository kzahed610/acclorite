#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "acclorite/query/relevance.hpp"
#include "acclorite/ranking/trace.hpp"

namespace acclorite::ranking {

[[nodiscard]] inline SourceEvidenceTrace semantic_evidence(
    std::string source,
    std::string method,
    const query::RelevanceMatch& relevance,
    std::vector<RankingAdjustment> adjustments,
    const double final_score
) {
    return SourceEvidenceTrace{
        .source = std::move(source),
        .method = std::move(method),
        .semantic = relevance.breakdown,
        .adjustments = std::move(adjustments),
        .final_score = final_score,
    };
}

[[nodiscard]] inline SourceEvidenceTrace opaque_evidence(
    std::string source,
    std::string method,
    const double final_score
) {
    return SourceEvidenceTrace{
        .source = std::move(source),
        .method = std::move(method),
        .semantic = std::nullopt,
        .adjustments = {},
        .final_score = final_score,
    };
}

} // namespace acclorite::ranking
