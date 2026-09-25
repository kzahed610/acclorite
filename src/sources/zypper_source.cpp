#include "acclorite/sources/zypper_source.hpp"

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

#ifndef ACCLORITE_ZYPPER_CONFIG_BUILD_FILE
#define ACCLORITE_ZYPPER_CONFIG_BUILD_FILE ""
#endif

#ifndef ACCLORITE_ZYPPER_CONFIG_INSTALL_FILE
#define ACCLORITE_ZYPPER_CONFIG_INSTALL_FILE ""
#endif

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

std::string xml_decode(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size();) {
        if (input[i] != '&') {
            out.push_back(input[i++]);
            continue;
        }

        const auto semi = input.find(';', i + 1);
        if (semi == std::string_view::npos) {
            out.push_back(input[i++]);
            continue;
        }
        const auto entity = input.substr(i + 1, semi - i - 1);
        if (entity == "amp") out.push_back('&');
        else if (entity == "quot") out.push_back('"');
        else if (entity == "apos") out.push_back('\'');
        else if (entity == "lt") out.push_back('<');
        else if (entity == "gt") out.push_back('>');
        else {
            out.append(input.substr(i, semi - i + 1));
            i = semi + 1;
            continue;
        }
        i = semi + 1;
    }
    return out;
}

std::optional<std::string> xml_attribute(
    const std::string_view tag,
    const std::string_view name
) {
    std::string needle;
    needle.reserve(name.size() + 2);
    needle.append(name);
    needle += "=\"";
    const auto start = tag.find(needle);
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    const auto value_begin = start + needle.size();
    const auto value_end = tag.find('"', value_begin);
    if (value_end == std::string_view::npos) {
        return std::nullopt;
    }
    return xml_decode(tag.substr(value_begin, value_end - value_begin));
}

std::vector<std::string_view> solvable_tags(const std::string_view xml) {
    std::vector<std::string_view> tags;
    std::size_t position = 0;
    while (true) {
        const auto start = xml.find("<solvable", position);
        if (start == std::string_view::npos) {
            break;
        }
        const auto after_name = start + std::string_view("<solvable").size();
        if (after_name < xml.size() &&
            xml[after_name] != ' ' && xml[after_name] != '\t' &&
            xml[after_name] != '\r' && xml[after_name] != '\n' &&
            xml[after_name] != '>') {
            position = after_name;
            continue;
        }
        const auto end = xml.find('>', after_name);
        if (end == std::string_view::npos) {
            break;
        }
        tags.push_back(xml.substr(start, end - start + 1));
        position = end + 1;
    }
    return tags;
}

bool real_repository(const std::string_view repository) {
    return !repository.empty() && repository != "@System" && repository != "(System Packages)";
}

} // namespace

std::optional<std::filesystem::path> ZypperSource::readonly_config() {
#ifdef __linux__
    std::error_code error;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error && !executable.empty()) {
        const auto candidate = executable.parent_path().parent_path() /
                               "share" / "acclorite" / "zypper-readonly.conf";
        if (std::filesystem::is_regular_file(candidate, error) && !error) {
            return candidate;
        }
    }
#endif

    for (const char* raw : {ACCLORITE_ZYPPER_CONFIG_INSTALL_FILE,
                            ACCLORITE_ZYPPER_CONFIG_BUILD_FILE}) {
        if (raw == nullptr || *raw == '\0') {
            continue;
        }
        std::error_code error;
        const std::filesystem::path path(raw);
        if (std::filesystem::is_regular_file(path, error) && !error) {
            return path;
        }
    }
    return std::nullopt;
}

std::vector<std::string> ZypperSource::base_arguments() {
    const auto config = readonly_config();
    if (!config) {
        return {};
    }
    return {
        "zypper",
        "--config", config->string(),
        "--non-interactive",
        "--xmlout",
        "--no-refresh",
        "--ignore-unknown",
    };
}

bool ZypperSource::available() const {
    return system::find_executable("zypper").has_value() && readonly_config().has_value();
}

