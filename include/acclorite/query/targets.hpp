#pragma once

#include <string>
#include <vector>

#include "acclorite/core/query.hpp"

namespace acclorite::query {

// Extract explicit named targets from frame-heavy human language. These are not
// semantic search concepts; they are concrete things the user appears to be
// referring to (for example `rg` and `grep` in a comparison query).
[[nodiscard]] std::vector<std::string> frame_targets(const Query& query);

} // namespace acclorite::query
