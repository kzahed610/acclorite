#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "acclorite/core/query.hpp"

namespace acclorite::query {

struct ConceptGroup {
    std::string term;
    std::vector<std::string> alternatives;
};

[[nodiscard]] bool is_stopword(std::string_view token);
[[nodiscard]] std::vector<std::string> meaningful_terms(const Query& query);
[[nodiscard]] std::vector<ConceptGroup> concept_groups(const Query& query);
[[nodiscard]] std::vector<std::string> retrieval_terms(
    const Query& query,
    std::size_t limit = 18
);

} // namespace acclorite::query
