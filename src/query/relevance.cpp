#include "acclorite/query/relevance.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include "acclorite/query/fuzzy.hpp"
#include "acclorite/query/lexicon.hpp"

namespace acclorite::query {
namespace {

std::string lower(std::string_view input) {
    std::string out(input);
    std::ranges::transform(out, out.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
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

std::string light_stem(std::string_view word) {
    std::string stem(word);
    if (stem.size() <= 4) {
        return stem;
    }

    const auto strip = [&](const std::string_view suffix, const std::size_t minimum_root) {
        if (stem.size() > suffix.size() + minimum_root && stem.ends_with(suffix)) {
            stem.resize(stem.size() - suffix.size());
            return true;
        }
        return false;
    };

    if (strip("ing", 4) || strip("ers", 4) || strip("ies", 4) ||
        strip("er", 4) || strip("ed", 4) || strip("es", 4)) {
        return stem;
    }
    // Do not turn singular words such as "process" into "proces". A trailing
    // s is a useful plural heuristic only when it is not part of an -ss ending.
    if (!stem.ends_with("ss") && strip("s", 4)) {
        return stem;
    }
    return stem;
}

bool related_word(const std::string_view candidate, const std::string_view term) {
    if (candidate == term) {
        return true;
    }

    if (candidate.size() >= 4 && term.size() >= 4 && light_stem(candidate) == light_stem(term)) {
        return true;
    }

    if (candidate.size() < 4 || term.size() < 4) {
        return false;
    }

    // Fuzzy matching is intentionally conservative here. It exists primarily for
    // small spelling mistakes, not as a substitute for the semantic lexicon.
    const double threshold = std::min(candidate.size(), term.size()) <= 5 ? 0.80 : 0.84;

    // Edit distance can never be smaller than the length difference. If even that
    // theoretical best case cannot reach the requested similarity, avoid invoking
    // OSA entirely. This is especially valuable while rescoring broad FTS recall
    // sets, where most word/intent pairs are obviously different lengths.
    const std::size_t longest = std::max(candidate.size(), term.size());
    const std::size_t length_difference =
        candidate.size() > term.size() ? candidate.size() - term.size() : term.size() - candidate.size();
    const double best_possible = 1.0 -
        (static_cast<double>(length_difference) / static_cast<double>(longest));
    if (best_possible < threshold) {
        return false;
    }

    return fuzzy::similarity(candidate, term) >= threshold;
}

std::optional<std::string> related_token(
    const std::vector<std::string>& haystack,
    const std::string_view needle
) {
    const auto match = std::ranges::find_if(haystack, [&](const std::string& word) {
        return related_word(word, needle);
    });
    if (match == haystack.end()) {
        return std::nullopt;
    }
    return *match;
}

bool contains_any_related(
    const std::vector<std::string>& haystack,
    const std::initializer_list<std::string_view> needles
) {
    return std::ranges::any_of(haystack, [&](const std::string& word) {
        return std::ranges::any_of(needles, [&](const std::string_view needle) {
            return related_word(word, needle);
        });
    });
}

struct FrameBiasResult {
    double score{0.0};
    std::string effect;
};

FrameBiasResult apply_frame_bias(
    const Query& query,
    const std::vector<std::string>& name_words,
    const std::vector<std::string>& description_words,
    double score
) {
    FrameBiasResult result{.score = score, .effect = {}};
    if (score <= 0.0) {
        return result;
    }

    std::vector<std::string> all_words = name_words;
    all_words.insert(all_words.end(), description_words.begin(), description_words.end());

    const bool inspect_evidence = contains_any_related(
        all_words, {"inspect", "investigate", "show", "list", "view", "display",
                    "monitor", "watch", "status", "check", "report"}
    );
    const bool modify_evidence = contains_any_related(
        all_words, {"edit", "editor", "modify", "change", "configure", "configuration",
                    "manage", "management", "manager", "settings", "control"}
    );

    if (query.frame.frame == QueryFrame::Inspect) {
        if (inspect_evidence) {
            result.score += 0.025;
            result.effect = "inspect evidence +0.025";
        }
        if (modify_evidence && !inspect_evidence) {
            result.score *= 0.72;
            result.effect = "modify-only evidence ×0.72";
        }
    } else if (query.frame.frame == QueryFrame::Modify) {
        if (modify_evidence) {
            result.score += 0.025;
            result.effect = "modify evidence +0.025";
        }
        if (inspect_evidence && !modify_evidence) {
            result.score *= 0.78;
            result.effect = "inspect-only evidence ×0.78";
        }
    }

    result.score = std::clamp(result.score, 0.0, 1.0);
    return result;
}

struct TermQualityResult {
    double quality{0.0};
    SemanticMatchLocation location{SemanticMatchLocation::None};
    SemanticMatchKind kind{SemanticMatchKind::None};
    std::string matched_via;
    std::string matched_token;
};

TermQualityResult term_quality(
    const ConceptGroup& group,
    const std::vector<std::string>& name_words,
    const std::vector<std::string>& description_words
) {
    if (auto token = related_token(name_words, group.term)) {
        return TermQualityResult{
            .quality = 1.00,
            .location = SemanticMatchLocation::Name,
            .kind = SemanticMatchKind::Canonical,
            .matched_via = group.term,
            .matched_token = std::move(*token),
        };
    }
    if (auto token = related_token(description_words, group.term)) {
        return TermQualityResult{
            .quality = 0.94,
            .location = SemanticMatchLocation::Description,
            .kind = SemanticMatchKind::Canonical,
            .matched_via = group.term,
            .matched_token = std::move(*token),
        };
    }

    for (const auto& alternative : group.alternatives) {
        if (alternative == group.term) {
            continue;
        }
        if (auto token = related_token(name_words, alternative)) {
            return TermQualityResult{
                .quality = 0.80,
                .location = SemanticMatchLocation::Name,
                .kind = SemanticMatchKind::Alternative,
                .matched_via = alternative,
                .matched_token = std::move(*token),
            };
        }
        if (auto token = related_token(description_words, alternative)) {
            return TermQualityResult{
                .quality = 0.70,
                .location = SemanticMatchLocation::Description,
                .kind = SemanticMatchKind::Alternative,
                .matched_via = alternative,
                .matched_token = std::move(*token),
            };
        }
    }

    return {};
}

} // namespace

RelevanceMatch score_text(
    const Query& query,
    const std::string_view name,
    const std::string_view description,
    const double exact_name_score,
    const double full_match_ceiling
) {
    RelevanceMatch result;
    if (query.normalized.empty()) {
        return result;
    }

    const auto groups = concept_groups(query);
    const std::string normalized_name = lower(name);

    if (normalized_name == query.normalized) {
        result.semantic_fit = exact_name_score;
        result.score = exact_name_score;
        for (const auto& group : groups) {
            if (!group.implicit) {
                result.matched_terms.push_back(group.term);
            }
        }
        if (query.explain_ranking) {
            SemanticBreakdown breakdown{
                .exact_name_match = true,
                .total_weight = 0.0,
                .matched_weight = 0.0,
                .coverage = 1.0,
                .average_quality = 1.0,
                .formula_score = exact_name_score,
                .full_match_ceiling = full_match_ceiling,
                .after_ceiling = exact_name_score,
                .frame_before = exact_name_score,
                .frame_after = exact_name_score,
                .frame_effect = "exact name match bypasses semantic formula",
                .concepts = {},
            };
            for (const auto& group : groups) {
                breakdown.total_weight += group.weight;
                breakdown.matched_weight += group.weight;
                breakdown.concepts.push_back(ConceptScoreTrace{
                    .term = group.term,
                    .role = group.role,
                    .weight = group.weight,
                    .implicit = group.implicit,
                    .quality = 1.0,
                    .location = SemanticMatchLocation::Name,
                    .kind = SemanticMatchKind::Canonical,
                    .matched_via = group.term,
                    .matched_token = normalized_name,
                });
            }
            result.breakdown = std::move(breakdown);
        }
        return result;
    }

    if (groups.empty()) {
        return result;
    }

    const auto name_words = words(normalized_name);
    const auto description_words = words(description);

    double total_weight = 0.0;
    double matched_weight = 0.0;
    double weighted_quality = 0.0;
    std::vector<ConceptScoreTrace> concept_trace;
    if (query.explain_ranking) {
        concept_trace.reserve(groups.size());
    }

    for (const auto& group : groups) {
        total_weight += group.weight;
        const auto quality = term_quality(group, name_words, description_words);
        if (quality.quality > 0.0) {
            matched_weight += group.weight;
            weighted_quality += quality.quality * group.weight;
            if (!group.implicit) {
                result.matched_terms.push_back(group.term);
            }
        }

        if (query.explain_ranking) {
            concept_trace.push_back(ConceptScoreTrace{
                .term = group.term,
                .role = group.role,
                .weight = group.weight,
                .implicit = group.implicit,
                .quality = quality.quality,
                .location = quality.location,
                .kind = quality.kind,
                .matched_via = quality.matched_via,
                .matched_token = quality.matched_token,
            });
        }
    }

    if (matched_weight <= 0.0 || total_weight <= 0.0) {
        if (query.explain_ranking) {
            result.breakdown = SemanticBreakdown{
                .exact_name_match = false,
                .total_weight = total_weight,
                .matched_weight = 0.0,
                .coverage = 0.0,
                .average_quality = 0.0,
                .formula_score = 0.0,
                .full_match_ceiling = full_match_ceiling,
                .after_ceiling = 0.0,
                .frame_before = 0.0,
                .frame_after = 0.0,
                .frame_effect = {},
                .concepts = std::move(concept_trace),
            };
        }
        return result;
    }

    const double coverage = matched_weight / total_weight;
    const double average_quality = weighted_quality / matched_weight;

    double formula_score = 0.0;
    if (coverage >= 0.999) {
        formula_score = 0.68 + (0.22 * average_quality);
    } else {
        // Partial intent coverage is weighted by semantic role. Matching the action
        // in "archive files" is much stronger evidence than merely matching "files".
        formula_score = 0.10 + (0.48 * coverage) + (0.12 * average_quality);
    }

    const double after_ceiling = std::min(formula_score, full_match_ceiling);
    const auto frame = apply_frame_bias(query, name_words, description_words, after_ceiling);
    result.semantic_fit = frame.score;
    result.score = frame.score;

    if (query.explain_ranking) {
        result.breakdown = SemanticBreakdown{
            .exact_name_match = false,
            .total_weight = total_weight,
            .matched_weight = matched_weight,
            .coverage = coverage,
            .average_quality = average_quality,
            .formula_score = formula_score,
            .full_match_ceiling = full_match_ceiling,
            .after_ceiling = after_ceiling,
            .frame_before = after_ceiling,
            .frame_after = frame.score,
            .frame_effect = frame.effect,
            .concepts = std::move(concept_trace),
        };
    }

    return result;
}

} // namespace acclorite::query
