#include "acclorite/sources/apt_source.hpp"

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

std::vector<query::ConceptGroup> selected_search_groups(const Query& query) {
    auto groups = query::concept_groups(query);
    if (groups.empty()) {
        return {};
    }

    std::ranges::sort(groups, [](const auto& left, const auto& right) {
        if (left.implicit != right.implicit) {
            return !left.implicit;
        }
        return left.weight > right.weight;
    });

    std::vector<query::ConceptGroup> selected;
    for (const auto& group : groups) {
        if (group.implicit && !selected.empty()) {
            continue;
        }
        selected.push_back(group);
        if (selected.size() == 2) {
            break;
        }
    }

    if (selected.size() == 1 && !selected.front().implicit) {
        for (const auto& group : groups) {
            if (group.implicit) {
                selected.push_back(group);
                break;
            }
        }
    }
    return selected;
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

std::string group_pattern(const query::ConceptGroup& group) {
    std::vector<std::string> alternatives;
    alternatives.reserve(4);
    std::unordered_set<std::string> seen;

    for (const auto& term : group.alternatives) {
        if (term.size() < 2 || !seen.insert(term).second) {
            continue;
        }
        alternatives.push_back(ere_escape(term));
        if (alternatives.size() == 4) {
            break;
        }
    }

    if (alternatives.empty()) {
        return {};
    }
    if (alternatives.size() == 1) {
        return alternatives.front();
    }

    std::string pattern = "(";
    for (std::size_t i = 0; i < alternatives.size(); ++i) {
        if (i != 0) {
            pattern.push_back('|');
        }
        pattern += alternatives[i];
    }
    pattern.push_back(')');
    return pattern;
}

std::string normalize_binary_package(std::string name) {
    const auto colon = name.find(':');
    if (colon != std::string::npos) {
        name.resize(colon);
    }
    return name;
}

} // namespace

bool AptSource::available() const {
    return system::find_executable("apt-cache").has_value();
}

std::vector<std::string> AptSource::search_patterns(const Query& query) {
    const auto groups = selected_search_groups(query);
    std::vector<std::string> patterns;
    patterns.reserve(groups.size());
    for (const auto& group : groups) {
        const std::string pattern = group_pattern(group);
        if (!pattern.empty()) {
            patterns.push_back(pattern);
        }
    }
    return patterns;
}

std::vector<AptSource::PackageEntry> AptSource::parse_search(const std::string_view output) {
    std::vector<PackageEntry> entries;
    std::istringstream stream{std::string(output)};
    std::string line;
    while (std::getline(stream, line)) {
        const auto delimiter = line.find(" - ");
        if (delimiter == std::string::npos) {
            continue;
        }
        PackageEntry entry{
            .package = trim(std::string_view(line).substr(0, delimiter)),
            .version = {},
            .description = trim(std::string_view(line).substr(delimiter + 3)),
            .installed = false,
        };
        if (!entry.package.empty()) {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

std::unordered_map<std::string, std::string> AptSource::installed_packages() {
    std::unordered_map<std::string, std::string> installed;
    if (!system::find_executable("dpkg-query")) {
        return installed;
    }

    const auto process = system::run_capture_stdout(
        {"dpkg-query", "-W", "-f=${binary:Package}\\t${Version}\\t${Status}\\n"},
        4 * 1024 * 1024
    );
    if (process.exit_code != 0 && process.stdout_text.empty()) {
        return installed;
    }

    std::istringstream stream(process.stdout_text);
    std::string line;
    while (std::getline(stream, line)) {
        const auto first = line.find('\t');
        const auto second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
        if (first == std::string::npos || second == std::string::npos) {
            continue;
        }
        const std::string status = trim(std::string_view(line).substr(second + 1));
        if (status != "install ok installed") {
            continue;
        }
        const std::string raw_name = trim(std::string_view(line).substr(0, first));
        const std::string version = trim(std::string_view(line).substr(first + 1, second - first - 1));
        if (raw_name.empty()) {
            continue;
        }
        installed[raw_name] = version;
        installed.try_emplace(normalize_binary_package(raw_name), version);
    }
    return installed;
}


std::unordered_map<std::string, std::string> AptSource::available_versions(
    const std::vector<Candidate>& candidates
) {
    std::unordered_map<std::string, std::string> versions;
    if (candidates.empty()) {
        return versions;
    }

    std::vector<std::string> args{"apt-cache", "show", "--no-all-versions"};
    args.reserve(candidates.size() + 3);
    for (const auto& candidate : candidates) {
        if (!candidate.package.empty()) {
            args.push_back(candidate.package);
        }
    }
    if (args.size() == 3) {
        return versions;
    }

    const auto process = system::run_capture_stdout(args, 2 * 1024 * 1024);
    if (process.exit_code != 0 && process.stdout_text.empty()) {
        return versions;
    }

    std::istringstream stream(process.stdout_text);
    std::string line;
    std::string package;
    std::string version;
    auto flush = [&]() {
        if (!package.empty() && !version.empty()) {
            versions.try_emplace(package, version);
        }
        package.clear();
        version.clear();
    };

    while (std::getline(stream, line)) {
        if (line.empty()) {
            flush();
            continue;
        }
        if (line.rfind("Package:", 0) == 0) {
            package = trim(std::string_view(line).substr(8));
        } else if (line.rfind("Version:", 0) == 0) {
            version = trim(std::string_view(line).substr(8));
        }
    }
    flush();
    return versions;
}

std::vector<Candidate> AptSource::search(const Query& query) const {
    if (!available()) {
        return {};
    }

    const auto patterns = search_patterns(query);
    if (patterns.empty()) {
        return {};
    }

    const auto run_search = [&](const std::vector<std::string>& requested_patterns) {
        std::vector<std::string> args{"apt-cache", "search"};
        args.insert(args.end(), requested_patterns.begin(), requested_patterns.end());
        const auto process = system::run_capture_stdout(args, 4 * 1024 * 1024);
        if (process.exit_code != 0 && process.stdout_text.empty()) {
            return std::vector<PackageEntry>{};
        }
        return parse_search(process.stdout_text);
    };

    auto entries = run_search(patterns);
    if (entries.empty() && patterns.size() > 1) {
        entries = run_search({patterns.front()});
    }

    const auto installed = installed_packages();
    std::vector<Candidate> result;
    result.reserve(entries.size());

    for (auto& entry : entries) {
        const auto installed_it = installed.find(entry.package);
        if (installed_it != installed.end()) {
            entry.installed = true;
            entry.version = installed_it->second;
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
            .summary = entry.description.empty() ? "Package available in APT repositories" : entry.description,
            .source = "apt",
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
                "apt", "repository package semantic match", relevance,
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

    const auto versions = available_versions(result);
    for (auto& candidate : result) {
        if (const auto it = versions.find(candidate.package); it != versions.end()) {
            candidate.package_version = it->second;
        }
    }
    return result;
}

} // namespace acclorite
