#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "acclorite/sources/package_backend.hpp"

namespace acclorite {

class XbpsSource final : public PackageBackend {
public:
    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::string_view diagnostic_name() const override { return "xbps-packages"; }
    [[nodiscard]] PackageFamily package_family() const override { return PackageFamily::Void; }
    [[nodiscard]] std::string_view backend_id() const override { return "xbps"; }
    [[nodiscard]] std::vector<Candidate> search(const Query& query) const override;

    // Read-only Doctor probes. xbps-query uses synchronized on-disk repository
    // indexes unless --memory-sync/-M is explicitly requested; Acclorite never
    // requests that mode and never invokes xbps-install.
    [[nodiscard]] static bool cached_metadata_ready();
    [[nodiscard]] static bool installed_metadata_ready();

private:
    struct PackageEntry {
        std::string pkgver;
        std::string package;
        std::string version;
        std::string description;
        bool installed{false};
    };

    [[nodiscard]] static std::string search_pattern(const Query& query);
    [[nodiscard]] static std::vector<PackageEntry> parse_search(std::string_view output);
    static void decode_pkgvers(std::vector<PackageEntry>& entries);
};

} // namespace acclorite
