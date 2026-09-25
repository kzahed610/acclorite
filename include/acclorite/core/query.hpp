#pragma once

#include <optional>
#include <string>
#include <vector>

#include "acclorite/core/action_target.hpp"
#include "acclorite/core/query_frame.hpp"

namespace acclorite {

struct Query {
    std::string raw;
    std::string normalized;
    std::vector<std::string> tokens;
    QueryFrameResult frame;
    std::vector<std::string> targets;
    std::optional<ActionTarget> action_target;
    bool explain_ranking{false};

    static Query parse(std::string raw_query);
};

} // namespace acclorite
