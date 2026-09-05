#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace acclorite {

enum class QueryFrame {
    Unknown,
    Discover,
    Explain,
    Locate,
    Inspect,
    Modify,
    Compare,
    Diagnose,
};

struct QueryFrameResult {
    QueryFrame frame{QueryFrame::Unknown};
    double confidence{0.0};
    bool explicit_frame{false};
    std::vector<std::string> signals;
};

[[nodiscard]] std::string_view query_frame_name(QueryFrame frame);

} // namespace acclorite
