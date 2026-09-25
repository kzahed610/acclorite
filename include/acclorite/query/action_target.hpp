#pragma once

#include <optional>

#include "acclorite/core/action_target.hpp"
#include "acclorite/core/query.hpp"

namespace acclorite::query {

[[nodiscard]] std::optional<ActionTarget> detect_action_target(const Query& query);

} // namespace acclorite::query
