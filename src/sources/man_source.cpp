#include "acclorite/sources/man_source.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "acclorite/query/lexicon.hpp"
#include "acclorite/query/relevance.hpp"
#include "acclorite/ranking/evidence.hpp"
#include "acclorite/system/executable.hpp"
#include "acclorite/system/process.hpp"

namespace acclorite {
namespace {


std::string trim(std::string value) {
    const auto not_space = [](const unsigned char ch) { return !std::isspace(ch); };

    const auto first = std::ranges::find_if(value, not_space);
    if (first == value.end()) {
        return {};
    }

    const auto last = std::find_if(value.rbegin(), value.rend(), not_space).base();
    return std::string(first, last);
}

std::string regex_literal(std::string_view input) {
    static constexpr std::string_view metacharacters = R"(\.^$|()[]{}*+?)";
    std::string out;
    out.reserve(input.size() * 2);
    for (const char ch : input) {
        if (metacharacters.find(ch) != std::string_view::npos) {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

bool command_section(const std::string_view section) {
    // Section 1 is executable programs / shell commands. Section 8 is system
    // administration commands. A simple prefix also admits subsections such as 1p.
    return section.starts_with('1') || section.starts_with('8');
}


} // namespace

bool ManSource::available() const {
    return system::find_executable("apropos").has_value() ||
           system::find_executable("man").has_value();
}

std::vector<std::string> ManSource::search_terms(const Query& query) {
    return query::retrieval_terms(query);
}

std::vector<ManSource::ManualEntry> ManSource::parse_apropos(const std::string_view output) {
    std::vector<ManualEntry> entries;
    std::istringstream stream{std::string(output)};
    std::string line;

    while (std::getline(stream, line)) {
        // Typical man-db format:
        // grep (1)             - print lines that match patterns
        const std::size_t separator = line.find(" - ");
        if (separator == std::string::npos) {
            continue;
        }

        const std::string left = trim(line.substr(0, separator));
        const std::string summary = trim(line.substr(separator + 3));
        if (left.empty() || summary.empty()) {
            continue;
        }

        const std::size_t close = left.rfind(')');
        const std::size_t open = close == std::string::npos
            ? std::string::npos
            : left.rfind('(', close);
        if (open == std::string::npos || close == std::string::npos || open == 0) {
            continue;
        }

        std::string names = trim(left.substr(0, open));
        const std::string section = trim(left.substr(open + 1, close - open - 1));
        if (!command_section(section)) {
            continue;
        }

        std::size_t start = 0;
        while (start <= names.size()) {
            const std::size_t comma = names.find(',', start);
            std::string name = trim(names.substr(start, comma - start));
            if (!name.empty()) {
                const std::size_t whitespace = name.find_first_of(" \t");
                if (whitespace != std::string::npos) {
                    name.resize(whitespace);
                }

                if (!name.empty()) {
                    entries.push_back(ManualEntry{
                        .command = std::move(name),
                        .section = section,
                        .summary = summary,
                    });
                }
            }

            if (comma == std::string::npos) {
                break;
            }
            start = comma + 1;
        }
    }

    return entries;
}

double ManSource::score_entry(const Query& query, const ManualEntry& entry) {
    auto relevance = query::score_text(query, entry.command, entry.summary, 0.99, 0.93);
    if (relevance.score > 0.0 && entry.section.starts_with('1')) {
        relevance.score = std::min(0.96, relevance.score + 0.03);
    }
    return relevance.score;
}

std::vector<Candidate> ManSource::catalog() {
    const bool has_apropos = system::find_executable("apropos").has_value();
    const bool has_man = system::find_executable("man").has_value();
    if (!has_apropos && !has_man) {
        return {};
    }

    std::vector<std::string> arguments;
    if (has_apropos) {
        arguments = {"apropos", "-l", "-s", "1,8", "-r", "."};
    } else {
        arguments = {"man", "-k", "-s", "1,8", "-r", "."};
    }

    const auto process = system::run_capture_stdout(arguments, 16 * 1024 * 1024);
    std::unordered_map<std::string, Candidate> candidates;

    for (const auto& entry : parse_apropos(process.stdout_text)) {
        const auto executable = system::find_executable(entry.command);
        if (!executable) {
            continue;
        }

        Candidate incoming{
            .command = entry.command,
            .path = executable->string(),
            .summary = entry.summary,
            .source = "man",
            .package = {},
            .repository = {},
            .package_version = {},
            .installed = true,
            .repository_available = false,
            .cli_capable = true,
            .gui_capable = false,
            .matched_terms = {},
            .provided_commands = {},
            .descriptive_evidence = {},
            .examples = {},
            .learning_resources = {},
            .evidence_trace = {},
            .base_merge_trace = {},
            .score = 0.0,
            .ranking = std::nullopt,
        };

        auto [it, inserted] = candidates.try_emplace(incoming.command, incoming);
        if (!inserted && it->second.summary.size() < incoming.summary.size()) {
            it->second = std::move(incoming);
        }
    }

    std::vector<Candidate> result;
    result.reserve(candidates.size());
    for (auto& [_, candidate] : candidates) {
        result.push_back(std::move(candidate));
    }
    return result;
}

std::vector<Candidate> ManSource::search(const Query& query) const {
    const auto terms = search_terms(query);
    if (terms.empty()) {
        return {};
    }

    const bool has_apropos = system::find_executable("apropos").has_value();
    const bool has_man = system::find_executable("man").has_value();
    if (!has_apropos && !has_man) {
        return {};
    }

    std::vector<std::string> arguments;
    if (has_apropos) {
        arguments = {"apropos", "-s", "1,8", "--"};
    } else {
        arguments = {"man", "-k", "-s", "1,8", "--"};
    }

    for (const auto& term : terms) {
        arguments.push_back(regex_literal(term));
    }

    // One broad native lookup. apropos uses OR semantics for multiple keywords;
    // Acclorite then performs the stricter concept-aware scoring locally.
    const auto process = system::run_capture_stdout(arguments);

    std::unordered_map<std::string, Candidate> candidates;
    for (const auto& entry : parse_apropos(process.stdout_text)) {
        // ManSource represents installed local tools. Non-executable documentation
        // (library calls, constants, config names) is noise for tool discovery.
        const auto executable = system::find_executable(entry.command);
        if (!executable) {
            continue;
        }

        auto relevance = query::score_text(query, entry.command, entry.summary, 0.99, 0.93);
        if (relevance.score <= 0.0) {
            continue;
        }

        std::vector<ranking::RankingAdjustment> source_adjustments;
        double score = relevance.score;
        if (entry.section.starts_with('1')) {
            const double before = score;
            score = std::min(0.96, score + 0.03);
            if (score != before) {
                source_adjustments.push_back(ranking::RankingAdjustment{
                    .id = "manual-section-1",
                    .label = "manual section 1 command bonus",
                    .kind = ranking::AdjustmentKind::Add,
                    .value = score - before,
                    .before = before,
                    .after = score,
                });
            }
        }

        Candidate incoming{
            .command = entry.command,
            .path = executable->string(),
            .summary = entry.summary,
            .source = "man",
            .package = {},
            .repository = {},
            .package_version = {},
            .installed = true,
            .repository_available = false,
            .cli_capable = true,
            .gui_capable = false,
            .matched_terms = {},
            .provided_commands = {},
            .descriptive_evidence = {},
            .examples = {},
            .learning_resources = {},
            .evidence_trace = {},
            .base_merge_trace = {},
            .semantic_fit = relevance.semantic_fit,
            .score = score,
            .ranking = std::nullopt,
        };

        incoming.matched_terms = relevance.matched_terms;
        if (query.explain_ranking) {
            incoming.evidence_trace.push_back(ranking::semantic_evidence(
                "man", "manual synopsis semantic match", relevance,
                std::move(source_adjustments), incoming.score
            ));
        }

        auto [it, inserted] = candidates.try_emplace(incoming.command, incoming);
        if (!inserted && incoming.score > it->second.score) {
            it->second = std::move(incoming);
        }
    }

    std::vector<Candidate> result;
    result.reserve(candidates.size());
    for (auto& [_, candidate] : candidates) {
        result.push_back(std::move(candidate));
    }
    return result;
}

} // namespace acclorite
