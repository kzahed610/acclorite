#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "acclorite/core/capabilities.hpp"
#include "acclorite/core/machine_interface.hpp"
#include "acclorite/core/query.hpp"
#include "acclorite/core/search_engine.hpp"
#include "acclorite/diagnostics/doctor.hpp"
#include "acclorite/guidance/curated_provider.hpp"
#include "acclorite/guidance/info_provider.hpp"
#include "acclorite/guidance/man_provider.hpp"
#include "acclorite/guidance/tldr_provider.hpp"
#include "acclorite/output/capabilities_json_renderer.hpp"
#include "acclorite/output/doctor_json_renderer.hpp"
#include "acclorite/output/doctor_terminal_renderer.hpp"
#include "acclorite/output/json_renderer.hpp"
#include "acclorite/output/terminal_renderer.hpp"
#include "acclorite/sources/desktop_source.hpp"
#include "acclorite/sources/filesystem_location_source.hpp"
#include "acclorite/sources/index_source.hpp"
#include "acclorite/sources/man_source.hpp"
#include "acclorite/sources/pacman_source.hpp"
#include "acclorite/sources/path_source.hpp"
#include "acclorite/sources/pkgfile_enricher.hpp"

#ifndef ACCLORITE_VERSION
#define ACCLORITE_VERSION "0.2.6"
#endif

namespace {

constexpr std::string_view kVersion = ACCLORITE_VERSION;
using acclorite::machine::ExitCode;
using acclorite::machine::exit_code;

struct CliOptions {
    bool help{false};
    bool version{false};
    bool json{false};
    bool reindex{false};
    bool explain_ranking{false};
    bool profile{false};
    bool capabilities{false};
    std::vector<std::string> query_parts;
};

void print_usage(std::ostream& out) {
    out << "Acclorite — Linux tool discovery assistant\n\n";
    out << "Usage:\n";
    out << "  acclorite [--json] [--explain-ranking] [--profile] <query...>\n";
    out << "  acclorite [--json] doctor\n";
    out << "  acclorite --reindex\n";
    out << "  acclorite [--json] --capabilities\n";
    out << "  acclorite --version\n";
    out << "  acclorite --help\n";
}

int usage_error(const std::string_view message, const bool show_usage = false) {
    std::cerr << "error: " << message << '\n';
    if (show_usage) {
        std::cerr << '\n';
        print_usage(std::cerr);
    }
    return exit_code(ExitCode::UsageError);
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

int run_cli(const int argc, char** argv) {
    if (argc < 2) {
        print_usage(std::cerr);
        return exit_code(ExitCode::UsageError);
    }

    CliOptions options;
    bool end_of_options = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);

        if (end_of_options) {
            options.query_parts.emplace_back(arg);
            continue;
        }

        if (arg == "--") {
            end_of_options = true;
            continue;
        }
        if (arg == "--help" || arg == "-h") {
            options.help = true;
            continue;
        }
        if (arg == "--version" || arg == "-V") {
            options.version = true;
            continue;
        }
        if (arg == "--json") {
            options.json = true;
            continue;
        }
        if (arg == "--reindex") {
            options.reindex = true;
            continue;
        }
        if (arg == "--explain-ranking") {
            options.explain_ranking = true;
            continue;
        }
        if (arg == "--profile") {
            options.profile = true;
            continue;
        }
        if (arg == "--capabilities") {
            options.capabilities = true;
            continue;
        }
        if (!arg.empty() && arg.front() == '-') {
            return usage_error("unknown option: " + std::string(arg));
        }

        options.query_parts.emplace_back(arg);
    }

    if (options.help) {
        if (options.version || options.capabilities || options.reindex || options.json ||
            options.explain_ranking || options.profile || !options.query_parts.empty()) {
            return usage_error("--help must be used by itself");
        }
        print_usage(std::cout);
        return exit_code(ExitCode::Success);
    }

    if (options.version) {
        if (options.capabilities || options.reindex || options.json || options.explain_ranking ||
            options.profile || !options.query_parts.empty()) {
            return usage_error("--version must be used by itself");
        }
        std::cout << "acclorite " << kVersion << '\n';
        return exit_code(ExitCode::Success);
    }

    if (options.capabilities) {
        if (options.reindex || options.explain_ranking || options.profile || !options.query_parts.empty()) {
            return usage_error("--capabilities does not accept a query, --reindex, --explain-ranking, or --profile");
        }
        const auto report = acclorite::detect_capabilities();
        acclorite::CapabilitiesJsonRenderer{}.render(report, kVersion, std::cout);
        return exit_code(ExitCode::Success);
    }

    const bool doctor = options.query_parts.size() == 1 && options.query_parts.front() == "doctor";
    if (doctor) {
        if (options.reindex) {
            return usage_error("doctor does not accept --reindex");
        }
        if (options.explain_ranking) {
            return usage_error("doctor does not accept --explain-ranking");
        }
        if (options.profile) {
            return usage_error("doctor does not accept --profile");
        }

        const auto report = acclorite::diagnostics::Doctor{}.run();
        if (options.json) {
            acclorite::DoctorJsonRenderer{}.render(report, std::cout);
        } else {
            acclorite::DoctorTerminalRenderer{}.render(report, std::cout);
        }
        return report.has_errors()
            ? exit_code(ExitCode::DiagnosticError)
            : exit_code(ExitCode::Success);
    }

    if (options.reindex) {
        if (options.json || options.explain_ranking || options.profile || !options.query_parts.empty()) {
            return usage_error("--reindex is a standalone maintenance command");
        }
        const acclorite::IndexSource index;
        if (!index.rebuild()) {
            std::cerr << "error: failed to rebuild Acclorite index\n";
            return exit_code(ExitCode::OperationalError);
        }
        std::cout << "Rebuilt Acclorite index: " << acclorite::IndexSource::database_path().string() << '\n';
        return exit_code(ExitCode::Success);
    }

    if (options.query_parts.empty()) {
        return usage_error("missing query", true);
    }

    const acclorite::Query query = acclorite::Query::parse(join_query(options.query_parts));

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

    const acclorite::SearchResult result = engine.search(
        query,
        10,
        options.explain_ranking,
        options.profile
    );

    if (options.json) {
        acclorite::JsonRenderer{}.render(result, std::cout);
    } else {
        acclorite::TerminalRenderer{}.render(result, std::cout);
    }

    return (result.candidates.empty() && result.locations.empty())
        ? exit_code(ExitCode::NoResult)
        : exit_code(ExitCode::Success);
}

} // namespace

int main(const int argc, char** argv) {
    try {
        return run_cli(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "error: Acclorite operational failure: " << error.what() << '\n';
        return exit_code(ExitCode::OperationalError);
    } catch (...) {
        std::cerr << "error: Acclorite operational failure: unknown exception\n";
        return exit_code(ExitCode::OperationalError);
    }
}
