#pragma once

namespace acclorite {

struct CapabilityReport {
    bool sqlite_fts{false};
    bool manual_guidance{false};
    bool manual_syntax{false};
    bool fish_completion_syntax{false};
    bool info_guidance{false};
    bool tldr_cache{false};
    bool curated_guidance{false};
    bool arch_packages{false};
    bool apt_packages{false};
    bool dnf_packages{false};
    bool zypper_packages{false};
    bool xbps_packages{false};
    bool pkgfile{false};
};

// Cheap, local-only feature detection for machine clients. This function must
// not spawn subprocesses, access the network, rebuild caches, or mutate state.
[[nodiscard]] CapabilityReport detect_capabilities();

} // namespace acclorite
