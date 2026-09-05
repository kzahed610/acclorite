#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "acclorite/core/query.hpp"
#include "acclorite/query/lexicon.hpp"

namespace acclorite::query {

enum class SemanticMatchLocation {
    None,
    Name,
    Description,
};

enum class SemanticMatchKind {
    None,
    Canonical,
    Alternative,
};

struct ConceptScoreTrace {
    std::string term;
    ConceptRole role{ConceptRole::Subject};
    double weight{0.0};
    bool implicit{false};
    double quality{0.0};
    SemanticMatchLocation location{SemanticMatchLocation::None};
    SemanticMatchKind kind{SemanticMatchKind::None};
    std::string matched_via;
    std::string matched_token;
};

struct SemanticBreakdown {
    bool exact_name_match{false};
    double total_weight{0.0};
    double matched_weight{0.0};
    double coverage{0.0};
    double average_quality{0.0};
    double formula_score{0.0};
    double full_match_ceiling{0.0};
    double after_ceiling{0.0};
    double frame_before{0.0};
    double frame_after{0.0};
    std::string frame_effect;
    std::vector<ConceptScoreTrace> concepts;
};

struct RelevanceMatch {
    // Pure intent fit after semantic formula/frame alignment, before source-specific
    // confidence bonuses such as BM25 position or repository metadata support.
    double semantic_fit{0.0};
    double score{0.0};
    std::vector<std::string> matched_terms;
    std::optional<SemanticBreakdown> breakdown;
};

[[nodiscard]] RelevanceMatch score_text(
    const Query& query,
    std::string_view name,
    std::string_view description,
    double exact_name_score = 0.99,
    double full_match_ceiling = 0.96
);

[[nodiscard]] inline const char* semantic_match_location_name(const SemanticMatchLocation location) {
    switch (location) {
        case SemanticMatchLocation::Name: return "name";
        case SemanticMatchLocation::Description: return "description";
        case SemanticMatchLocation::None: return "none";
    }
    return "none";
}

[[nodiscard]] inline const char* semantic_match_kind_name(const SemanticMatchKind kind) {
    switch (kind) {
        case SemanticMatchKind::Canonical: return "canonical";
        case SemanticMatchKind::Alternative: return "alternative";
        case SemanticMatchKind::None: return "none";
    }
    return "none";
}

[[nodiscard]] inline const char* concept_role_name(const ConceptRole role) {
    switch (role) {
        case ConceptRole::Action: return "action";
        case ConceptRole::Subject: return "subject";
        case ConceptRole::Context: return "context";
    }
    return "subject";
}

} // namespace acclorite::query
