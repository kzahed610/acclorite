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
#include "acclorite/lore/quote_provider.hpp"
#include "acclorite/syntax/man_provider.hpp"
#include "acclorite/syntax/fish_completion_provider.hpp"
#include "acclorite/output/capabilities_json_renderer.hpp"
#include "acclorite/output/doctor_json_renderer.hpp"
#include "acclorite/output/doctor_terminal_renderer.hpp"
#include "acclorite/output/json_renderer.hpp"
#include "acclorite/output/terminal_renderer.hpp"
#include "acclorite/sources/apt_source.hpp"
#include "acclorite/sources/desktop_source.hpp"
#include "acclorite/sources/dnf_source.hpp"
#include "acclorite/sources/zypper_source.hpp"
#include "acclorite/sources/xbps_source.hpp"
#include "acclorite/sources/filesystem_location_source.hpp"
#include "acclorite/sources/index_source.hpp"
#include "acclorite/sources/man_source.hpp"
#include "acclorite/sources/pacman_source.hpp"
#include "acclorite/sources/path_source.hpp"
#include "acclorite/sources/pkgfile_enricher.hpp"
#include "acclorite/system/distro.hpp"
#include "acclorite/system/terminal.hpp"

#ifndef ACCLORITE_VERSION
#define ACCLORITE_VERSION "0.3.5"
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
    bool lore{false};
    std::vector<std::string> query_parts;
};

constexpr std::string_view kAnsiReset = "\x1b[0m";
constexpr std::string_view kAnsiBold = "\x1b[1m";
constexpr std::string_view kAnsiDim = "\x1b[2m";
constexpr std::string_view kAnsiGreen = "\x1b[32m";
constexpr std::string_view kAnsiCyan = "\x1b[36m";
constexpr std::string_view kAnsiRed = "\x1b[31m";

void styled(
    std::ostream& out,
    const bool color,
    const std::string_view ansi,
    const std::string_view text
) {
    if (color) {
        out << ansi << text << kAnsiReset;
    } else {
        out << text;
    }
}

void render_lore_quote(const acclorite::lore::Quote& quote, std::ostream& out, const bool color, const bool leading_gap = true) {
    if (leading_gap) {
        out << '\n';
    }
    styled(out, color, kAnsiDim, "“");
    styled(out, color, kAnsiDim, quote.text);
    styled(out, color, kAnsiDim, "”");
    out << "\n  ";
    styled(out, color, kAnsiDim, std::string("— ") + quote.attribution);
    out << '\n';
}

void print_usage(std::ostream& out, const bool color = false) {
    styled(out, color, kAnsiCyan, "Acclorite");
    out << " " << kVersion << '\n';
    styled(out, color, kAnsiBold, "Find the right Linux tool — from local knowledge.");
    out << '\n';
    styled(out, color, kAnsiDim, "Deterministic · local-first · no network at runtime");
    out << "\n\n";

    styled(out, color, kAnsiBold, "Usage");
    out << "\n";
    out << "  acclorite <query...>                 Find, explain, locate, or compare tools\n";
    out << "  acclorite doctor                     Inspect local readiness\n";
    out << "  acclorite --reindex                  Rebuild the local search index\n";
    out << "  acclorite --capabilities             Print the machine capability contract\n\n";

    styled(out, color, kAnsiBold, "Try it");
    out << "\n";
    out << "  acclorite \"search text inside files\"\n";
    out << "  acclorite \"what is rg\"\n";
    out << "  acclorite \"where is ssh config\"\n";
    out << "  acclorite \"difference between rg and grep\"\n\n";

    styled(out, color, kAnsiBold, "Options");
    out << "\n";
    out << "  --json                 Emit stable machine-readable JSON\n";
    out << "  --explain-ranking      Show detailed ranking evidence\n";
    out << "  --profile              Show internal stage timings\n";
    out << "  --                      Stop option parsing\n";
    out << "  -h, --help             Show this help\n";
    out << "  -V, --version          Print the Acclorite version\n\n";

    styled(out, color, kAnsiDim, "NO_COLOR disables ANSI styling. Full docs: `man acclorite`.");
    out << '\n';
}

