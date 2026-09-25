#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "acclorite/core/candidate_hint.hpp"
#include "acclorite/core/query.hpp"
#include "acclorite/core/search_engine.hpp"
#include "acclorite/sources/apt_source.hpp"
#include "acclorite/sources/desktop_source.hpp"
#include "acclorite/sources/dnf_source.hpp"
#include "acclorite/sources/index_source.hpp"
#include "acclorite/sources/man_source.hpp"
#include "acclorite/sources/pacman_source.hpp"
#include "acclorite/sources/path_source.hpp"
#include "acclorite/sources/pkgfile_enricher.hpp"
#include "acclorite/sources/xbps_source.hpp"
#include "acclorite/sources/zypper_source.hpp"
#include "acclorite/system/distro.hpp"

namespace {

std::string join_query(const std::vector<std::string>& parts) {
    std::string query;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            query.push_back(' ');
        }
        query += parts[i];
    }
    return query;
}

acclorite::SearchEngine make_engine() {
    acclorite::SearchEngine engine;
    auto index = std::make_unique<acclorite::IndexSource>();
    if (index->available()) {
        engine.add_source(std::move(index));
    } else {
        engine.add_source(std::make_unique<acclorite::PathSource>());
        engine.add_source(std::make_unique<acclorite::ManSource>());
        engine.add_source(std::make_unique<acclorite::DesktopSource>());
    }

    auto pacman = std::make_unique<acclorite::PacmanSource>();
    auto apt = std::make_unique<acclorite::AptSource>();
    auto dnf = std::make_unique<acclorite::DnfSource>();
    auto zypper = std::make_unique<acclorite::ZypperSource>();
    auto xbps = std::make_unique<acclorite::XbpsSource>();
    const bool pacman_available = pacman->available();
    const bool apt_available = apt->available();
    const bool dnf_available = dnf->available();
    const bool zypper_available = zypper->available();
    const bool xbps_available = xbps->available();
    const auto package_family = acclorite::system::choose_package_family(
        acclorite::system::detect_distro(),
        acclorite::system::PackageBackendAvailability{
            .arch = pacman_available,
            .debian = apt_available,
            .fedora = dnf_available,
            .suse = zypper_available,
            .void_linux = xbps_available,
        }
    );

    if (package_family == acclorite::PackageFamily::Arch && pacman_available) {
        engine.add_source(std::move(pacman));
        auto pkgfile = std::make_unique<acclorite::PkgfileEnricher>();
        if (pkgfile->available()) {
            engine.add_enricher(std::move(pkgfile));
        }
    } else if (package_family == acclorite::PackageFamily::Debian && apt_available) {
        engine.add_source(std::move(apt));
    } else if (package_family == acclorite::PackageFamily::Fedora && dnf_available) {
        engine.add_source(std::move(dnf));
    } else if (package_family == acclorite::PackageFamily::Suse && zypper_available) {
        engine.add_source(std::move(zypper));
    } else if (package_family == acclorite::PackageFamily::Void && xbps_available) {
        engine.add_source(std::move(xbps));
    }
    return engine;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<acclorite::CandidateHint> hints;
    std::vector<std::string> query_parts;
    bool query_mode = false;
    bool diagnostic = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (!query_mode && arg == "--diagnostic") {
            diagnostic = true;
            continue;
        }
        if (!query_mode && arg == "--hint") {
            if (i + 1 >= argc) {
                std::cerr << "error: --hint requires a command name\n";
                return 2;
            }
            hints.push_back(acclorite::CandidateHint{.command = argv[++i], .relevance_floor = 0.0});
            continue;
        }
        if (!query_mode && arg == "--ranked-hint") {
            if (i + 2 >= argc) {
                std::cerr << "error: --ranked-hint requires COMMAND FLOOR\n";
                return 2;
            }
            const std::string command = argv[++i];
            try {
                const double floor = std::stod(argv[++i]);
                hints.push_back(acclorite::CandidateHint{.command = command, .relevance_floor = floor});
            } catch (...) {
                std::cerr << "error: invalid --ranked-hint floor\n";
                return 2;
            }
            continue;
        }
        if (!query_mode && arg == "--") {
            query_mode = true;
            continue;
        }
        if (!query_mode && (arg == "--help" || arg == "-h")) {
            std::cout << "usage: acclorite_m11_injection_runner [--diagnostic] [--hint COMMAND | --ranked-hint COMMAND FLOOR ...] -- QUERY\n";
            return 0;
        }
        query_parts.emplace_back(arg);
        query_mode = true;
    }

    if (query_parts.empty()) {
        std::cerr << "error: missing query\n";
        return 2;
    }

    auto engine = make_engine();
    const auto result = engine.search(
        acclorite::Query::parse(join_query(query_parts)),
        10,
        false,
        false,
        hints
    );
    if (diagnostic) {
        std::cout << "@ambiguity\t" << acclorite::ambiguity_state_name(result.confidence.ambiguity) << '\n';
        std::cout << "@confidence\t" << std::fixed << std::setprecision(6)
                  << result.confidence.top_candidate << '\n';
        std::cout << "@level\t" << acclorite::confidence_level_name(result.confidence.top_candidate) << '\n';
    }
    for (const auto& candidate : result.candidates) {
        if (diagnostic) {
            std::cout << "@result\t";
        }
        std::cout << candidate.command << '\n';
    }
    return result.candidates.empty() ? 1 : 0;
}
