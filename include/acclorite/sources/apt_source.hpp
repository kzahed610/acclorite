#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "acclorite/sources/package_backend.hpp"

namespace acclorite {

class AptSource final : public PackageBackend {
public:
    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::string_view diagnostic_name() const override { return "apt-packages"; }
    [[nodiscard]] PackageFamily package_family() const override { return PackageFamily::Debian; }
    [[nodiscard]] std::string_view backend_id() const override { return "apt"; }
    [[nodiscard]] std::vector<Candidate> search(const Query& query) const override;

private:
    struct PackageEntry {
        std::string package;
        std::string version;
        std::string description;
        bool installed{false};
    };

    [[nodiscard]] static std::vector<std::string> search_patterns(const Query& query);
    [[nodiscard]] static std::vector<PackageEntry> parse_search(std::string_view output);
    [[nodiscard]] static std::unordered_map<std::string, std::string> installed_packages();
    [[nodiscard]] static std::unordered_map<std::string, std::string> available_versions(
        const std::vector<Candidate>& candidates
    );
};

} // namespace acclorite
