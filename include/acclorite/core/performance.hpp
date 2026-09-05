#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace acclorite {

struct TimingStage {
    std::string name;
    double milliseconds{0.0};
    std::size_t items{0};
};

struct SearchTiming {
    bool enabled{false};
    double total_ms{0.0};
    std::vector<TimingStage> stages;
};

} // namespace acclorite
