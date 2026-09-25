#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace acclorite {

enum class PackageFamily {
    Unknown,
    Arch,
    Debian,
    Fedora,
    Suse,
    Void,
    Alpine,
};

[[nodiscard]] constexpr std::string_view package_family_name(const PackageFamily family) {
    switch (family) {
    case PackageFamily::Arch: return "arch";
    case PackageFamily::Debian: return "debian";
    case PackageFamily::Fedora: return "fedora";
    case PackageFamily::Suse: return "suse";
    case PackageFamily::Void: return "void";
    case PackageFamily::Alpine: return "alpine";
    case PackageFamily::Unknown: return "unknown";
    }
    return "unknown";
}

namespace system {

struct DistroInfo {
    std::string id;
    std::vector<std::string> id_like;
    PackageFamily family{PackageFamily::Unknown};
};

// Reads os-release locally. ACCLORITE_OS_RELEASE may point at a fixture/alternate
// file; no subprocesses or network access are involved.
[[nodiscard]] DistroInfo detect_distro();
[[nodiscard]] DistroInfo detect_distro(const std::filesystem::path& os_release_path);

struct PackageBackendAvailability {
    bool arch{false};
    bool debian{false};
    bool fedora{false};
    bool suse{false};
    bool void_linux{false};
};

// Prefer the distro-declared family when multiple package managers coexist. If
// exactly one supported backend is available, use it as a conservative fallback
// for containers/chroots with incomplete os-release metadata.
[[nodiscard]] PackageFamily choose_package_family(
    const DistroInfo& distro,
    const PackageBackendAvailability& available
);

} // namespace system
} // namespace acclorite
