#pragma once

namespace acclorite::system {

[[nodiscard]] bool stdout_is_terminal() noexcept;
[[nodiscard]] bool stdout_supports_color() noexcept;
[[nodiscard]] bool stderr_supports_color() noexcept;

} // namespace acclorite::system
