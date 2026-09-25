#include "acclorite/sources/dnf_source.hpp"

#include <algorithm>
#include <array>
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

bool known_arch_suffix(const std::string_view suffix) {
    static constexpr std::array<std::string_view, 11> arches{
        "x86_64", "noarch", "aarch64", "i686", "i586", "i486",
        "ppc64le", "ppc64", "s390x", "armv7hl", "src"
    };
    return std::ranges::find(arches, suffix) != arches.end();
}

std::string normalize_search_package(std::string value) {
    value = trim(value);
    const auto dot = value.rfind('.');
    if (dot != std::string::npos && known_arch_suffix(std::string_view(value).substr(dot + 1))) {
        value.resize(dot);
    }
    return value;
}

bool search_header(std::string_view line) {
    line = std::string_view(line).substr(line.find_first_not_of(" \t") == std::string_view::npos
        ? line.size() : line.find_first_not_of(" \t"));
    return line.empty() || line.front() == '=' ||
           line.starts_with("Matched fields:") ||
           line.starts_with("Name & Summary Matched:") ||
           line.starts_with("Name Matched:") ||
           line.starts_with("Summary Matched:") ||
           line.starts_with("Description & URL Matched:") ||
           line.starts_with("Last metadata expiration check:");
}

} // namespace

std::optional<std::string> DnfSource::frontend() {
    if (system::find_executable("dnf5")) {
        return std::string{"dnf5"};
    }
    if (system::find_executable("dnf")) {
        return std::string{"dnf"};
    }
    return std::nullopt;
}

bool DnfSource::available() const {
    return frontend().has_value();
}

std::vector<std::string> DnfSource::search_terms(const Query& query) {
    auto terms = query::retrieval_terms(query, 8);
    std::vector<std::string> selected;
    selected.reserve(4);
    std::unordered_set<std::string> seen;

    for (const auto& term : terms) {
        if (term.size() < 2 || !seen.insert(term).second) {
            continue;
        }
        selected.push_back(term);
        if (selected.size() == 4) {
            break;
        }
    }

    if (selected.empty()) {
        for (const auto& token : query.tokens) {
            if (token.size() >= 2 && seen.insert(token).second) {
                selected.push_back(token);
            }
            if (selected.size() == 4) {
                break;
            }
        }
    }
    return selected;
}

std::vector<DnfSource::PackageEntry> DnfSource::parse_search(const std::string_view output) {
    std::vector<PackageEntry> entries;
    std::unordered_set<std::string> seen;
    std::istringstream stream{std::string(output)};
    std::string line;

    while (std::getline(stream, line)) {
        if (search_header(line)) {
            continue;
        }

        const std::string_view content = std::string_view(line).substr(
            line.find_first_not_of(" \t") == std::string::npos ? line.size()
                                                              : line.find_first_not_of(" \t")
        );
        if (content.empty()) {
            continue;
        }

        // DNF4 traditionally prints `name.arch : summary`, while DNF5 has
        // emitted both `name.arch: summary` and column-style whitespace output
        // across releases. Accept all of those presentation variants without
        // making the package backend depend on one frontend's typography.
        std::size_t package_end = std::string_view::npos;
        std::size_t description_begin = std::string_view::npos;

        if (const auto delimiter = content.find(" : "); delimiter != std::string_view::npos) {
            package_end = delimiter;
            description_begin = delimiter + 3;
        } else if (const auto delimiter = content.find(": "); delimiter != std::string_view::npos) {
            package_end = delimiter;
            description_begin = delimiter + 2;
        } else {
            const auto first_space = content.find_first_of(" \t");
            if (first_space != std::string_view::npos) {
                const auto after_gap = content.find_first_not_of(" \t", first_space);
                if (after_gap != std::string_view::npos) {
                    package_end = first_space;
                    description_begin = after_gap;
                }
            }
        }

        if (package_end == std::string_view::npos || description_begin == std::string_view::npos) {
            continue;
        }

        std::string package = normalize_search_package(std::string(content.substr(0, package_end)));
        if (package.empty() || !seen.insert(package).second) {
            continue;
        }

        entries.push_back(PackageEntry{
            .package = std::move(package),
            .version = {},
            .repository = {},
            .description = trim(content.substr(description_begin)),
            .installed = false,
        });
    }

    return entries;
}

