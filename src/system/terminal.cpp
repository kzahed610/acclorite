#include "acclorite/system/terminal.hpp"

#include <cstdlib>
#include <cstring>

#include <unistd.h>

namespace acclorite::system {
namespace {

bool stream_supports_color(const int fd) noexcept {
    if (std::getenv("NO_COLOR") != nullptr) {
        return false;
    }
    if (const char* term = std::getenv("TERM"); term != nullptr && std::strcmp(term, "dumb") == 0) {
        return false;
    }
    return ::isatty(fd) == 1;
}

} // namespace

bool stdout_supports_color() noexcept {
    return stream_supports_color(STDOUT_FILENO);
}

bool stderr_supports_color() noexcept {
    return stream_supports_color(STDERR_FILENO);
}

} // namespace acclorite::system
