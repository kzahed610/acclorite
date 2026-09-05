#pragma once

#include <filesystem>

#include "acclorite/sources/location_source.hpp"

namespace acclorite {

class FilesystemLocationSource final : public LocationSource {
public:
    explicit FilesystemLocationSource(
        std::filesystem::path system_config_root = "/etc"
    );

    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::vector<LocationHit> search(const Query& query) const override;

private:
    std::filesystem::path system_config_root_;
};

} // namespace acclorite
