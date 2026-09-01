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
#include "acclorite/system/executable.hpp"
#include "acclorite/system/process.hpp"

namespace acclorite {
namespace {

std::string lower(std::string_view input) {
    std::string out(input);
    std::ranges::transform(out, out.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

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

std::vector<std::string> words(std::string_view text) {
    std::vector<std::string> result;
    std::string current;

    for (const unsigned char ch : text) {
        if (std::isalnum(ch) || ch == '+' || ch == '#') {
            current.push_back(static_cast<char>(std::tolower(ch)));
        } else if (!current.empty()) {
            result.push_back(std::move(current));
            current.clear();
        }
    }

    if (!current.empty()) {
        result.push_back(std::move(current));
    }

    return result;
}

bool related_word(const std::string_view candidate, const std::string_view term) {
    if (candidate == term) {
        return true;
    }

    if (candidate.size() < 4 || term.size() < 4) {
        return false;
    }

    const std::size_t limit = std::min(candidate.size(), term.size());
    std::size_t common = 0;
    while (common < limit && candidate[common] == term[common]) {
        ++common;
    }

    // Cheap morphology: archive/archiver/archiving, process/processes,
    // monitor/monitoring. Typo tolerance comes later via RapidFuzz.
    return common >= 4 && common + 2 >= std::min(candidate.size(), term.size());
}

double term_quality(
    const query::ConceptGroup& group,
    const std::vector<std::string>& command_words,
    const std::vector<std::string>& summary_words
) {
    const auto contains = [](const std::vector<std::string>& haystack, const std::string_view needle) {
        return std::ranges::any_of(haystack, [&](const std::string& word) {
            return related_word(word, needle);
        });
    };

    if (contains(command_words, group.term)) {
        return 1.00;
    }
    if (contains(summary_words, group.term)) {
        return 0.94;
    }

    for (const auto& alternative : group.alternatives) {
        if (alternative == group.term) {
            continue;
        }
        if (contains(command_words, alternative)) {
            return 0.80;
        }
        if (contains(summary_words, alternative)) {
            return 0.70;
        }
    }

    return 0.0;
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
    if (query.normalized.empty()) {
        return 0.0;
    }

    const std::string command = lower(entry.command);
    if (command == query.normalized) {
        return 0.99;
    }

    const auto groups = query::concept_groups(query);
    if (groups.empty()) {
        return 0.0;
    }

    const auto command_words = words(command);
    const auto summary_words = words(entry.summary);

    std::size_t matched_groups = 0;
    double quality_sum = 0.0;
    for (const auto& group : groups) {
        const double quality = term_quality(group, command_words, summary_words);
        if (quality > 0.0) {
            ++matched_groups;
            quality_sum += quality;
        }
    }

    if (matched_groups == 0) {
        return 0.0;
    }

    const double coverage = static_cast<double>(matched_groups) /
                            static_cast<double>(groups.size());
    const double average_quality = quality_sum / static_cast<double>(matched_groups);

    double score = 0.0;
    if (matched_groups == groups.size()) {
        score = 0.68 + (0.22 * average_quality);
    } else {
        // Partial matches are recall candidates, not confident answers. This is the
        // guardrail that stops one generic word from dominating a multi-word intent.
        score = 0.14 + (0.32 * coverage) + (0.16 * average_quality);
    }

    if (entry.section.starts_with('1')) {
        score += 0.03;
    }

    return std::min(score, 0.96);
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
            .installed = true,
            .repository_available = false,
            .cli_capable = true,
            .gui_capable = false,
            .matched_terms = {},
            .score = 0.0,
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

        const double score = score_entry(query, entry);
        if (score <= 0.0) {
            continue;
        }

        Candidate incoming{
            .command = entry.command,
            .path = executable->string(),
            .summary = entry.summary,
            .source = "man",
            .installed = true,
            .repository_available = false,
            .cli_capable = true,
            .gui_capable = false,
            .matched_terms = {},
            .score = score,
        };

        for (const auto& group : query::concept_groups(query)) {
            const auto command_words = words(incoming.command);
            const auto summary_words = words(incoming.summary);
            if (term_quality(group, command_words, summary_words) > 0.0) {
                incoming.matched_terms.push_back(group.term);
            }
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
