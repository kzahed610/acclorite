#pragma once

#include <string>
#include <vector>

#include "acclorite/core/candidate.hpp"

namespace acclorite {

struct SearchResult {
    std::string raw_query;
    std::string normalized_query;
    std::vector<Candidate> candidates;
};

} // namespace acclorite