int usage_error(const std::string_view message, const bool show_usage = false) {
    const bool color = acclorite::system::stderr_supports_color();
    styled(std::cerr, color, kAnsiRed, "error");
    std::cerr << ": " << message << '\n';
    if (show_usage) {
        std::cerr << '\n';
        print_usage(std::cerr, color);
    } else {
        std::cerr << "hint: run `acclorite --help` for usage\n";
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
        print_usage(std::cerr, acclorite::system::stderr_supports_color());
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
        if (arg == "--lore") {
            options.lore = true;
            continue;
        }
        if (!arg.empty() && arg.front() == '-') {
            return usage_error("unknown option: " + std::string(arg));
        }

        options.query_parts.emplace_back(arg);
    }

    if (options.help) {
        if (options.version || options.capabilities || options.reindex || options.json || options.lore ||
            options.explain_ranking || options.profile || !options.query_parts.empty()) {
            return usage_error("--help must be used by itself");
        }
        print_usage(std::cout, acclorite::system::stdout_supports_color());
        return exit_code(ExitCode::Success);
    }

    if (options.version) {
        if (options.capabilities || options.reindex || options.json || options.lore || options.explain_ranking ||
            options.profile || !options.query_parts.empty()) {
            return usage_error("--version must be used by itself");
        }
        std::cout << "acclorite " << kVersion << '\n';
        return exit_code(ExitCode::Success);
    }

    if (options.capabilities) {
        if (options.reindex || options.explain_ranking || options.profile || options.lore || !options.query_parts.empty()) {
            return usage_error("--capabilities does not accept a query, --reindex, --explain-ranking, --profile, or --lore");
        }
        const auto report = acclorite::detect_capabilities();
        acclorite::CapabilitiesJsonRenderer{}.render(report, kVersion, std::cout);
        return exit_code(ExitCode::Success);
    }

    if (options.lore) {
        if (options.json || options.reindex || options.explain_ranking || options.profile ||
            !options.query_parts.empty()) {
            return usage_error("--lore must be used by itself");
        }
        const acclorite::lore::QuoteProvider lore;
        const auto quote = lore.quote_for(acclorite::lore::QuoteProvider::runtime_entropy());
        if (!quote) {
            return exit_code(ExitCode::NoResult);
        }
        render_lore_quote(*quote, std::cout, acclorite::system::stdout_supports_color(), false);
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
            acclorite::DoctorTerminalRenderer{acclorite::system::stdout_supports_color()}.render(report, std::cout);
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
            const bool color = acclorite::system::stderr_supports_color();
            styled(std::cerr, color, kAnsiRed, "error");
            std::cerr << ": failed to rebuild the Acclorite index\n";
            std::cerr << "hint: the previous usable index was preserved if one existed\n";
            std::cerr << "hint: run `acclorite doctor` for source and index diagnostics\n";
            return exit_code(ExitCode::OperationalError);
        }
        const bool color = acclorite::system::stdout_supports_color();
        styled(std::cout, color, kAnsiGreen, "✓ Index rebuilt");
        std::cout << "\n  " << acclorite::IndexSource::database_path().string() << '\n';
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

    auto man_syntax = std::make_unique<acclorite::ManCommandSyntaxProvider>();
    if (man_syntax->available()) {
        engine.add_syntax_provider(std::move(man_syntax));
    }

    auto fish_syntax = std::make_unique<acclorite::FishCompletionSyntaxProvider>();
    if (fish_syntax->available()) {
        engine.add_syntax_provider(std::move(fish_syntax));
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
        const bool color = acclorite::system::stdout_supports_color();
        acclorite::TerminalRenderer{color}.render(result, std::cout);
        if (!options.explain_ranking && !options.profile && acclorite::system::stdout_is_terminal()) {
            const auto entropy = acclorite::lore::QuoteProvider::runtime_entropy();
            if (acclorite::lore::QuoteProvider::should_surface(entropy)) {
                const acclorite::lore::QuoteProvider lore;
                if (const auto quote = lore.quote_for(entropy)) {
                    render_lore_quote(*quote, std::cout, color);
                }
            }
        }
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
