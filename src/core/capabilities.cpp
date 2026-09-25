#include "acclorite/core/capabilities.hpp"

#include "acclorite/guidance/curated_provider.hpp"
#include "acclorite/guidance/info_provider.hpp"
#include "acclorite/guidance/man_provider.hpp"
#include "acclorite/guidance/tldr_provider.hpp"
#include "acclorite/syntax/man_provider.hpp"
#include "acclorite/syntax/fish_completion_provider.hpp"
#include "acclorite/sources/apt_source.hpp"
#include "acclorite/sources/dnf_source.hpp"
#include "acclorite/sources/pacman_source.hpp"
#include "acclorite/sources/zypper_source.hpp"
#include "acclorite/sources/xbps_source.hpp"
#include "acclorite/sources/pkgfile_enricher.hpp"
#include "acclorite/system/distro.hpp"

#ifndef ACCLORITE_HAS_SQLITE
#define ACCLORITE_HAS_SQLITE 0
#endif

namespace acclorite {

CapabilityReport detect_capabilities() {
    const bool arch_available = PacmanSource{}.available();
    const bool apt_available = AptSource{}.available();
    const bool dnf_available = DnfSource{}.available();
    const bool zypper_available = ZypperSource{}.available();
    const bool xbps_available = XbpsSource{}.available();
    const auto family = system::choose_package_family(
        system::detect_distro(),
        system::PackageBackendAvailability{
            .arch = arch_available,
            .debian = apt_available,
            .fedora = dnf_available,
            .suse = zypper_available,
            .void_linux = xbps_available,
        }
    );

    return CapabilityReport{
        .sqlite_fts = ACCLORITE_HAS_SQLITE != 0,
        .manual_guidance = ManGuidanceProvider{}.available(),
        .manual_syntax = ManCommandSyntaxProvider{}.available(),
        .fish_completion_syntax = FishCompletionSyntaxProvider{}.available(),
        .info_guidance = InfoGuidanceProvider{}.available(),
        .tldr_cache = TldrGuidanceProvider{}.available(),
        .curated_guidance = CuratedGuidanceProvider{}.available(),
        .arch_packages = family == PackageFamily::Arch && arch_available,
        .apt_packages = family == PackageFamily::Debian && apt_available,
        .dnf_packages = family == PackageFamily::Fedora && dnf_available,
        .zypper_packages = family == PackageFamily::Suse && zypper_available,
        .xbps_packages = family == PackageFamily::Void && xbps_available,
        .pkgfile = family == PackageFamily::Arch && PkgfileEnricher{}.available(),
    };
}

} // namespace acclorite
