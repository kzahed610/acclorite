#pragma once

#include <string>
#include <vector>

#include "acclorite/core/candidate.hpp"
#include "acclorite/core/confidence.hpp"
#include "acclorite/core/location.hpp"
#include "acclorite/core/performance.hpp"
#include "acclorite/core/query_frame.hpp"

namespace acclorite {

struct SearchResult {
    std::string raw_query;
    std::string normalized_query;
    QueryFrameResult frame;
    std::vector<std::string> targets;
    std::vector<LocationHit> locations;
    std::vector<Candidate> candidates;
    Confidence confidence;
    std::vector<ClarificationOption> clarifications;
    SearchTiming timing;
};

} // namespace acclorite
