#pragma once

#include "acclorite/core/query.hpp"
#include "acclorite/core/query_frame.hpp"

namespace acclorite::query {

[[nodiscard]] QueryFrameResult recognize_frame(const Query& query);

} // namespace acclorite::query
