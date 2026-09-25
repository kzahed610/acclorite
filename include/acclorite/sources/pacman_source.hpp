#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "acclorite/sources/package_backend.hpp"

namespace acclorite {

class PacmanSource final : public PackageBackend {
public:
    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::string_view diagnostic_name() const override { return "arch-packages"; }
    [[nodiscard]] PackageFamily package_family() const override { return PackageFamily::Arch; }
    [[nodiscard]] std::string_view backend_id() const override { return "pacman"; }
    [[nodiscard]] std::vector<Candidate> search(const Query& query) const override;

private:
    struct PackageEntry {
        std::string repository;
        std::string package;
        std::string version;
        std::string description;
        bool installed{false};
        std::string source{"pacman"};
    };

    [[nodiscard]] static std::vector<std::string> search_patterns(const Query& query);
    [[nodiscard]] static std::vector<PackageEntry> parse_search(std::string_view output);
    [[nodiscard]] static std::vector<PackageEntry> parse_expac(std::string_view output);
    [[nodiscard]] static double score_entry(const Query& query, const PackageEntry& entry);

    // expac can enumerate the complete synchronized ALPM catalog in one read. Acclorite
    // persists that snapshot and fingerprints pacman's local sync databases so ordinary
    // queries do not respawn/reparse ALPM for every sentence.
    [[nodiscard]] static std::filesystem::path cache_path();
    [[nodiscard]] static std::string arch_catalog_fingerprint();
    [[nodiscard]] static std::optional<std::vector<PackageEntry>> load_cache(
        std::string_view fingerprint
    );
    [[nodiscard]] static std::optional<std::vector<PackageEntry>> rebuild_cache(
        std::string_view fingerprint
    );
    [[nodiscard]] static std::optional<std::vector<PackageEntry>> load_fast_cache(
        const Query& query,
        std::string_view fingerprint
    );
    [[nodiscard]] static std::optional<std::vector<PackageEntry>> rebuild_fast_cache(
        const Query& query,
        std::string_view fingerprint
    );
    [[nodiscard]] static std::vector<PackageEntry> filter_cached_entries(
        const Query& query,
        const std::vector<PackageEntry>& entries
    );
};

} // namespace acclorite
