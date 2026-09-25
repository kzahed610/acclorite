#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "acclorite/sources/package_backend.hpp"

namespace acclorite {

class DnfSource final : public PackageBackend {
public:
    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::string_view diagnostic_name() const override { return "dnf-packages"; }
    [[nodiscard]] PackageFamily package_family() const override { return PackageFamily::Fedora; }
    [[nodiscard]] std::string_view backend_id() const override { return "dnf"; }
    [[nodiscard]] std::vector<Candidate> search(const Query& query) const override;

    // Exposed for Doctor/tests without running a package search. Detection is
    // local PATH inspection only; DNF5 is preferred when both frontends exist.
    [[nodiscard]] static std::optional<std::string> frontend();

private:
    struct PackageEntry {
        std::string package;
        std::string version;
        std::string repository;
        std::string description;
        bool installed{false};
    };

    [[nodiscard]] static std::vector<std::string> search_terms(const Query& query);
    [[nodiscard]] static std::vector<PackageEntry> parse_search(std::string_view output);
    [[nodiscard]] static std::unordered_map<std::string, PackageEntry> repository_details(
        std::string_view frontend,
        const std::vector<PackageEntry>& entries
    );
    [[nodiscard]] static std::unordered_map<std::string, std::string> installed_versions(
        const std::vector<PackageEntry>& entries
    );
};

} // namespace acclorite
