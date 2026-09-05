#include "acclorite/core/search_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ranges>
#include <optional>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "acclorite/query/frame.hpp"
#include "acclorite/query/targets.hpp"
#include "acclorite/ranking/preference.hpp"
#include "acclorite/ranking/confidence.hpp"

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

double candidate_utility(const Candidate& candidate) {
    return candidate.ranking_utility > 0.0 ? candidate.ranking_utility : candidate.score;
}

void set_candidate_utility(Candidate& candidate, const double utility) {
    candidate.ranking_utility = utility;
    candidate.score = std::clamp(utility, 0.0, 1.0);
}

void invalidate_candidate_ordering(Candidate& candidate) {
    candidate.ordering_semantic_fit = -1.0;
    candidate.ordering_semantic_tier = -1;
}

void prepare_candidate_ordering(const Query& query, Candidate& candidate) {
    const auto assessment = ranking::assess_semantic_fit(query, candidate);
    candidate.ordering_semantic_fit = assessment.effective_fit;
    candidate.ordering_semantic_tier = ranking::semantic_fit_tier(assessment.effective_fit);
}

double candidate_semantic_fit(const Query& query, const Candidate& candidate) {
    if (candidate.ordering_semantic_fit >= 0.0) {
        return candidate.ordering_semantic_fit;
    }
    return ranking::effective_semantic_fit(query, candidate);
}

int candidate_semantic_tier(const Query& query, const Candidate& candidate) {
    if (candidate.ordering_semantic_tier >= 0) {
        return candidate.ordering_semantic_tier;
    }
    return ranking::semantic_fit_tier(candidate_semantic_fit(query, candidate));
}

bool candidate_rank_less(const Query& query, const Candidate& lhs, const Candidate& rhs) {
    const int lhs_tier = candidate_semantic_tier(query, lhs);
    const int rhs_tier = candidate_semantic_tier(query, rhs);
    if (lhs_tier != rhs_tier) {
        return lhs_tier > rhs_tier;
    }
    if (candidate_utility(lhs) != candidate_utility(rhs)) {
        return candidate_utility(lhs) > candidate_utility(rhs);
    }
    const double lhs_fit = candidate_semantic_fit(query, lhs);
    const double rhs_fit = candidate_semantic_fit(query, rhs);
    if (lhs_fit != rhs_fit) {
        return lhs_fit > rhs_fit;
    }
    return lhs.command < rhs.command;
}

bool relevance_source_token(const std::string_view token) {
    // pkgfile maps package -> executable. It enriches capabilities/provenance but
    // does not independently say the executable matches the user's intent.
    return !token.empty() && token != "pkgfile";
}

