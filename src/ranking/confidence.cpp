#include "acclorite/ranking/confidence.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <ranges>
#include <string>
#include <string_view>

#include "acclorite/query/lexicon.hpp"
#include "acclorite/ranking/preference.hpp"

namespace acclorite {

std::string_view ambiguity_state_name(const AmbiguityState state) {
    switch (state) {
        case AmbiguityState::Clear: return "clear";
        case AmbiguityState::Competitive: return "competitive";
        case AmbiguityState::Ambiguous: return "ambiguous";
        case AmbiguityState::LowConfidence: return "low-confidence";
        case AmbiguityState::NoResult: return "no-result";
    }
    return "no-result";
}

std::string_view confidence_level_name(const double confidence) {
    if (confidence >= 0.75) return "high";
    if (confidence >= 0.55) return "medium";
    return "low";
}

} // namespace acclorite

namespace acclorite::ranking {
namespace {

struct QueryAmbiguity {
    bool ambiguous{false};
    double interpretation_confidence{0.82};
    std::vector<std::string> signals;
    std::vector<ClarificationOption> clarifications;
};

bool explicitly_mentions(
    const Query& query,
    const std::initializer_list<std::string_view> terms
) {
    return std::ranges::any_of(query.tokens, [&](const std::string& token) {
        return std::ranges::find(terms, std::string_view(token)) != terms.end();
    });
}

bool has_explicit_concept(const Query& query, const std::string_view term) {
    const auto groups = query::concept_groups(query);
    return std::ranges::any_of(groups, [&](const query::ConceptGroup& group) {
        return group.term == term && !group.implicit;
    });
}

QueryAmbiguity inspect_query_ambiguity(const Query& query) {
    QueryAmbiguity result;

    // "archive files" describes a domain but not the operation direction. Create,
    // extract, and browse/manage are materially different intents with different
    // good front doors. Keep this small and benchmark-driven rather than growing
    // a giant hand-written ambiguity ontology.
    if (has_explicit_concept(query, "archive") &&
        !explicitly_mentions(query, {
            "create", "make", "build", "pack", "package", "compress",
            "extract", "unpack", "decompress", "restore",
            "browse", "open", "manage", "list", "view", "inspect"
        })) {
        result.ambiguous = true;
        result.interpretation_confidence = 0.52;
        result.signals.push_back("archive operation is underspecified");
        result.clarifications = {
            {"archive-create", "create/compress an archive"},
            {"archive-extract", "extract/unpack an archive"},
            {"archive-manage", "browse/manage archives"},
        };
    }

    return result;
}

double candidate_utility(const Candidate& candidate) {
    return candidate.ranking_utility > 0.0 ? candidate.ranking_utility : candidate.score;
}

double separation_score(
    const Query& query,
    const Candidate& top,
    const Candidate* second,
    double& semantic_gap,
    double& utility_gap
) {
    if (!second) {
        semantic_gap = effective_semantic_fit(query, top);
        utility_gap = candidate_utility(top);
        return 1.0;
    }

    const double top_fit = effective_semantic_fit(query, top);
    const double second_fit = effective_semantic_fit(query, *second);
    semantic_gap = std::max(0.0, top_fit - second_fit);
    utility_gap = std::max(0.0, candidate_utility(top) - candidate_utility(*second));

    const int top_tier = semantic_fit_tier(top_fit);
    const int second_tier = semantic_fit_tier(second_fit);
    if (top_tier > second_tier) {
        const int tier_gap = top_tier - second_tier;
        return std::clamp(0.78 + (0.07 * static_cast<double>(tier_gap)) +
                              std::min(0.08, semantic_gap), 0.0, 1.0);
    }

    const double utility_component = std::clamp(utility_gap / 0.10, 0.0, 1.0);
    const double semantic_component = std::clamp(semantic_gap / 0.04, 0.0, 1.0);
    return std::clamp(0.25 + (0.55 * utility_component) +
                              (0.20 * semantic_component), 0.0, 1.0);
}

double baseline_interpretation_confidence(const Query& query) {
    // Default Discover is a safe product default, not a claim that the language
    // recognizer is only 35% sure what the user means. Keep frame confidence as a
    // separate reported signal while mapping it conservatively into interpretation.
    if (query.frame.frame == QueryFrame::Discover && !query.frame.explicit_frame) {
        return query.frame.confidence <= 0.46 ? 0.82 : 0.86;
    }
    if (query.frame.explicit_frame) {
        return std::clamp(0.82 + (0.15 * query.frame.confidence), 0.0, 0.98);
    }
    return std::clamp(0.68 + (0.25 * query.frame.confidence), 0.0, 0.90);
}

} // namespace

ConfidenceAssessment assess_confidence(
    const Query& query,
    const std::vector<Candidate>& ranked_candidates
) {
    ConfidenceAssessment assessment;
    auto& confidence = assessment.confidence;

    if (ranked_candidates.empty()) {
        confidence.ambiguity = AmbiguityState::NoResult;
        confidence.interpretation = baseline_interpretation_confidence(query);
        confidence.signals.push_back("no ranked candidate is available");
        return assessment;
    }

    const Candidate& top = ranked_candidates.front();
    const Candidate* second = ranked_candidates.size() > 1 ? &ranked_candidates[1] : nullptr;
    const double top_fit = effective_semantic_fit(query, top);
    const int top_tier = semantic_fit_tier(top_fit);

    confidence.interpretation = baseline_interpretation_confidence(query);
    confidence.separation = separation_score(
        query, top, second, confidence.semantic_gap, confidence.utility_gap
    );

    // Named-entity frames already resolved the user's entity explicitly. Multiple
    // candidates in Compare are expected operands, not competing interpretations.
    const bool entity_resolved = !query.targets.empty() &&
        (query.frame.frame == QueryFrame::Explain || query.frame.frame == QueryFrame::Compare);
    if (entity_resolved) {
        confidence.interpretation = std::max(confidence.interpretation, 0.95);
        confidence.separation = std::max(confidence.separation, 0.92);
        confidence.ambiguity = AmbiguityState::Clear;
        confidence.signals.push_back("explicit named target resolved");
    }

    const auto query_ambiguity = inspect_query_ambiguity(query);
    if (!entity_resolved && query_ambiguity.ambiguous) {
        confidence.interpretation = query_ambiguity.interpretation_confidence;
        confidence.ambiguity = AmbiguityState::Ambiguous;
        confidence.signals = query_ambiguity.signals;
        assessment.clarifications = query_ambiguity.clarifications;
    } else if (!entity_resolved && top_fit < 0.70) {
        confidence.ambiguity = AmbiguityState::LowConfidence;
        confidence.signals.push_back("best semantic fit is weak");
    } else if (!entity_resolved && second) {
        const double second_fit = effective_semantic_fit(query, *second);
        const int second_tier = semantic_fit_tier(second_fit);
        const bool near_equivalent = top_tier == second_tier &&
            std::abs(top_fit - second_fit) <= 0.015 &&
            std::abs(candidate_utility(top) - candidate_utility(*second)) <= 0.015;
        if (near_equivalent) {
            confidence.ambiguity = AmbiguityState::Competitive;
            confidence.signals.push_back("top candidates are nearly tied for the same interpreted intent");
        } else if (!entity_resolved) {
            confidence.ambiguity = AmbiguityState::Clear;
            if (top_tier > second_tier) {
                confidence.signals.push_back("top candidate has a stronger semantic-fit tier");
            } else {
                confidence.signals.push_back("top candidate is meaningfully separated within its semantic tier");
            }
        }
    } else if (!entity_resolved) {
        confidence.ambiguity = AmbiguityState::Clear;
        confidence.signals.push_back("single viable candidate");
    }

    confidence.top_candidate = std::clamp(
        (0.62 * top_fit) +
        (0.20 * confidence.separation) +
        (0.18 * confidence.interpretation),
        0.0,
        0.99
    );

    if (confidence.ambiguity == AmbiguityState::Ambiguous) {
        confidence.top_candidate *= 0.82;
    } else if (confidence.ambiguity == AmbiguityState::LowConfidence) {
        confidence.top_candidate = std::min(confidence.top_candidate, 0.58);
    }
    confidence.top_candidate = std::clamp(confidence.top_candidate, 0.0, 0.99);

    return assessment;
}

} // namespace acclorite::ranking
