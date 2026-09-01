#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace acclorite::system {

struct ProcessResult {
    int exit_code{-1};
    std::string stdout_text;
};

[[nodiscard]] ProcessResult run_capture_stdout(
    const std::vector<std::string>& arguments,
    std::size_t max_output_bytes = 2 * 1024 * 1024
);

} // namespace acclorite::system
