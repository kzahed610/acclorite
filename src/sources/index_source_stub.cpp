#include "acclorite/sources/index_source.hpp"

#include <cstdlib>

namespace acclorite {

std::filesystem::path IndexSource::database_path() {
    if (const char* override_path = std::getenv("ACCLORITE_INDEX_PATH")) {
        return override_path;
    }
    if (const char* cache_home = std::getenv("XDG_CACHE_HOME")) {
        return std::filesystem::path(cache_home) / "acclorite/index.db";
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".cache/acclorite/index.db";
    }
    return std::filesystem::temp_directory_path() / "acclorite-index.db";
}

IndexStatus IndexSource::probe() {
    const auto path = database_path();
    std::error_code error;
    const bool exists = std::filesystem::is_regular_file(path, error);
    std::uintmax_t size = 0;
    if (exists && !error) {
        size = std::filesystem::file_size(path, error);
        if (error) {
            size = 0;
        }
    }
    return IndexStatus{
        .sqlite_supported = false,
        .exists = exists,
        .ready = false,
        .fingerprinted = false,
        .stale = false,
        .path = path,
        .size_bytes = size,
        .stale_sources = {},
    };
}

bool IndexSource::rebuild() const { return false; }
bool IndexSource::ensure_ready() const { return false; }
bool IndexSource::available() const { return false; }
std::vector<Candidate> IndexSource::search(const Query&) const { return {}; }

} // namespace acclorite
