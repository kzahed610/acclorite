#pragma once

#include <string_view>

namespace acclorite::query::fuzzy {

// Normalized Optimal String Alignment similarity in the range [0, 1].
// 1.0 means exact equality. Adjacent transpositions count as one edit.
[[nodiscard]] double similarity(std::string_view lhs, std::string_view rhs);

[[nodiscard]] bool similar(
    std::string_view lhs,
    std::string_view rhs,
    double threshold = 0.82
);

} // namespace acclorite::query::fuzzy
