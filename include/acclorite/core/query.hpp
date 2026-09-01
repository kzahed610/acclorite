#pragma once

#include <string>
#include <vector>

namespace acclorite {

struct Query {
    std::string raw;
    std::string normalized;
    std::vector<std::string> tokens;

    static Query parse(std::string raw_query);
};

} // namespace acclorite
