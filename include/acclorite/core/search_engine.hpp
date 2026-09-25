#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "acclorite/core/candidate_hint.hpp"
#include "acclorite/core/query.hpp"
#include "acclorite/core/result.hpp"
#include "acclorite/sources/source.hpp"
#include "acclorite/sources/enricher.hpp"
#include "acclorite/sources/location_source.hpp"
#include "acclorite/guidance/provider.hpp"
#include "acclorite/syntax/provider.hpp"

namespace acclorite {

class SearchEngine {
public:
    void add_source(std::unique_ptr<KnowledgeSource> source);
    void add_location_source(std::unique_ptr<LocationSource> source);
    void add_enricher(std::unique_ptr<CandidateEnricher> enricher);
    void add_guidance_provider(std::unique_ptr<GuidanceProvider> provider);
    void add_syntax_provider(std::unique_ptr<CommandSyntaxProvider> provider);

    [[nodiscard]] SearchResult search(
        const Query& query,
        std::size_t limit = 10,
        bool explain_ranking = false,
        bool profile = false,
        std::span<const CandidateHint> candidate_hints = {}
    ) const;

private:
    std::vector<std::unique_ptr<KnowledgeSource>> sources_;
    std::vector<std::unique_ptr<LocationSource>> location_sources_;
    std::vector<std::unique_ptr<CandidateEnricher>> enrichers_;
    std::vector<std::unique_ptr<GuidanceProvider>> guidance_providers_;
    std::vector<std::unique_ptr<CommandSyntaxProvider>> syntax_providers_;
};

} // namespace acclorite
