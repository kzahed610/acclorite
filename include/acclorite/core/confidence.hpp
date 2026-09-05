#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace acclorite {

enum class AmbiguityState {
    Clear,
    Competitive,
    Ambiguous,
    LowConfidence,
    NoResult,
};

struct Confidence {
    double top_candidate{0.0};
    double interpretation{0.0};
    double separation{0.0};
    double semantic_gap{0.0};
    double utility_gap{0.0};
    AmbiguityState ambiguity{AmbiguityState::NoResult};
    std::vector<std::string> signals;
};

struct ClarificationOption {
    std::string id;
    std::string label;
};

[[nodiscard]] std::string_view ambiguity_state_name(AmbiguityState state);
[[nodiscard]] std::string_view confidence_level_name(double confidence);

} // namespace acclorite