std::unordered_map<std::string, DnfSource::PackageEntry> DnfSource::repository_details(
    const std::string_view frontend_name,
    const std::vector<PackageEntry>& entries
) {
    std::unordered_map<std::string, PackageEntry> details;
    if (entries.empty()) {
        return details;
    }

    std::vector<std::string> args{
        std::string(frontend_name),
        "--cacheonly",
        "repoquery",
        "--available",
        "--queryformat=%{name}\\t%{evr}\\t%{repoid}\\t%{summary}\\n",
    };
    args.reserve(entries.size() + 5);
    for (const auto& entry : entries) {
        args.push_back(entry.package);
    }

    const auto process = system::run_capture_stdout(args, 4 * 1024 * 1024);
    if (process.exit_code != 0 && process.stdout_text.empty()) {
        return details;
    }

    std::istringstream stream(process.stdout_text);
    std::string line;
    while (std::getline(stream, line)) {
        const auto first = line.find('\t');
        const auto second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
        const auto third = second == std::string::npos ? std::string::npos : line.find('\t', second + 1);
        if (first == std::string::npos || second == std::string::npos || third == std::string::npos) {
            continue;
        }

        PackageEntry entry{
            .package = trim(std::string_view(line).substr(0, first)),
            .version = trim(std::string_view(line).substr(first + 1, second - first - 1)),
            .repository = trim(std::string_view(line).substr(second + 1, third - second - 1)),
            .description = trim(std::string_view(line).substr(third + 1)),
            .installed = false,
        };
        if (entry.package.empty()) {
            continue;
        }

        // Repoquery can return multiple architectures/versions. Acclorite only
        // needs one bounded repository projection per package name; keep the
        // first result emitted by DNF's own ordering.
        details.try_emplace(entry.package, std::move(entry));
    }

    return details;
}

std::unordered_map<std::string, std::string> DnfSource::installed_versions(
    const std::vector<PackageEntry>& entries
) {
    std::unordered_map<std::string, std::string> installed;
    if (entries.empty() || !system::find_executable("rpm")) {
        return installed;
    }

    std::vector<std::string> args{
        "rpm", "-q", "--qf", "%{NAME}\\t%{EVR}\\n"
    };
    args.reserve(entries.size() + 4);
    for (const auto& entry : entries) {
        args.push_back(entry.package);
    }

    const auto process = system::run_capture_stdout(args, 2 * 1024 * 1024);
    if (process.exit_code != 0 && process.stdout_text.empty()) {
        return installed;
    }

    std::istringstream stream(process.stdout_text);
    std::string line;
    while (std::getline(stream, line)) {
        const auto tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }
        const std::string package = trim(std::string_view(line).substr(0, tab));
        const std::string version = trim(std::string_view(line).substr(tab + 1));
        if (!package.empty()) {
            installed[package] = version;
        }
    }
    return installed;
}

std::vector<Candidate> DnfSource::search(const Query& query) const {
    const auto frontend_name = frontend();
    if (!frontend_name) {
        return {};
    }

    const auto terms = search_terms(query);
    if (terms.empty()) {
        return {};
    }

    std::vector<std::string> args{
        *frontend_name,
        "--cacheonly",
        "search",
        "--all",
    };
    args.insert(args.end(), terms.begin(), terms.end());

    const auto process = system::run_capture_stdout(args, 4 * 1024 * 1024);
    if (process.exit_code != 0 && process.stdout_text.empty()) {
        return {};
    }

    auto entries = parse_search(process.stdout_text);
    if (entries.empty()) {
        return {};
    }

    std::vector<Candidate> result;
    result.reserve(entries.size());

    for (const auto& entry : entries) {
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

        Candidate candidate{
            .command = entry.package,
            .path = {},
            .summary = entry.description.empty() ? "Package available in DNF repositories" : entry.description,
            .source = "dnf",
            .package = entry.package,
            .repository = {},
            .package_version = {},
            .installed = false,
            .repository_available = true,
            .cli_capable = false,
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
                "dnf", "repository package semantic match", relevance,
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

    std::vector<PackageEntry> survivors;
    survivors.reserve(result.size());
    for (const auto& candidate : result) {
        survivors.push_back(PackageEntry{
            .package = candidate.package,
            .version = {},
            .repository = {},
            .description = {},
            .installed = false,
        });
    }

    const auto details = repository_details(*frontend_name, survivors);
    const auto installed = installed_versions(survivors);
    for (auto& candidate : result) {
        if (const auto it = details.find(candidate.package); it != details.end()) {
            candidate.package_version = it->second.version;
            candidate.repository = it->second.repository;
            if (!it->second.description.empty()) {
                candidate.summary = it->second.description;
            }
        }

        if (const auto executable = system::find_executable(candidate.package)) {
            candidate.path = executable->string();
            candidate.cli_capable = true;
            candidate.installed = true;
        }
        if (const auto it = installed.find(candidate.package); it != installed.end()) {
            candidate.installed = true;
            if (candidate.package_version.empty()) {
                candidate.package_version = it->second;
            }
        }
    }

    return result;
}

} // namespace acclorite
