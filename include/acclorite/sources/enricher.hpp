#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>

#include "acclorite/core/candidate.hpp"
#include "acclorite/core/query.hpp"

namespace acclorite {

class CandidateEnricher {
public:
    virtual ~CandidateEnricher() = default;

    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual std::string_view diagnostic_name() const { return "enricher"; }
    virtual void enrich(const Query& query, Candidate& candidate) const = 0;
    virtual void enrich_many(
        const Query& query,
        std::vector<Candidate>& candidates,
        const std::size_t count
    ) const {
        const auto bounded = std::min(count, candidates.size());
        for (std::size_t i = 0; i < bounded; ++i) {
            enrich(query, candidates[i]);
        }
    }
};

} // namespace acclorite
