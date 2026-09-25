#include "acclorite/sources/xbps_source.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

#include "acclorite/query/lexicon.hpp"
#include "acclorite/query/relevance.hpp"
#include "acclorite/ranking/evidence.hpp"
#include "acclorite/system/executable.hpp"
#include "acclorite/system/process.hpp"

namespace acclorite {
namespace {

std::string trim(std::string_view input) {
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = input.find_last_not_of(" \t\r\n");
    return std::string(input.substr(first, last - first + 1));
}

std::string ere_escape(std::string_view input) {
    static constexpr std::string_view special = R"(.^$[](){}*+?|\)";
    std::string escaped;
    escaped.reserve(input.size() * 2);
    for (const char ch : input) {
        if (special.find(ch) != std::string_view::npos) {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::vector<std::string> lines(std::string_view input) {
    std::vector<std::string> result;
    std::istringstream stream{std::string(input)};
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (!line.empty()) {
            result.push_back(std::move(line));
        }
    }
    return result;
}

// Fallback only. Normal Void installations include xbps-uhelper, which Acclorite
// uses to decode the formal <pkgname>-<version>_<revision> tuple exactly.
std::pair<std::string, std::string> fallback_split_pkgver(const std::string_view pkgver) {
    for (std::size_t i = 0; i + 1 < pkgver.size(); ++i) {
        if (pkgver[i] != '-' || !std::isdigit(static_cast<unsigned char>(pkgver[i + 1]))) {
            continue;
        }
        const auto suffix = pkgver.substr(i + 1);
        if (suffix.find('_') != std::string_view::npos) {
            return {std::string(pkgver.substr(0, i)), std::string(suffix)};
        }
    }
    return {std::string(pkgver), {}};
}

} // namespace

bool XbpsSource::available() const {
    return system::find_executable("xbps-query").has_value() &&
           system::find_executable("xbps-uhelper").has_value();
}

bool XbpsSource::cached_metadata_ready() {
    if (!system::find_executable("xbps-query")) {
        return false;
    }
    const auto process = system::run_capture_stdout({"xbps-query", "-L"}, 256 * 1024);
    if (process.exit_code != 0) {
        return false;
    }

    std::istringstream stream(process.stdout_text);
    std::string line;
    while (std::getline(stream, line)) {
        std::istringstream fields(line);
        long long count = -1;
        if (fields >> count && count >= 0) {
            return true;
        }
    }
    return false;
}

bool XbpsSource::installed_metadata_ready() {
    if (!system::find_executable("xbps-query")) {
        return false;
    }
    const auto process = system::run_capture_stdout({"xbps-query", "-l"}, 256 * 1024);
    return process.exit_code == 0;
}

std::string XbpsSource::search_pattern(const Query& query) {
    auto terms = query::retrieval_terms(query, 8);
    std::vector<std::string> selected;
    selected.reserve(4);
    std::unordered_set<std::string> seen;

    for (const auto& term : terms) {
        if (term.size() < 2 || !seen.insert(term).second) {
            continue;
        }
        selected.push_back(ere_escape(term));
        if (selected.size() == 4) {
            break;
        }
    }
    if (selected.empty()) {
        for (const auto& token : query.tokens) {
            if (token.size() >= 2 && seen.insert(token).second) {
                selected.push_back(ere_escape(token));
            }
            if (selected.size() == 4) {
                break;
            }
        }
    }
    if (selected.empty()) {
        return {};
    }
    if (selected.size() == 1) {
        return selected.front();
    }

    std::string pattern{"("};
    for (std::size_t i = 0; i < selected.size(); ++i) {
        if (i != 0) {
            pattern.push_back('|');
        }
        pattern += selected[i];
    }
    pattern.push_back(')');
    return pattern;
}

std::vector<XbpsSource::PackageEntry> XbpsSource::parse_search(const std::string_view output) {
    std::vector<PackageEntry> entries;
    entries.reserve(64);
    std::unordered_set<std::string> seen_pkgvers;
    std::istringstream stream{std::string(output)};
    std::string line;

    while (std::getline(stream, line)) {
        const std::string content = trim(line);
        if (content.size() < 5 || content.front() != '[' || content[2] != ']') {
            continue;
        }

        const bool installed = content[1] == '*';
        const auto body_begin = content.find_first_not_of(" \t", 3);
        if (body_begin == std::string::npos) {
            continue;
        }
        const auto pkgver_end = content.find_first_of(" \t", body_begin);
        const std::string pkgver = pkgver_end == std::string::npos
            ? content.substr(body_begin)
            : content.substr(body_begin, pkgver_end - body_begin);
        if (pkgver.empty() || !seen_pkgvers.insert(pkgver).second) {
            continue;
        }

        const auto description_begin = pkgver_end == std::string::npos
            ? std::string::npos
            : content.find_first_not_of(" \t", pkgver_end);
        auto [fallback_name, fallback_version] = fallback_split_pkgver(pkgver);
        entries.push_back(PackageEntry{
            .pkgver = pkgver,
            .package = std::move(fallback_name),
            .version = std::move(fallback_version),
            .description = description_begin == std::string::npos
                ? std::string{}
                : trim(std::string_view(content).substr(description_begin)),
            .installed = installed,
        });

        // Bound helper argv and semantic work even for extremely broad regexes.
        if (entries.size() == 160) {
            break;
        }
    }
    return entries;
}

void XbpsSource::decode_pkgvers(std::vector<PackageEntry>& entries) {
    if (entries.empty() || !system::find_executable("xbps-uhelper")) {
        return;
    }

    std::vector<std::string> name_args{"xbps-uhelper", "getpkgname"};
    std::vector<std::string> version_args{"xbps-uhelper", "getpkgversion"};
    name_args.reserve(entries.size() + 2);
    version_args.reserve(entries.size() + 2);
    for (const auto& entry : entries) {
        name_args.push_back(entry.pkgver);
        version_args.push_back(entry.pkgver);
    }

    const auto names_process = system::run_capture_stdout(name_args, 512 * 1024);
    const auto versions_process = system::run_capture_stdout(version_args, 512 * 1024);
    if (names_process.exit_code != 0 || versions_process.exit_code != 0) {
        return;
    }

    const auto names = lines(names_process.stdout_text);
    const auto versions = lines(versions_process.stdout_text);
    if (names.size() != entries.size() || versions.size() != entries.size()) {
        return;
    }
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (!names[i].empty()) {
            entries[i].package = names[i];
        }
        if (!versions[i].empty()) {
            entries[i].version = versions[i];
        }
    }
}

std::vector<Candidate> XbpsSource::search(const Query& query) const {
    if (!available()) {
        return {};
    }
    const std::string pattern = search_pattern(query);
    if (pattern.empty()) {
        return {};
    }

    // Repository mode reads synchronized on-disk repodata. Never add -M/
    // --memory-sync here: that is XBPS's explicit remote-fetch mode.
    const auto process = system::run_capture_stdout(
        {"xbps-query", "--regex", "-Rs", pattern},
        4 * 1024 * 1024
    );
    if (process.exit_code != 0 && process.stdout_text.empty()) {
        return {};
    }

    auto entries = parse_search(process.stdout_text);
    if (entries.empty()) {
        return {};
    }
    decode_pkgvers(entries);

    std::vector<Candidate> result;
    result.reserve(entries.size());
    for (const auto& entry : entries) {
        if (entry.package.empty()) {
            continue;
        }
        auto relevance = query::score_text(query, entry.package, entry.description, 0.98, 0.94);
        if (relevance.score <= 0.0) {
            continue;
        }

        std::vector<ranking::RankingAdjustment> source_adjustments;
        const double before_metadata_bonus = relevance.score;
        const double score = std::min(0.96, relevance.score + 0.02);
        if (score != before_metadata_bonus) {
            source_adjustments.push_back(ranking::RankingAdjustment{
                .id = "repository-metadata",
                .label = "repository metadata support",
                .kind = ranking::AdjustmentKind::Add,
                .value = score - before_metadata_bonus,
                .before = before_metadata_bonus,
                .after = score,
            });
        }

        const auto executable = system::find_executable(entry.package);
        Candidate candidate{
            .command = entry.package,
            .path = executable ? executable->string() : std::string{},
            .summary = entry.description.empty() ? "Package available in XBPS repositories" : entry.description,
            .source = "xbps",
            .package = entry.package,
            .repository = {},
            .package_version = entry.version,
            .installed = entry.installed || executable.has_value(),
            .repository_available = true,
            .cli_capable = executable.has_value(),
            .gui_capable = false,
            .matched_terms = relevance.matched_terms,
            .provided_commands = {},
            .descriptive_evidence = {},
            .examples = {},
            .learning_resources = {},
            .evidence_trace = {},
            .base_merge_trace = {},
            .score = score,
            .ranking = std::nullopt,
        };
        candidate.semantic_fit = relevance.semantic_fit;
        if (query.explain_ranking) {
            candidate.evidence_trace.push_back(ranking::semantic_evidence(
                "xbps", "repository package semantic match", relevance,
                std::move(source_adjustments), candidate.score
            ));
        }
        result.push_back(std::move(candidate));
    }

    std::ranges::sort(result, [](const Candidate& left, const Candidate& right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        return left.command < right.command;
    });
    if (result.size() > 80) {
        result.resize(80);
    }
    return result;
}

} // namespace acclorite
