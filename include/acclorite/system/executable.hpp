#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace acclorite::system {

[[nodiscard]] std::vector<std::filesystem::path> path_directories();
[[nodiscard]] std::optional<std::filesystem::path> find_executable(std::string_view name);

} // namespace acclorite::system
