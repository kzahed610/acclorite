#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "acclorite/sources/package_backend.hpp"

namespace acclorite {

class ZypperSource final : public PackageBackend {
public:
    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::string_view diagnostic_name() const override { return "zypper-packages"; }
    [[nodiscard]] PackageFamily package_family() const override { return PackageFamily::Suse; }
    [[nodiscard]] std::string_view backend_id() const override { return "zypper"; }
    [[nodiscard]] std::vector<Candidate> search(const Query& query) const override;

    // Local-only helpers shared with Doctor/tests. The config path resolves to
    // Acclorite's bundled read-only Zypper configuration and never mutates user
    // or system package-manager configuration.
    [[nodiscard]] static std::optional<std::filesystem::path> readonly_config();
    [[nodiscard]] static bool cached_metadata_ready();

private:
    struct PackageEntry {
        std::string package;
        std::string version;
        std::string repository;
        std::string description;
        std::string status;
        bool installed{false};
    };

    [[nodiscard]] static std::vector<std::string> search_terms(const Query& query);
    [[nodiscard]] static std::vector<PackageEntry> parse_search_xml(std::string_view output);
    [[nodiscard]] static std::unordered_map<std::string, PackageEntry> repository_details(
        const std::vector<Candidate>& candidates
    );
    [[nodiscard]] static std::unordered_map<std::string, std::string> installed_versions(
        const std::vector<Candidate>& candidates
    );
    [[nodiscard]] static std::vector<std::string> base_arguments();
};

} // namespace acclorite
