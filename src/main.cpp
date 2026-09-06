#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "acclorite/core/query.hpp"
#include "acclorite/core/search_engine.hpp"
#include "acclorite/diagnostics/doctor.hpp"
#include "acclorite/output/json_renderer.hpp"
#include "acclorite/output/doctor_json_renderer.hpp"
#include "acclorite/output/doctor_terminal_renderer.hpp"
#include "acclorite/output/terminal_renderer.hpp"
#include "acclorite/sources/desktop_source.hpp"
#include "acclorite/sources/index_source.hpp"
#include "acclorite/sources/filesystem_location_source.hpp"
#include "acclorite/sources/man_source.hpp"
#include "acclorite/sources/pacman_source.hpp"
#include "acclorite/sources/path_source.hpp"
#include "acclorite/sources/pkgfile_enricher.hpp"
#include "acclorite/guidance/man_provider.hpp"
#include "acclorite/guidance/info_provider.hpp"
#include "acclorite/guidance/tldr_provider.hpp"
#include "acclorite/guidance/curated_provider.hpp"

#ifndef ACCLORITE_VERSION
#define ACCLORITE_VERSION "0.2.4"
#endif

namespace {

constexpr std::string_view kVersion = ACCLORITE_VERSION;

void print_usage(std::ostream& out) {
    out << "Acclorite — Linux tool discovery assistant\n\n";
    out << "Usage:\n";
    out << "  acclorite [--json] [--explain-ranking] [--profile] <query...>\n";
    out << "  acclorite --reindex\n";
    out << "  acclorite [--json] doctor\n";
    out << "  acclorite --version\n";
    out << "  acclorite --help\n";
}

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

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(std::cerr);
        return 2;
    }

    bool json = false;
    bool reindex = false;
    bool explain_ranking = false;
    bool profile = false;
    std::vector<std::string> query_parts;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);

        if (arg == "--help" || arg == "-h") {
            print_usage(std::cout);
            return 0;
        }

        if (arg == "--version" || arg == "-V") {
            std::cout << "acclorite " << kVersion << '\n';
            return 0;
        }

        if (arg == "--json") {
            json = true;
            continue;
        }

        if (arg == "--reindex") {
            reindex = true;
            continue;
        }

        if (arg == "--explain-ranking") {
            explain_ranking = true;
            continue;
        }

        if (arg == "--profile") {
            profile = true;
            continue;
        }

        query_parts.emplace_back(arg);
    }

    const bool doctor = query_parts.size() == 1 && query_parts.front() == "doctor";
    if (doctor) {
        if (reindex) {
            std::cerr << "error: doctor does not accept --reindex\n";
            return 2;
        }
        if (explain_ranking) {
            std::cerr << "error: doctor does not accept --explain-ranking\n";
            return 2;
        }
        if (profile) {
            std::cerr << "error: doctor does not accept --profile\n";
            return 2;
        }

        const auto report = acclorite::diagnostics::Doctor{}.run();
        if (json) {
            acclorite::DoctorJsonRenderer{}.render(report, std::cout);
        } else {
            acclorite::DoctorTerminalRenderer{}.render(report, std::cout);
        }
        return report.has_errors() ? 1 : 0;
    }

    if (reindex) {
        const acclorite::IndexSource index;
        if (!index.rebuild()) {
            std::cerr << "error: failed to rebuild Acclorite index\n";
            return 1;
        }
        std::cout << "Rebuilt Acclorite index: " << acclorite::IndexSource::database_path().string() << '\n';
        if (query_parts.empty()) {
            return 0;
        }
    }

    if (query_parts.empty()) {
        std::cerr << "error: missing query\n\n";
        print_usage(std::cerr);
        return 2;
    }

    const acclorite::Query query = acclorite::Query::parse(join_query(query_parts));

    acclorite::SearchEngine engine;
    auto index = std::make_unique<acclorite::IndexSource>();
    if (index->available()) {
        engine.add_source(std::move(index));
    } else {
        // Cache/index failure must degrade, not brick tool discovery.
        engine.add_source(std::make_unique<acclorite::PathSource>());
        engine.add_source(std::make_unique<acclorite::ManSource>());
        engine.add_source(std::make_unique<acclorite::DesktopSource>());
    }

    // Arch package discovery is deliberately live for now: pacman can search the
    // synchronized repository metadata without requiring Acclorite to duplicate
    // the entire package database in its cache. Other distro backends will use
    // the same source boundary later.
    auto pacman = std::make_unique<acclorite::PacmanSource>();
    if (pacman->available()) {
        engine.add_source(std::move(pacman));
    }

    auto pkgfile = std::make_unique<acclorite::PkgfileEnricher>();
    if (pkgfile->available()) {
        engine.add_enricher(std::move(pkgfile));
    }

    engine.add_location_source(std::make_unique<acclorite::FilesystemLocationSource>());

    auto man_guidance = std::make_unique<acclorite::ManGuidanceProvider>();
    if (man_guidance->available()) {
        engine.add_guidance_provider(std::move(man_guidance));
    }

    auto info_guidance = std::make_unique<acclorite::InfoGuidanceProvider>();
    if (info_guidance->available()) {
        engine.add_guidance_provider(std::move(info_guidance));
    }

    auto tldr_guidance = std::make_unique<acclorite::TldrGuidanceProvider>();
    if (tldr_guidance->available()) {
        engine.add_guidance_provider(std::move(tldr_guidance));
    }

    auto curated_guidance = std::make_unique<acclorite::CuratedGuidanceProvider>();
    if (curated_guidance->available()) {
        engine.add_guidance_provider(std::move(curated_guidance));
    }

    const acclorite::SearchResult result = engine.search(query, 10, explain_ranking, profile);

    if (json) {
        acclorite::JsonRenderer{}.render(result, std::cout);
    } else {
        acclorite::TerminalRenderer{}.render(result, std::cout);
    }

    return (result.candidates.empty() && result.locations.empty()) ? 1 : 0;
}
