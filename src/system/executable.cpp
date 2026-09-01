#include "acclorite/system/executable.hpp"

#include <cstdlib>
#include <string>

#include <unistd.h>

namespace acclorite::system {

std::vector<std::filesystem::path> path_directories() {
    std::vector<std::filesystem::path> directories;

    const char* raw_path = std::getenv("PATH");
    if (!raw_path) {
        return directories;
    }

    const std::string path(raw_path);
    std::size_t start = 0;

    while (start <= path.size()) {
        const std::size_t end = path.find(':', start);
        const std::string entry = path.substr(start, end - start);
        directories.emplace_back(entry.empty() ? "." : entry);

        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    return directories;
}

std::optional<std::filesystem::path> find_executable(const std::string_view name) {
    if (name.empty() || name.find('/') != std::string_view::npos) {
        return std::nullopt;
    }

    for (const auto& directory : path_directories()) {
        const auto candidate = directory / std::string(name);
        std::error_code error;
        if (!std::filesystem::is_regular_file(candidate, error) &&
            !std::filesystem::is_symlink(candidate, error)) {
            continue;
        }

        if (::access(candidate.c_str(), X_OK) == 0) {
            return candidate;
        }
    }

    return std::nullopt;
}

} // namespace acclorite::system
