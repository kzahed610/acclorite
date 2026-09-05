#include "acclorite/output/terminal_renderer.hpp"

#include <algorithm>
#include <iomanip>
#include <ostream>
#include <ranges>
#include <string_view>

namespace acclorite {
namespace {

void render_candidate_details(const Candidate& candidate, std::ostream& out, const bool include_score = true) {
    out << candidate.command << '\n';
    out << candidate.summary << "\n\n";
    out << "Installed\n";
    out << (candidate.installed ? "✓ yes" : "✗ no") << '\n';

    if (candidate.repository_available) {
        out << "\nAvailable\n✓ ";
        out << (candidate.repository.empty() ? "repository" : candidate.repository);
        out << '\n';
    }

    if (!candidate.package.empty()) {
        out << "\nPackage\n";
        if (!candidate.repository.empty()) {
            out << candidate.repository << '/';
        }
        out << candidate.package;
        if (!candidate.package_version.empty()) {
            out << "  " << candidate.package_version;
        }
        out << '\n';
    }

    out << "\nInterface\n" << interface_kind_name(candidate.interface_kind()) << '\n';

    if (!candidate.path.empty()) {
        out << "\nPath\n" << candidate.path << '\n';
    }

    if (!candidate.provided_commands.empty() &&
        (candidate.provided_commands.size() > 1 ||
         (!candidate.package.empty() && candidate.command != candidate.package))) {
        out << "\nProvided commands\n";
        const std::size_t display_limit = std::min<std::size_t>(8, candidate.provided_commands.size());
        for (std::size_t i = 0; i < display_limit; ++i) {
            if (i != 0) {
                out << " · ";
            }
            out << candidate.provided_commands[i];
        }
        if (candidate.provided_commands.size() > display_limit) {
            out << " · …";
        }
        out << '\n';
    }

    if (!candidate.matched_terms.empty()) {
        out << "\nMatched concepts\n";
        for (std::size_t i = 0; i < candidate.matched_terms.size(); ++i) {
            if (i != 0) {
                out << " · ";
            }
            out << candidate.matched_terms[i];
        }
        out << '\n';
    }

    if (!candidate.source.empty()) {
        out << "\nSources\n" << candidate.source << '\n';
    }

    if (include_score) {
        out << "\nScore\n" << std::fixed << std::setprecision(2) << candidate.score << '\n';
    }
}


void render_guidance(const Candidate& candidate, std::ostream& out) {
    const bool has_man_resource = std::ranges::any_of(
        candidate.learning_resources,
        [](const LearningResource& resource) {
            return resource.source_kind == GuidanceSourceKind::Man;
        }
    );

    if (!candidate.examples.empty()) {
        const auto& example = candidate.examples.front();
        out << "\nVerified example\n" << example.text << '\n';
        out << "Source: " << example.source_reference << '\n';
    } else if (has_man_resource) {
        out << "\nVerified example\n";
        out << "None found in current local documentation.\n";
    }

    if (!candidate.learning_resources.empty()) {
        out << "\nLearn\n";
        for (std::size_t i = 0; i < candidate.learning_resources.size(); ++i) {
            const auto& resource = candidate.learning_resources[i];
            if (i != 0) {
                out << " · ";
            }
            out << resource.target;
        }
        out << '\n';
    }
}

void render_alternatives(const SearchResult& result, std::ostream& out, const std::size_t start = 1) {
    if (result.candidates.size() <= start) {
        return;
    }

    out << "\nAlternatives\n";
    for (std::size_t i = start; i < result.candidates.size(); ++i) {
        const auto& candidate = result.candidates[i];
        out << "  " << (i - start + 1) << ". " << candidate.command
            << "  [" << interface_kind_name(candidate.interface_kind());
        if (!candidate.installed && candidate.repository_available) {
            out << " · available";
        }
        out << "]"
            << "  (" << std::fixed << std::setprecision(2)
            << candidate.score << ")\n";
    }
}


void render_confidence_summary(const SearchResult& result, std::ostream& out) {
    const auto& confidence = result.confidence;
    out << "\nConfidence\n";
    out << confidence_level_name(confidence.top_candidate)
        << " · " << ambiguity_state_name(confidence.ambiguity)
        << " · " << std::fixed << std::setprecision(2) << confidence.top_candidate << '\n';

    if (confidence.ambiguity != AmbiguityState::Clear && !confidence.signals.empty()) {
        out << "Why\n" << confidence.signals.front() << '\n';
    }

    if (!result.clarifications.empty()) {
        out << "\nClarify\n";
        for (std::size_t i = 0; i < result.clarifications.size(); ++i) {
            if (i != 0) {
                out << " · ";
            }
            out << result.clarifications[i].label;
        }
        out << '\n';
    }
}

void render_confidence_diagnostics(const SearchResult& result, std::ostream& out) {
    const auto& confidence = result.confidence;
    out << "Confidence diagnostics\n";
    out << "  State                     " << ambiguity_state_name(confidence.ambiguity) << '\n';
    out << "  Top candidate confidence  " << std::fixed << std::setprecision(4)
        << confidence.top_candidate << '\n';
    out << "  Interpretation confidence " << std::fixed << std::setprecision(4)
        << confidence.interpretation << '\n';
    out << "  Candidate separation      " << std::fixed << std::setprecision(4)
        << confidence.separation << '\n';
    out << "  Semantic #1/#2 gap        " << std::fixed << std::setprecision(4)
        << confidence.semantic_gap << '\n';
    out << "  Utility #1/#2 gap         " << std::fixed << std::setprecision(4)
        << confidence.utility_gap << '\n';
    out << "  Query-frame confidence    " << std::fixed << std::setprecision(4)
        << result.frame.confidence
        << (result.frame.explicit_frame ? " · explicit" : " · inferred") << '\n';
    for (const auto& signal : confidence.signals) {
        out << "  Signal                     " << signal << '\n';
    }
    for (const auto& option : result.clarifications) {
        out << "  Clarification              " << option.label << '\n';
    }
    out << '\n';
}

void render_semantic_breakdown(const query::SemanticBreakdown& semantic, std::ostream& out) {
    if (semantic.exact_name_match) {
        out << "      Exact command/package name match\n";
    } else {
        for (const auto& concept_entry : semantic.concepts) {
            out << "      " << (concept_entry.quality > 0.0 ? "✓ " : "✗ ") << concept_entry.term
                << " [" << query::concept_role_name(concept_entry.role)
                << (concept_entry.implicit ? " · implicit" : "")
                << ", w=" << std::fixed << std::setprecision(2) << concept_entry.weight << "]";
            if (concept_entry.quality > 0.0) {
                out << " q=" << std::fixed << std::setprecision(2) << concept_entry.quality
                    << " · " << query::semantic_match_kind_name(concept_entry.kind);
                if (!concept_entry.matched_via.empty()) {
                    out << " via '" << concept_entry.matched_via << "'";
                }
                out << " → " << query::semantic_match_location_name(concept_entry.location);
                if (!concept_entry.matched_token.empty()) {
                    out << " '" << concept_entry.matched_token << "'";
                }
            }
            out << '\n';
        }
    }

    out << "      Coverage " << std::fixed << std::setprecision(4) << semantic.coverage
        << " · avg quality " << semantic.average_quality << '\n';
    out << "      Semantic formula " << semantic.formula_score
        << " · ceiling " << semantic.full_match_ceiling
        << " → " << semantic.after_ceiling << '\n';
    if (semantic.frame_after != semantic.frame_before || !semantic.frame_effect.empty()) {
        out << "      Frame bias " << semantic.frame_before << " → " << semantic.frame_after;
        if (!semantic.frame_effect.empty()) {
            out << " · " << semantic.frame_effect;
        }
        out << '\n';
    }
}

void render_base_evidence(const ranking::RankingBreakdown& breakdown, std::ostream& out) {
    if (breakdown.source_evidence.empty() && breakdown.base_merges.empty()) {
        return;
    }

    out << "  Base evidence\n";
    for (const auto& evidence : breakdown.source_evidence) {
        out << "    " << evidence.source;
        if (!evidence.method.empty()) {
            out << " · " << evidence.method;
        }
        out << '\n';

        if (evidence.semantic) {
            render_semantic_breakdown(*evidence.semantic, out);
        }
        for (const auto& adjustment : evidence.adjustments) {
            out << "      ";
            switch (adjustment.kind) {
                case ranking::AdjustmentKind::Add:
                    out << (adjustment.value >= 0.0 ? "+" : "")
                        << std::fixed << std::setprecision(4) << adjustment.value;
                    break;
                case ranking::AdjustmentKind::Multiply:
                    out << "×" << std::fixed << std::setprecision(3) << adjustment.value;
                    break;
                case ranking::AdjustmentKind::Floor:
                    out << "floor " << std::fixed << std::setprecision(4) << adjustment.value;
                    break;
                case ranking::AdjustmentKind::Clamp:
                    out << "clamp " << std::fixed << std::setprecision(4) << adjustment.value;
                    break;
            }
            out << "  " << adjustment.label
                << "  (" << std::fixed << std::setprecision(4) << adjustment.before
                << " → " << adjustment.after << ")\n";
        }
        out << "      Source score " << std::fixed << std::setprecision(4)
            << evidence.final_score << '\n';
    }

    if (!breakdown.base_merges.empty()) {
        out << "  Base merges\n";
        for (const auto& merge : breakdown.base_merges) {
            out << "    + " << merge.incoming_source
                << " · strongest " << std::fixed << std::setprecision(4) << merge.strongest
                << " + 0.08×support " << merge.supporting
                << " = " << merge.raw_after;
            if (merge.raw_after != merge.after) {
                out << " → display clamp " << merge.after;
            }
            out << '\n';
        }
    }
}

void render_ranking_breakdown(const Candidate& candidate, std::ostream& out) {
    if (!candidate.ranking) {
        return;
    }

    const auto& breakdown = *candidate.ranking;
    out << candidate.command << '\n';
    render_base_evidence(breakdown, out);
    if (!breakdown.semantic_adjustments.empty()) {
        out << "  Raw semantic fit          " << std::fixed << std::setprecision(4)
            << breakdown.raw_semantic_fit << '\n';
        for (const auto& adjustment : breakdown.semantic_adjustments) {
            out << "  ×" << std::fixed << std::setprecision(3) << adjustment.value
                << "  " << adjustment.label
                << "  (" << std::fixed << std::setprecision(4) << adjustment.before
                << " → " << adjustment.after << ")\n";
        }
    }
    out << "  Semantic fit              " << std::fixed << std::setprecision(4)
        << breakdown.semantic_fit << " · " << breakdown.semantic_tier << '\n';
    out << "  Base display score        " << std::fixed << std::setprecision(4)
        << breakdown.base_score << '\n';
    out << "  Base ranking utility      " << std::fixed << std::setprecision(4)
        << breakdown.base_utility << '\n';

    for (const auto& adjustment : breakdown.adjustments) {
        out << "  ";
        switch (adjustment.kind) {
            case ranking::AdjustmentKind::Add:
                out << (adjustment.value >= 0.0 ? "+" : "")
                    << std::fixed << std::setprecision(4) << adjustment.value;
                break;
            case ranking::AdjustmentKind::Multiply:
                out << "×" << std::fixed << std::setprecision(3) << adjustment.value;
                break;
            case ranking::AdjustmentKind::Floor:
                out << "floor " << std::fixed << std::setprecision(4) << adjustment.value;
                break;
            case ranking::AdjustmentKind::Clamp:
                out << "clamp " << std::fixed << std::setprecision(4) << adjustment.value;
                break;
        }
        out << "  " << adjustment.label
            << "  (" << std::fixed << std::setprecision(4) << adjustment.before
            << " → " << adjustment.after << ")\n";
    }

    out << "  Final ranking utility    " << std::fixed << std::setprecision(4)
        << breakdown.final_utility << "\n";
    out << "  Display score            " << std::fixed << std::setprecision(4)
        << breakdown.final_score << "\n";
}

void render_timing_summary(const SearchResult& result, std::ostream& out) {
    if (!result.timing.enabled) {
        return;
    }
    out << "\nPerformance profile\n";
    out << "────────────────────────────\n";
    out << "Total internal search  " << std::fixed << std::setprecision(2)
        << result.timing.total_ms << " ms\n";
    for (const auto& stage : result.timing.stages) {
        out << "  " << stage.name << "  " << std::fixed << std::setprecision(2)
            << stage.milliseconds << " ms";
        if (stage.items != 0) {
            out << " · " << stage.items << " item" << (stage.items == 1 ? "" : "s");
        }
        out << '\n';
    }
}

void render_ranking_diagnostics(const SearchResult& result, std::ostream& out) {
    const bool has_trace = std::ranges::any_of(result.candidates, [](const Candidate& candidate) {
        return candidate.ranking.has_value();
    });
    if (!has_trace) {
        return;
    }

    out << "\nRanking diagnostics\n";
    out << "────────────────────────────\n";
    out << "Base evidence decomposes semantic coverage and independent-source support. "
           "Ranking utility preserves ordering beyond the bounded public score.\n\n";
    render_confidence_diagnostics(result, out);

    const std::size_t display_limit = std::min<std::size_t>(5, result.candidates.size());
    for (std::size_t i = 0; i < display_limit; ++i) {
        if (i != 0) {
            out << '\n';
        }
        render_ranking_breakdown(result.candidates[i], out);
    }
}

} // namespace

void TerminalRenderer::render(const SearchResult& result, std::ostream& out) const {
    if (result.frame.frame != QueryFrame::Discover || result.frame.explicit_frame) {
        out << "Request type\n";
        out << query_frame_name(result.frame.frame);
        if (!result.frame.explicit_frame) {
            out << " · inferred";
        }
        out << "\n\n";
    }

    if (!result.targets.empty()) {
        out << "Target" << (result.targets.size() == 1 ? "" : "s") << "\n";
        for (std::size_t i = 0; i < result.targets.size(); ++i) {
            if (i != 0) {
                out << " · ";
            }
            out << result.targets[i];
        }
        out << "\n\n";
    }

    if (result.frame.frame == QueryFrame::Locate) {
        if (!result.locations.empty()) {
            out << "Locations\n";
            out << "────────────────────────────\n\n";
            for (std::size_t i = 0; i < result.locations.size(); ++i) {
                const auto& hit = result.locations[i];
                out << (i + 1) << ". " << hit.path;
                if (!hit.kind.empty()) {
                    out << "  [" << hit.kind << "]";
                }
                out << '\n';
            }
        } else {
            out << "Locations\n";
            out << "────────────────────────────\n\n";
            out << "No matching local resource path found in the bounded config locations yet.\n";
        }

        if (!result.candidates.empty()) {
            out << "\nRelated tool\n";
            out << "────────────────────────────\n\n";
            render_candidate_details(result.candidates.front(), out);
            render_guidance(result.candidates.front(), out);
            render_confidence_summary(result, out);
        }
        render_ranking_diagnostics(result, out);
        render_timing_summary(result, out);
        return;
    }

    if (result.frame.frame == QueryFrame::Compare && result.candidates.size() >= 2) {
        out << "Comparison targets\n";
        out << "────────────────────────────\n\n";
        for (std::size_t i = 0; i < result.candidates.size(); ++i) {
            if (i != 0) {
                out << "\n────────────────────────────\n\n";
            }
            render_candidate_details(result.candidates[i], out, false);
            render_guidance(result.candidates[i], out);
        }
        render_confidence_summary(result, out);
        out << "\nDetailed capability-by-capability comparison is not implemented yet; "
               "these are the resolved tools and their local metadata.\n";
        render_ranking_diagnostics(result, out);
        render_timing_summary(result, out);
        return;
    }

    if (result.candidates.empty()) {
        out << "No matches yet for: " << result.raw_query << '\n';
        out << "(No tool matched strongly enough in the available local/package sources.)\n";
        render_confidence_summary(result, out);
        render_timing_summary(result, out);
        return;
    }

    const auto& best = result.candidates.front();
    std::string_view heading = "Best Match";
    if (result.frame.frame == QueryFrame::Explain) {
        heading = "About";
    } else if (result.frame.frame == QueryFrame::Diagnose) {
        heading = "Related diagnostic tool";
    }

    out << heading << '\n';
    out << "────────────────────────────\n\n";
    render_candidate_details(best, out);
    render_guidance(best, out);
    render_confidence_summary(result, out);

    // Explanation of an explicitly named entity should not immediately bury the
    // answer under substring-related package noise.
    if (result.frame.frame != QueryFrame::Explain) {
        render_alternatives(result, out);
    }
    render_ranking_diagnostics(result, out);
    render_timing_summary(result, out);
}

} // namespace acclorite
