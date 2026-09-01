#include "acclorite/core/search_engine.hpp"

#include <algorithm>
#include <ranges>
#include <string>
#include <unordered_map>
#include <utility>

namespace acclorite {
namespace {

bool is_placeholder_summary(const std::string& summary) {
    return summary.empty() || summary == "Executable available in PATH";
}

void merge_terms(std::vector<std::string>& target, const std::vector<std::string>& incoming) {
    for (const auto& term : incoming) {
        if (std::ranges::find(target, term) == target.end()) {
            target.push_back(term);
        }
    }
}

void merge_candidate(Candidate& target, Candidate incoming) {
    const double old_score = target.score;
    const double incoming_score = incoming.score;

    if (target.path.empty() && !incoming.path.empty()) {
        target.path = std::move(incoming.path);
    }

    if (is_placeholder_summary(target.summary) && !is_placeholder_summary(incoming.summary)) {
        target.summary = std::move(incoming.summary);
    }

    if (target.source.empty()) {
        target.source = std::move(incoming.source);
    } else if (!incoming.source.empty() && target.source.find(incoming.source) == std::string::npos) {
        target.source += "+" + incoming.source;
    }

    target.installed = target.installed || incoming.installed;
    target.repository_available = target.repository_available || incoming.repository_available;
    target.cli_capable = target.cli_capable || incoming.cli_capable;
    target.gui_capable = target.gui_capable || incoming.gui_capable;
    merge_terms(target.matched_terms, incoming.matched_terms);

    // Multiple independent sources agreeing on a candidate should help, but never
    // overpower a genuinely strong match. Keep the boost intentionally small.
    const double strongest = std::max(old_score, incoming_score);
    const double supporting = std::min(old_score, incoming_score);
    target.score = std::min(1.0, strongest + (0.08 * supporting));
}

} // namespace

void SearchEngine::add_source(std::unique_ptr<KnowledgeSource> source) {
    sources_.push_back(std::move(source));
}

SearchResult SearchEngine::search(const Query& query, const std::size_t limit) const {
    SearchResult result{
        .raw_query = query.raw,
        .normalized_query = query.normalized,
        .candidates = {},
    };

    std::unordered_map<std::string, Candidate> merged;

    for (const auto& source : sources_) {
        if (!source || !source->available()) {
            continue;
        }

        for (auto candidate : source->search(query)) {
            auto [it, inserted] = merged.try_emplace(candidate.command, candidate);
            if (!inserted) {
                merge_candidate(it->second, std::move(candidate));
            }
        }
    }

    result.candidates.reserve(merged.size());
    for (auto& [_, candidate] : merged) {
        result.candidates.push_back(std::move(candidate));
    }

    std::ranges::sort(result.candidates, [](const Candidate& lhs, const Candidate& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        return lhs.command < rhs.command;
    });

    if (result.candidates.size() > limit) {
        result.candidates.resize(limit);
    }

    return result;
}

} // namespace acclorite
