#include "acclorite/core/capabilities.hpp"

#include "acclorite/guidance/curated_provider.hpp"
#include "acclorite/guidance/info_provider.hpp"
#include "acclorite/guidance/man_provider.hpp"
#include "acclorite/guidance/tldr_provider.hpp"
#include "acclorite/sources/pacman_source.hpp"
#include "acclorite/sources/pkgfile_enricher.hpp"

#ifndef ACCLORITE_HAS_SQLITE
#define ACCLORITE_HAS_SQLITE 0
#endif

namespace acclorite {

CapabilityReport detect_capabilities() {
    return CapabilityReport{
        .sqlite_fts = ACCLORITE_HAS_SQLITE != 0,
        .manual_guidance = ManGuidanceProvider{}.available(),
        .info_guidance = InfoGuidanceProvider{}.available(),
        .tldr_cache = TldrGuidanceProvider{}.available(),
        .curated_guidance = CuratedGuidanceProvider{}.available(),
        .arch_packages = PacmanSource{}.available(),
        .pkgfile = PkgfileEnricher{}.available(),
    };
}

} // namespace acclorite
