#pragma once

#include <string>
#include <vector>

#include "acclorite/core/query_frame.hpp"

namespace acclorite {

struct Query {
    std::string raw;
    std::string normalized;
    std::vector<std::string> tokens;
    QueryFrameResult frame;
    std::vector<std::string> targets;
    bool explain_ranking{false};

    static Query parse(std::string raw_query);
};

} // namespace acclorite
