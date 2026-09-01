#pragma once

#include <string>
#include <vector>

namespace acclorite {

struct Candidate {
    std::string command;
    std::string path;
    std::string summary;
    std::string source;

    bool installed{false};
    bool repository_available{false};

    std::vector<std::string> matched_terms;
    double score{0.0};
};

} // namespace acclorite