bool ZypperSource::cached_metadata_ready() {
    if (!system::find_executable("zypper")) {
        return false;
    }
    auto args = base_arguments();
    if (args.empty()) {
        return false;
    }
    args.insert(args.end(), {
        "search", "--details", "--type", "package", "--match-exact", "rpm"
    });
    const auto process = system::run_capture_stdout(args, 256 * 1024);
    if (process.exit_code != 0 && process.stdout_text.empty()) {
        return false;
    }
    for (const auto tag : solvable_tags(process.stdout_text)) {
        const auto name = xml_attribute(tag, "name");
        const auto kind = xml_attribute(tag, "kind");
        if (name && *name == "rpm" && (!kind || *kind == "package")) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> ZypperSource::search_terms(const Query& query) {
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

std::vector<ZypperSource::PackageEntry> ZypperSource::parse_search_xml(
    const std::string_view output
) {
    std::vector<PackageEntry> entries;
    std::unordered_set<std::string> seen;
    for (const auto tag : solvable_tags(output)) {
        const auto name = xml_attribute(tag, "name");
        const auto kind = xml_attribute(tag, "kind");
        if (!name || name->empty() || (kind && *kind != "package") || !seen.insert(*name).second) {
            continue;
        }
        entries.push_back(PackageEntry{
            .package = *name,
            .version = xml_attribute(tag, "edition").value_or(std::string{}),
            .repository = xml_attribute(tag, "repository").value_or(std::string{}),
            .description = xml_attribute(tag, "summary").value_or(std::string{}),
            .status = xml_attribute(tag, "status").value_or(std::string{}),
            .installed = xml_attribute(tag, "status").value_or(std::string{}) == "installed",
        });
    }
    return entries;
}

std::unordered_map<std::string, ZypperSource::PackageEntry> ZypperSource::repository_details(
    const std::vector<Candidate>& candidates
) {
    std::unordered_map<std::string, PackageEntry> details;
    if (candidates.empty()) {
        return details;
    }
    auto args = base_arguments();
    if (args.empty()) {
        return details;
    }
    args.insert(args.end(), {
        "search", "--details", "--type", "package", "--match-exact"
    });
    for (const auto& candidate : candidates) {
        if (!candidate.package.empty()) {
            args.push_back(candidate.package);
        }
    }

    const auto process = system::run_capture_stdout(args, 4 * 1024 * 1024);
    if (process.exit_code != 0 && process.stdout_text.empty()) {
        return details;
    }

    for (const auto tag : solvable_tags(process.stdout_text)) {
        const auto name = xml_attribute(tag, "name");
        const auto kind = xml_attribute(tag, "kind");
        const auto status = xml_attribute(tag, "status").value_or(std::string{});
        if (!name || name->empty() || (kind && *kind != "package") || status == "other-version") {
            continue;
        }

        PackageEntry entry{
            .package = *name,
            .version = xml_attribute(tag, "edition").value_or(std::string{}),
            .repository = xml_attribute(tag, "repository").value_or(std::string{}),
            .description = xml_attribute(tag, "summary").value_or(std::string{}),
            .status = status,
            .installed = status == "installed",
        };

        const auto existing = details.find(entry.package);
        if (existing == details.end()) {
            details.emplace(entry.package, std::move(entry));
            continue;
        }
        // Prefer a repository-backed instance over @System so repository
        // availability/version remain useful even for already-installed tools.
        if (!real_repository(existing->second.repository) && real_repository(entry.repository)) {
            existing->second = std::move(entry);
        }
    }
    return details;
}

std::unordered_map<std::string, std::string> ZypperSource::installed_versions(
    const std::vector<Candidate>& candidates
) {
    std::unordered_map<std::string, std::string> installed;
    if (candidates.empty() || !system::find_executable("rpm")) {
        return installed;
    }
    std::vector<std::string> args{"rpm", "-q", "--qf", "%{NAME}\\t%{EVR}\\n"};
    for (const auto& candidate : candidates) {
        if (!candidate.package.empty()) {
            args.push_back(candidate.package);
        }
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
        const std::string name = trim(std::string_view(line).substr(0, tab));
        const std::string version = trim(std::string_view(line).substr(tab + 1));
        if (!name.empty()) {
            installed[name] = version;
        }
    }
    return installed;
}

std::vector<Candidate> ZypperSource::search(const Query& query) const {
    if (!available()) {
        return {};
    }
    const auto terms = search_terms(query);
    if (terms.empty()) {
        return {};
    }

    auto args = base_arguments();
    args.insert(args.end(), {
        "search", "--search-descriptions", "--type", "package"
    });
    args.insert(args.end(), terms.begin(), terms.end());
    const auto process = system::run_capture_stdout(args, 4 * 1024 * 1024);
    if (process.exit_code != 0 && process.stdout_text.empty()) {
        return {};
    }

    auto entries = parse_search_xml(process.stdout_text);
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

        const auto executable = system::find_executable(entry.package);
        Candidate candidate{
            .command = entry.package,
            .path = executable ? executable->string() : std::string{},
            .summary = entry.description.empty() ? "Package available in Zypper repositories" : entry.description,
            .source = "zypper",
            .package = entry.package,
            .repository = entry.repository,
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
                "zypper", "repository package semantic match", relevance,
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

    const auto details = repository_details(result);
    const auto installed = installed_versions(result);
    for (auto& candidate : result) {
        if (const auto it = details.find(candidate.package); it != details.end()) {
            candidate.package_version = it->second.version;
            candidate.repository = it->second.repository;
            if (!it->second.description.empty()) {
                candidate.summary = it->second.description;
            }
            if (!candidate.repository.empty()) {
                candidate.repository_available = real_repository(candidate.repository);
            }
        }
        if (const auto it = installed.find(candidate.package); it != installed.end()) {
            candidate.installed = true;
            if (candidate.package_version.empty()) {
                candidate.package_version = it->second;
            }
        }
        if (const auto executable = system::find_executable(candidate.package)) {
            candidate.path = executable->string();
            candidate.cli_capable = true;
            candidate.installed = true;
        }
    }

    return result;
}

} // namespace acclorite
