#pragma once

#include <string>
#include <string_view>

#include "acclorite/core/actionable.hpp"

namespace acclorite::output {

[[nodiscard]] std::string shell_quote(std::string_view value);
[[nodiscard]] std::string render_shell_invocation(const CommandInvocation& invocation);

} // namespace acclorite::output