bool adds_independent_relevance_source(
    const std::string_view existing,
    const std::string_view incoming
) {
    std::size_t start = 0;
    while (start <= incoming.size()) {
        const auto end = incoming.find('+', start);
        const auto token = incoming.substr(
            start,
            end == std::string_view::npos ? incoming.size() - start : end - start
        );
        if (relevance_source_token(token) && !source_contains(existing, token)) {
            return true;
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return false;
}

void merge_evidence_trace(
    std::vector<ranking::SourceEvidenceTrace>& target,
    std::vector<ranking::SourceEvidenceTrace> incoming
) {
    for (auto& trace : incoming) {
        auto existing = std::ranges::find_if(target, [&](const ranking::SourceEvidenceTrace& item) {
            return item.source == trace.source && item.method == trace.method;
        });
        if (existing == target.end()) {
            target.push_back(std::move(trace));
        } else if (trace.final_score > existing->final_score) {
            *existing = std::move(trace);
        }
    }
}

void merge_candidate(Candidate& target, Candidate incoming) {
    const auto preserve_description = [](std::vector<std::string>& descriptions, const std::string_view text) {
        if (text.empty() || text == "Executable available in PATH") {
            return;
        }
        if (std::ranges::find(descriptions, text) == descriptions.end()) {
            descriptions.emplace_back(text);
        }
    };

    // Preserve source-specific descriptive metadata before the preferred display
    // summary is selected. Package metadata can carry role information (for
    // example backup/deployment) that a shorter man synopsis legitimately omits.
    preserve_description(target.descriptive_evidence, target.summary);
    preserve_description(target.descriptive_evidence, incoming.summary);
    for (const auto& description : incoming.descriptive_evidence) {
        preserve_description(target.descriptive_evidence, description);
    }

    const double old_utility = candidate_utility(target);
    const double incoming_utility = candidate_utility(incoming);
    const std::string old_sources = target.source;
    const bool independent_support = adds_independent_relevance_source(old_sources, incoming.source);

    if (target.path.empty() && !incoming.path.empty()) {
        target.path = std::move(incoming.path);
    }

    if (is_placeholder_summary(target.summary) && !is_placeholder_summary(incoming.summary)) {
        target.summary = std::move(incoming.summary);
    }

    merge_sources(target.source, incoming.source);

    if (target.package.empty() && !incoming.package.empty()) {
        target.package = std::move(incoming.package);
    }
    if (target.repository.empty() && !incoming.repository.empty()) {
        target.repository = std::move(incoming.repository);
    }
    if (target.package_version.empty() && !incoming.package_version.empty()) {
        target.package_version = std::move(incoming.package_version);
    }

    target.installed = target.installed || incoming.installed;
    target.repository_available = target.repository_available || incoming.repository_available;
    target.cli_capable = target.cli_capable || incoming.cli_capable;
    target.gui_capable = target.gui_capable || incoming.gui_capable;
    target.semantic_fit = std::max(target.semantic_fit, incoming.semantic_fit);
    merge_terms(target.matched_terms, incoming.matched_terms);
    merge_terms(target.provided_commands, incoming.provided_commands);

    const bool trace_base_merge = !target.ranking.has_value() && !incoming.ranking.has_value() &&
        (!target.evidence_trace.empty() || !incoming.evidence_trace.empty() ||
         !target.base_merge_trace.empty() || !incoming.base_merge_trace.empty());
    if (trace_base_merge) {
        merge_evidence_trace(target.evidence_trace, std::move(incoming.evidence_trace));
        target.base_merge_trace.insert(
            target.base_merge_trace.end(),
            std::make_move_iterator(incoming.base_merge_trace.begin()),
            std::make_move_iterator(incoming.base_merge_trace.end())
        );
    }

    const double strongest = std::max(old_utility, incoming_utility);
    const double supporting = std::min(old_utility, incoming_utility);
    // Only genuinely independent relevance provenance earns corroboration. Two
    // repo variants of the same expac package are alternatives, not two votes.
    const double raw_after = independent_support
        ? strongest + (0.08 * supporting)
        : strongest;
    set_candidate_utility(target, raw_after);

    if (trace_base_merge && independent_support) {
        target.base_merge_trace.push_back(ranking::BaseMergeTrace{
            .incoming_source = incoming.source,
            .before = old_utility,
            .incoming_score = incoming_utility,
            .strongest = strongest,
            .supporting = supporting,
            .raw_after = raw_after,
            .after = target.score,
        });
    }

    // Merging can add package fan-out or descriptive evidence that changes
    // query-relative specialization constraints, so any previous ordering key
    // is no longer valid.
    invalidate_candidate_ordering(target);
}

bool exact_target_match(const Candidate& candidate, const std::string& target) {
    return candidate.command == target || (!candidate.package.empty() && candidate.package == target);
}

void finalize_candidate_score(
    const Query& query,
    Candidate& candidate,
    const bool explain_ranking
) {
    const double base_utility = candidate_utility(candidate);
    auto breakdown = ranking::explain_preference_prior(query, candidate, base_utility);
    candidate.ordering_semantic_fit = breakdown.semantic_fit;
    candidate.ordering_semantic_tier = ranking::semantic_fit_tier(breakdown.semantic_fit);
    candidate.ranking_utility = breakdown.final_utility;
    candidate.score = breakdown.final_score;
    if (explain_ranking) {
        candidate.ranking = std::move(breakdown);
    }
}

void apply_score_floor(
    Candidate& candidate,
    const double floor,
    const std::string_view id,
    const std::string_view label
) {
    const double utility = candidate_utility(candidate);
    if (utility >= floor) {
        return;
    }
    set_candidate_utility(candidate, floor);
    if (candidate.ranking) {
        candidate.ranking->adjustments.push_back(ranking::RankingAdjustment{
            .id = std::string(id),
            .label = std::string(label),
            .kind = ranking::AdjustmentKind::Floor,
            .value = floor,
            .before = utility,
            .after = floor,
        });
        candidate.ranking->final_utility = floor;
        candidate.ranking->final_score = candidate.score;
    }
}

} // namespace

void SearchEngine::add_source(std::unique_ptr<KnowledgeSource> source) {
    sources_.push_back(std::move(source));
}

void SearchEngine::add_location_source(std::unique_ptr<LocationSource> source) {
    location_sources_.push_back(std::move(source));
}

void SearchEngine::add_enricher(std::unique_ptr<CandidateEnricher> enricher) {
    enrichers_.push_back(std::move(enricher));
}

void SearchEngine::add_guidance_provider(std::unique_ptr<GuidanceProvider> provider) {
    guidance_providers_.push_back(std::move(provider));
}

SearchResult SearchEngine::search(
    const Query& input_query,
    const std::size_t limit,
    const bool explain_ranking,
    const bool profile
) const {
    using Clock = std::chrono::steady_clock;
    const auto search_started = Clock::now();

    Query query = input_query;
    query.explain_ranking = explain_ranking;
    query.frame = query::recognize_frame(query);
    query.targets = query::frame_targets(query);

    SearchResult result{
        .raw_query = query.raw,
        .normalized_query = query.normalized,
        .frame = query.frame,
        .targets = query.targets,
        .locations = {},
        .candidates = {},
        .confidence = {},
        .clarifications = {},
        .timing = SearchTiming{.enabled = profile, .total_ms = 0.0, .stages = {}},
    };

    const auto record_stage = [&](const std::string_view name, const Clock::time_point started, const std::size_t items = 0) {
        if (!profile) {
            return;
        }
        const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        result.timing.stages.push_back(TimingStage{
            .name = std::string(name),
            .milliseconds = elapsed,
            .items = items,
        });
    };
    const auto finish_timing = [&]() {
        if (!profile) {
            return;
        }
        result.timing.total_ms = std::chrono::duration<double, std::milli>(
            Clock::now() - search_started
        ).count();
    };

    if (query.frame.frame == QueryFrame::Locate) {
        for (const auto& source : location_sources_) {
            if (!source) {
                continue;
            }
            const auto stage_started = Clock::now();
            if (!source->available()) {
                record_stage("location-source", stage_started, 0);
                continue;
            }
            auto hits = source->search(query);
            const std::size_t hit_count = hits.size();
            record_stage("location-source", stage_started, hit_count);
            result.locations.insert(
                result.locations.end(),
                std::make_move_iterator(hits.begin()),
                std::make_move_iterator(hits.end())
            );
        }
        std::ranges::sort(result.locations, [](const LocationHit& left, const LocationHit& right) {
            if (left.score != right.score) {
                return left.score > right.score;
            }
            return left.path < right.path;
        });
        if (result.locations.size() > limit) {
            result.locations.resize(limit);
        }
    }

    const auto collect = [&](const Query& requested_query) {
        std::unordered_map<std::string, Candidate> merged;
        for (const auto& source : sources_) {
            if (!source) {
                continue;
            }
            const auto stage_started = Clock::now();
            if (!source->available()) {
                record_stage(std::string("source:") + std::string(source->diagnostic_name()), stage_started, 0);
                continue;
            }
            auto source_candidates = source->search(requested_query);
            const std::size_t source_count = source_candidates.size();
            record_stage(
                std::string("source:") + std::string(source->diagnostic_name()),
                stage_started,
                source_count
            );
            for (auto& candidate : source_candidates) {
                auto [it, inserted] = merged.try_emplace(candidate.command, candidate);
                if (!inserted) {
                    merge_candidate(it->second, std::move(candidate));
                }
            }
        }

        const auto prefilter_started = Clock::now();
        std::vector<Candidate> candidates;
        candidates.reserve(merged.size());
        for (auto& [_, candidate] : merged) {
            // Keep initial ranking purely semantic. Preference priors are applied
            // once, after enrichment, so they remain deterministic/idempotent.
            // Query-relative specialization analysis is cached once per candidate
            // before sorting; the comparator itself only reads numeric keys.
            prepare_candidate_ordering(requested_query, candidate);
            candidates.push_back(std::move(candidate));
        }
        // Enrichment is intentionally bounded. Repository search can return dozens
        // of candidates, but expensive package-file inspection is only useful for
        // candidates with a realistic chance of reaching the final result set.
        constexpr std::size_t kEnrichmentLimit = 24;
        const std::size_t enrich_count = std::min(kEnrichmentLimit, candidates.size());
        const auto rank_less = [&](const Candidate& lhs, const Candidate& rhs) {
            return candidate_rank_less(requested_query, lhs, rhs);
        };
        if (enrich_count < candidates.size()) {
            std::partial_sort(
                candidates.begin(),
                candidates.begin() + static_cast<std::ptrdiff_t>(enrich_count),
                candidates.end(),
                rank_less
            );
        } else {
            std::ranges::sort(candidates, rank_less);
        }
        record_stage("ranking-prefilter", prefilter_started, candidates.size());

        for (const auto& enricher : enrichers_) {
            if (!enricher) {
                continue;
            }
            const auto stage_started = Clock::now();
            if (!enricher->available()) {
                record_stage(std::string("enricher:") + std::string(enricher->diagnostic_name()), stage_started, 0);
                continue;
            }
            enricher->enrich_many(requested_query, candidates, enrich_count);
            record_stage(
                std::string("enricher:") + std::string(enricher->diagnostic_name()),
                stage_started,
                enrich_count
            );
        }

        // An enricher may discover that package name != executable name (for
        // example ripgrep -> rg), add package fan-out, or preserve new descriptive
        // evidence. All of those can affect query-relative ordering, so invalidate
        // the pre-enrichment cache before the final merge.
        for (auto& candidate : candidates) {
            invalidate_candidate_ordering(candidate);
        }

        // Re-merge after enrichment so repository metadata naturally joins any
        // local PATH/man candidate for that binary.
        const auto finalize_started = Clock::now();
        const auto remerge_started = Clock::now();
        std::unordered_map<std::string, Candidate> enriched_merged;
        for (auto& candidate : candidates) {
            auto [it, inserted] = enriched_merged.try_emplace(candidate.command, candidate);
            if (!inserted) {
                merge_candidate(it->second, std::move(candidate));
            }
        }
        record_stage("ranking-remerge", remerge_started, enriched_merged.size());

        const auto preferences_started = Clock::now();
        candidates.clear();
        candidates.reserve(enriched_merged.size());
        for (auto& [_, candidate] : enriched_merged) {
            finalize_candidate_score(requested_query, candidate, explain_ranking);
            candidates.push_back(std::move(candidate));
        }
        record_stage("ranking-preferences", preferences_started, candidates.size());

        const auto sort_started = Clock::now();
        std::ranges::sort(candidates, [&](const Candidate& lhs, const Candidate& rhs) {
            return candidate_rank_less(requested_query, lhs, rhs);
        });
        record_stage("ranking-sort", sort_started, candidates.size());
        record_stage("ranking-finalize", finalize_started, candidates.size());
        return candidates;
    };

    const auto attach_guidance = [&](const std::size_t candidate_count) {
        const std::size_t count = std::min(candidate_count, result.candidates.size());
        for (std::size_t candidate_index = 0; candidate_index < count; ++candidate_index) {
            auto& candidate = result.candidates[candidate_index];
            for (const auto& provider : guidance_providers_) {
                if (!provider) {
                    continue;
                }
                const auto stage_started = Clock::now();
                if (!provider->available()) {
                    record_stage(
                        std::string("guidance:") + std::string(provider->diagnostic_name()),
                        stage_started,
                        0
                    );
                    continue;
                }

                GuidanceBundle bundle = provider->guide(candidate);
                const std::size_t item_count = bundle.examples.size() + bundle.learning_resources.size();
                for (auto& example : bundle.examples) {
                    const auto duplicate = std::ranges::find_if(candidate.examples, [&](const UsageExample& existing) {
                        return existing.text == example.text &&
                               existing.source_kind == example.source_kind &&
                               existing.source_reference == example.source_reference;
                    });
                    if (duplicate == candidate.examples.end()) {
                        candidate.examples.push_back(std::move(example));
                    }
                }
                for (auto& resource : bundle.learning_resources) {
                    const auto duplicate = std::ranges::find_if(candidate.learning_resources, [&](const LearningResource& existing) {
                        return existing.target == resource.target &&
                               existing.source_kind == resource.source_kind;
                    });
                    if (duplicate == candidate.learning_resources.end()) {
                        candidate.learning_resources.push_back(std::move(resource));
                    }
                }
                record_stage(
                    std::string("guidance:") + std::string(provider->diagnostic_name()),
                    stage_started,
                    item_count
                );
            }
        }
    };

    const auto resolve_target = [&](const std::string& target) -> std::optional<Candidate> {
        Query target_query = Query::parse(target);
        target_query.explain_ranking = explain_ranking;
        target_query.frame = QueryFrameResult{
            .frame = QueryFrame::Explain,
            .confidence = 0.99,
            .explicit_frame = true,
            .signals = {"explicit target"},
        };
        target_query.targets = {target};

        auto candidates = collect(target_query);
        auto exact = std::ranges::find_if(candidates, [&](const Candidate& candidate) {
            return exact_target_match(candidate, target);
        });
        if (exact == candidates.end()) {
            return std::nullopt;
        }
        Candidate resolved = *exact;
        apply_score_floor(resolved, 0.97, "exact-target", "exact named target resolution");
        return resolved;
    };

    const auto attach_confidence = [&]() {
        const auto stage_started = Clock::now();
        const auto assessment = ranking::assess_confidence(query, result.candidates);
        result.confidence = assessment.confidence;
        result.clarifications = assessment.clarifications;
        record_stage("confidence", stage_started, result.candidates.size());
    };

    // Explain and Compare are entity-first frames. Resolve the named thing(s)
    // before searching the prose around them; otherwise English scaffolding such
    // as "difference" can become a literal command-search term.
    if (query.frame.frame == QueryFrame::Explain && !query.targets.empty()) {
        if (auto resolved = resolve_target(query.targets.front())) {
            result.candidates.push_back(std::move(*resolved));
            attach_guidance(1);
            attach_confidence();
            finish_timing();
            return result;
        }
    }

    if (query.frame.frame == QueryFrame::Compare && query.targets.size() >= 2) {
        for (const auto& target : query.targets) {
            if (auto resolved = resolve_target(target)) {
                result.candidates.push_back(std::move(*resolved));
            }
        }
        if (result.candidates.size() >= 2) {
            attach_guidance(result.candidates.size());
            attach_confidence();
            finish_timing();
            return result;
        }
        result.candidates.clear();
    }

    result.candidates = collect(query);

    // Diagnose/Locate often name the affected tool explicitly. Preserve generic
    // retrieval, but promote the resolved entity so `why is ssh ...` talks about
    // ssh rather than accidentally treating `key` as the entire request.
    if ((query.frame.frame == QueryFrame::Diagnose || query.frame.frame == QueryFrame::Locate) &&
        !query.targets.empty()) {
        if (auto resolved = resolve_target(query.targets.front())) {
            auto existing = std::ranges::find_if(result.candidates, [&](const Candidate& candidate) {
                return candidate.command == resolved->command;
            });
            {
                const double before = candidate_utility(*resolved);
                const double after = before + 0.02;
                set_candidate_utility(*resolved, after);
                if (resolved->ranking) {
                    resolved->ranking->adjustments.push_back(ranking::RankingAdjustment{
                        .id = "affected-target",
                        .label = "named affected tool promotion",
                        .kind = ranking::AdjustmentKind::Add,
                        .value = after - before,
                        .before = before,
                        .after = after,
                    });
                    resolved->ranking->final_utility = after;
                    resolved->ranking->final_score = resolved->score;
                }
            }
            apply_score_floor(*resolved, 0.94, "affected-target-floor", "named affected tool floor");
            if (existing == result.candidates.end()) {
                result.candidates.push_back(std::move(*resolved));
            } else {
                const double before_merge = candidate_utility(*existing);
                merge_candidate(*existing, std::move(*resolved));
                const double after_merge = candidate_utility(*existing);
                if (existing->ranking && after_merge != before_merge) {
                    existing->ranking->adjustments.push_back(ranking::RankingAdjustment{
                        .id = "affected-target-support",
                        .label = "named target supporting evidence merge",
                        .kind = ranking::AdjustmentKind::Add,
                        .value = after_merge - before_merge,
                        .before = before_merge,
                        .after = after_merge,
                    });
                    existing->ranking->final_utility = after_merge;
                    existing->ranking->final_score = existing->score;
                }
                apply_score_floor(*existing, 0.94, "affected-target-floor", "named affected tool floor");
            }
        }
    }

    std::ranges::sort(result.candidates, [&](const Candidate& lhs, const Candidate& rhs) {
        return candidate_rank_less(query, lhs, rhs);
    });

    if (result.candidates.size() > limit) {
        result.candidates.resize(limit);
    }

    // Guidance is post-ranking and bounded to the best match. It must never
    // affect recall, ranking, confidence, or the v0.1 regression contract.
    attach_guidance(1);
    attach_confidence();
    finish_timing();
    return result;
}

} // namespace acclorite
