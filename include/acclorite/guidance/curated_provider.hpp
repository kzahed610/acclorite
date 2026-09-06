#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "acclorite/guidance/provider.hpp"

namespace acclorite {

class CuratedGuidanceProvider final : public GuidanceProvider {
public:
    explicit CuratedGuidanceProvider(
        std::filesystem::path metadata_path = {},
        std::string verified_by = {}
    );

    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::string_view diagnostic_name() const override { return "curated"; }
    [[nodiscard]] GuidanceBundle guide(const Candidate& candidate) const override;

    [[nodiscard]] const std::filesystem::path& metadata_path() const { return metadata_path_; }

private:
    struct Entry {
        enum class Kind { Example, Resource };

        Kind kind{Kind::Resource};
        std::string id;
        std::string label;
        std::string value;
        std::string source_reference;
        std::string verified_on;
    };

    std::filesystem::path metadata_path_;
    std::string verified_by_;
    std::unordered_map<std::string, std::vector<Entry>> entries_;
};

} // namespace acclorite
