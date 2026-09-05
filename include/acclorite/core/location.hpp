#pragma once

#include <string>

namespace acclorite {

struct LocationHit {
    std::string path;
    std::string kind;
    std::string source;
    double score{0.0};
};

} // namespace acclorite
