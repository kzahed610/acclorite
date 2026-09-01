#pragma once

#include <string_view>
#include <vector>

#include "acclorite/core/query.hpp"

namespace acclorite::query {

struct RelevanceMatch {
    double score{0.0};
    std::vector<std::string> matched_terms;
};

[[nodiscard]] RelevanceMatch score_text(
    const Query& query,
    std::string_view name,
    std::string_view description,
    double exact_name_score = 0.99,
    double full_match_ceiling = 0.96
);

} // namespace acclorite::query
