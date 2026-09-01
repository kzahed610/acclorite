#include "acclorite/core/search_engine.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>

namespace acclorite {

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
            if (!inserted && candidate.score > it->second.score) {
                it->second = std::move(candidate);
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
