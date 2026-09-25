#include "acclorite/system/distro.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>

namespace acclorite::system {
namespace {

std::string trim(std::string_view input) {
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = input.find_last_not_of(" \t\r\n");
    return std::string(input.substr(first, last - first + 1));
}

std::string unquote(std::string value) {
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

std::string lower_ascii(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (const unsigned char ch : input) {
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

PackageFamily family_for_token(const std::string_view raw) {
    const std::string token = lower_ascii(raw);
    if (token == "arch" || token == "cachyos" || token == "manjaro" ||
        token == "endeavouros" || token == "garuda") {
        return PackageFamily::Arch;
    }
    if (token == "debian" || token == "ubuntu" || token == "linuxmint" ||
        token == "pop" || token == "elementary" || token == "kali" ||
        token == "neon") {
        return PackageFamily::Debian;
    }
    if (token == "fedora" || token == "rhel" || token == "centos" ||
        token == "rocky" || token == "almalinux") {
        return PackageFamily::Fedora;
    }
    if (token == "suse" || token == "opensuse" || token == "opensuse-tumbleweed" ||
        token == "opensuse-leap") {
        return PackageFamily::Suse;
    }
    if (token == "void") {
        return PackageFamily::Void;
    }
    if (token == "alpine") {
        return PackageFamily::Alpine;
    }
    return PackageFamily::Unknown;
}

PackageFamily detect_family(const std::string& id, const std::vector<std::string>& id_like) {
    if (const auto direct = family_for_token(id); direct != PackageFamily::Unknown) {
        return direct;
    }
    for (const auto& token : id_like) {
        if (const auto family = family_for_token(token); family != PackageFamily::Unknown) {
            return family;
        }
    }
    return PackageFamily::Unknown;
}

} // namespace

DistroInfo detect_distro(const std::filesystem::path& os_release_path) {
    DistroInfo info;
    std::ifstream file(os_release_path);
    std::string line;
    while (std::getline(file, line)) {
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line.resize(comment);
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = trim(std::string_view(line).substr(0, equals));
        const std::string value = unquote(trim(std::string_view(line).substr(equals + 1)));
        if (key == "ID") {
            info.id = lower_ascii(value);
        } else if (key == "ID_LIKE") {
            std::istringstream values(value);
            std::string token;
            while (values >> token) {
                info.id_like.push_back(lower_ascii(token));
            }
        }
    }
    info.family = detect_family(info.id, info.id_like);
    return info;
}

DistroInfo detect_distro() {
    if (const char* override_path = std::getenv("ACCLORITE_OS_RELEASE")) {
        return detect_distro(override_path);
    }
    return detect_distro("/etc/os-release");
}

PackageFamily choose_package_family(
    const DistroInfo& distro,
    const PackageBackendAvailability& available
) {
    const auto backend_available = [&](const PackageFamily family) {
        switch (family) {
        case PackageFamily::Arch: return available.arch;
        case PackageFamily::Debian: return available.debian;
        case PackageFamily::Fedora: return available.fedora;
        case PackageFamily::Suse: return available.suse;
        case PackageFamily::Void: return available.void_linux;
        case PackageFamily::Alpine:
        case PackageFamily::Unknown:
            return false;
        }
        return false;
    };

    std::size_t count = 0;
    PackageFamily only = PackageFamily::Unknown;
    for (const auto family : {PackageFamily::Arch, PackageFamily::Debian, PackageFamily::Fedora, PackageFamily::Suse, PackageFamily::Void}) {
        if (backend_available(family)) {
            ++count;
            only = family;
        }
    }

    if (count == 0) {
        return PackageFamily::Unknown;
    }
    if (count == 1) {
        return only;
    }
    if (backend_available(distro.family)) {
        return distro.family;
    }
    return PackageFamily::Unknown;
}

} // namespace acclorite::system
