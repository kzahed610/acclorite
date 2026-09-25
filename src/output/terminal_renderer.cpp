#include "acclorite/output/terminal_renderer.hpp"

#include "acclorite/output/shell_renderer.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <ostream>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>

namespace acclorite {
namespace {

constexpr std::string_view kRule = "────────────────────────────────────────";
constexpr std::string_view kReset = "\x1b[0m";
constexpr std::string_view kBold = "\x1b[1m";
constexpr std::string_view kDim = "\x1b[2m";
constexpr std::string_view kCyan = "\x1b[36m";
constexpr std::string_view kGreen = "\x1b[32m";
constexpr std::string_view kYellow = "\x1b[33m";
constexpr std::string_view kRed = "\x1b[31m";

void styled(
    std::ostream& out,
    const bool color,
    const std::string_view ansi,
    const std::string_view text
) {
    if (color) {
        out << ansi << text << kReset;
    } else {
        out << text;
    }
}

void render_section_heading(std::ostream& out, const std::string_view title, const bool color) {
    styled(out, color, kBold, title);
    out << '\n';
    styled(out, color, kDim, kRule);
    out << "\n";
}

std::string package_display(const Candidate& candidate) {
    std::ostringstream value;
    if (!candidate.repository.empty()) {
        value << candidate.repository << '/';
    }
    value << candidate.package;
    if (!candidate.package_version.empty()) {
        value << " " << candidate.package_version;
    }
    return value.str();
}

void render_candidate_details(const Candidate& candidate, std::ostream& out, const bool color) {
    styled(out, color, kBold, candidate.command);
    if (!candidate.summary.empty()) {
        out << "  —  " << candidate.summary;
    }
    out << '\n';

    out << "  ";
    if (candidate.installed) {
        styled(out, color, kGreen, "✓ installed");
        if (!candidate.path.empty()) {
            out << " · " << candidate.path;
        }
        if (candidate.interface_kind() != InterfaceKind::Unknown) {
            out << " · " << interface_kind_name(candidate.interface_kind());
        }
    } else {
        styled(out, color, kYellow, "○ not installed");
        if (candidate.interface_kind() != InterfaceKind::Unknown) {
            out << " · " << interface_kind_name(candidate.interface_kind());
        }
        if (candidate.repository_available) {
            out << " · available";
            if (!candidate.package.empty()) {
                out << ": " << package_display(candidate);
            } else if (!candidate.repository.empty()) {
                out << " in " << candidate.repository;
            }
        }
    }
    out << '\n';
}

std::string option_value_display(const CommandOption& option) {
    if (!option.value_shape_known || !option.value_name || option.value_name->empty()) {
        return {};
    }
    const auto& value = *option.value_name;
    if ((value.front() == '<' && value.back() == '>') ||
        (value.front() == '[' && value.back() == ']')) {
        return value;
    }
    return option.value_required ? "<" + value + ">" : "[" + value + "]";
}

void render_actionable_answer(const SearchResult& result, std::ostream& out, const bool color) {
    if (!result.actionable_answer) {
        return;
    }

    const auto& answer = *result.actionable_answer;
    out << '\n';
    const std::string_view heading = !answer.invocation
        ? std::string_view("Verified syntax")
        : (answer.invocation->complete
            ? std::string_view("Verified command")
            : std::string_view("Verified template"));
    styled(out, color, kBold, heading);
    out << '\n';

    if (answer.invocation) {
        out << "  ";
        styled(out, color, kCyan, output::render_shell_invocation(*answer.invocation));
        out << '\n';
    } else if (!answer.relevant_options.empty() && !answer.relevant_options.front().names.empty()) {
        const auto& option = answer.relevant_options.front();
        std::string syntax = answer.command + " " + option.names.front();
        if (const std::string value = option_value_display(option); !value.empty()) {
            syntax += " " + value;
        }
        out << "  ";
        styled(out, color, kCyan, syntax);
        out << '\n';
    } else if (!answer.relevant_subcommands.empty() && !answer.relevant_subcommands.front().name.empty()) {
        const auto& subcommand = answer.relevant_subcommands.front();
        out << "  ";
        styled(out, color, kCyan, answer.command + " " + subcommand.name);
        out << '\n';
    }

    if (!answer.explanation.empty()) {
        out << "  " << answer.explanation << '\n';
    }

    const SyntaxProvenance* provenance = nullptr;
    if (!answer.relevant_options.empty()) {
        provenance = &answer.relevant_options.front().provenance;
    } else if (!answer.relevant_subcommands.empty()) {
        provenance = &answer.relevant_subcommands.front().provenance;
    }
    if (provenance) {
        out << "  ";
        styled(out, color, kDim, "↳ ");
        styled(out, color, kDim, syntax_source_kind_name(provenance->source_kind));
        if (!provenance->source_reference.empty()) {
            out << " · ";
            styled(out, color, kDim, provenance->source_reference);
        }
        if (!provenance->section.empty()) {
            out << " · ";
            styled(out, color, kDim, provenance->section);
        }
        out << '\n';
    }

    out << "  ";
    styled(out, color, kDim, "Safety  ");
    styled(out, color, kDim, action_safety_name(answer.safety));
    out << '\n';
}

bool guidance_looks_like_template(const UsageExample& example) {
    return example.text.find("{{") != std::string::npos ||
           example.text.find("}}") != std::string::npos ||
           example.text.find("<pattern>") != std::string::npos ||
           example.text.find("<file>") != std::string::npos;
}

std::string compact_summary(std::string_view summary, const std::size_t max_chars = 76) {
    std::string out(summary);
    std::replace(out.begin(), out.end(), '\n', ' ');
    if (out.size() <= max_chars) {
        return out;
    }
    if (max_chars <= 1) {
        return "…";
    }
    out.resize(max_chars - 1);
    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back()))) {
        out.pop_back();
    }
    out += "…";
    return out;
}

std::string_view human_ambiguity_label(const AmbiguityState state) {
    switch (state) {
        case AmbiguityState::Clear: return "clear match";
        case AmbiguityState::Competitive: return "close alternatives";
        case AmbiguityState::Ambiguous: return "needs clarification";
        case AmbiguityState::LowConfidence: return "broad match";
        case AmbiguityState::NoResult: return "no result";
    }
    return "no result";
}

void render_guidance(const Candidate& candidate, std::ostream& out, const bool color) {
    if (!candidate.examples.empty()) {
        const auto& example = candidate.examples.front();
        styled(out, color, kDim, guidance_looks_like_template(example) ? "Template  " : "Example   ");
        styled(out, color, kCyan, example.text);
        out << " · ";
        styled(out, color, kDim, guidance_source_kind_name(example.source_kind));
        out << '\n';
    }

    if (!candidate.learning_resources.empty()) {
        bool wrote_local = false;
        for (const auto& resource : candidate.learning_resources) {
            if (resource.target.find("://") != std::string::npos) {
                continue;
            }
            if (!wrote_local) {
                styled(out, color, kDim, "Learn     ");
                wrote_local = true;
            } else {
                out << " · ";
            }
            out << resource.target;
        }
        if (wrote_local) {
            out << '\n';
        }

        for (const auto& resource : candidate.learning_resources) {
            if (resource.target.find("://") == std::string::npos) {
                continue;
            }
            styled(out, color, kDim, "Docs      ");
            if (!resource.label.empty()) {
                out << resource.label << " · ";
            }
            out << resource.target << '\n';
            break;
        }
    }
}

void render_alternatives(const SearchResult& result, std::ostream& out, const bool color, const std::size_t start = 1) {
    if (result.candidates.size() <= start) {
        return;
    }

    out << '\n';
    styled(out, color, kBold, "Other matches");
    out << '\n';
    const std::size_t limit = std::min<std::size_t>(result.candidates.size(), start + 3);
    for (std::size_t i = start; i < limit; ++i) {
        const auto& candidate = result.candidates[i];
        out << "  " << (i - start + 1) << ". ";
        styled(out, color, kBold, candidate.command);
        if (!candidate.summary.empty()) {
            out << "  —  " << compact_summary(candidate.summary);
        }
        if (!candidate.installed) {
            out << " · ";
            styled(out, color, kDim, candidate.repository_available ? "available" : "not installed");
        } else if (candidate.interface_kind() == InterfaceKind::Gui ||
                   candidate.interface_kind() == InterfaceKind::Hybrid) {
            out << " · ";
            styled(out, color, kDim, interface_kind_name(candidate.interface_kind()));
        }
        out << '\n';
    }
    if (result.candidates.size() > limit) {
        out << "  … " << (result.candidates.size() - limit) << " more\n";
    }
}

std::string_view confidence_color(const Confidence& confidence) {
    if (confidence.ambiguity == AmbiguityState::Ambiguous ||
        confidence.ambiguity == AmbiguityState::LowConfidence ||
        confidence.top_candidate < 0.55) {
        return kYellow;
    }
    return kGreen;
}

void render_confidence_summary(const SearchResult& result, std::ostream& out, const bool color) {
    const auto& confidence = result.confidence;

    if (confidence.ambiguity == AmbiguityState::Clear) {
        return;
    }

    if (confidence.ambiguity == AmbiguityState::NoResult) {
        styled(out, color, kYellow, "⚠ no confident match");
    } else {
        styled(out, color, confidence_color(confidence), "⚠ ");
        styled(out, color, confidence_color(confidence), human_ambiguity_label(confidence.ambiguity));
    }
    if (!confidence.signals.empty()) {
        out << " · ";
        styled(out, color, kDim, confidence.signals.front());
    }
    out << '\n';

    if (!result.clarifications.empty()) {
        out << '\n';
        styled(out, color, kBold, "Clarify");
        out << '\n';
        for (const auto& option : result.clarifications) {
            out << "  • " << option.label << '\n';
        }
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
    const bool show_frame = result.frame.frame != QueryFrame::Discover || result.frame.explicit_frame;
    if (show_frame) {
        std::string heading(query_frame_name(result.frame.frame));
        if (!result.targets.empty()) {
            heading += " · ";
            for (std::size_t i = 0; i < result.targets.size(); ++i) {
                if (i != 0) {
                    heading += " ↔ ";
                }
                heading += result.targets[i];
            }
        } else if (!result.frame.explicit_frame) {
            heading += " · inferred";
        }
        render_section_heading(out, heading, color_);
    }

    if (result.frame.frame == QueryFrame::Locate) {
        render_section_heading(out, "Locations", color_);
        if (!result.locations.empty()) {
            for (std::size_t i = 0; i < result.locations.size(); ++i) {
                const auto& hit = result.locations[i];
                out << "  " << (i + 1) << ". ";
                styled(out, color_, kBold, hit.path);
                if (!hit.kind.empty()) {
                    out << "  ·  ";
                    styled(out, color_, kDim, hit.kind);
                }
                out << '\n';
            }
        } else {
            styled(out, color_, kYellow, "  No matching local resource path found.");
            out << "\n  ";
            styled(out, color_, kDim, "Acclorite only checks bounded local configuration locations.");
            out << '\n';
        }

        if (!result.candidates.empty()) {
            out << '\n';
            render_section_heading(out, "Related tool", color_);
            render_candidate_details(result.candidates.front(), out, color_);
            render_actionable_answer(result, out, color_);
            render_guidance(result.candidates.front(), out, color_);
            render_confidence_summary(result, out, color_);
        }
        render_ranking_diagnostics(result, out);
        render_timing_summary(result, out);
        return;
    }

    if (result.frame.frame == QueryFrame::Compare && result.candidates.size() >= 2) {
        if (!show_frame) {
            render_section_heading(out, "Comparison", color_);
        }
        for (std::size_t i = 0; i < result.candidates.size(); ++i) {
            if (i != 0) {
                out << '\n';
            }
            out << "#" << (i + 1) << "  ";
            render_candidate_details(result.candidates[i], out, color_);
            render_guidance(result.candidates[i], out, color_);
        }
        render_actionable_answer(result, out, color_);
        render_confidence_summary(result, out, color_);
        out << '\n';
        styled(out, color_, kDim,
               "Detailed capability-by-capability comparison is not implemented yet; "
               "these are the resolved tools and their verified local metadata.");
        out << '\n';
        render_ranking_diagnostics(result, out);
        render_timing_summary(result, out);
        return;
    }

    if (result.candidates.empty()) {
        render_section_heading(out, "No strong match", color_);
        out << "  ";
        styled(out, color_, kDim, "Query  ");
        out << result.raw_query << "\n\n";
        out << "  Acclorite searched the available local and package knowledge sources,\n";
        out << "  but no tool matched strongly enough to recommend.\n\n";
        styled(out, color_, kBold, "Try");
        out << "\n";
        out << "  • describe the task with a concrete action and subject\n";
        out << "  • shorten the query if it contains lots of context\n";
        out << "  • run `acclorite doctor` if expected local sources seem missing\n";
        render_confidence_summary(result, out, color_);
        render_timing_summary(result, out);
        return;
    }

    if (!show_frame) {
        render_section_heading(out, "Best match", color_);
    }
    render_candidate_details(result.candidates.front(), out, color_);
    render_actionable_answer(result, out, color_);
    render_guidance(result.candidates.front(), out, color_);
    render_confidence_summary(result, out, color_);

    // Explanation of an explicitly named entity should not immediately bury the
    // answer under substring-related package noise.
    if (result.frame.frame != QueryFrame::Explain) {
        render_alternatives(result, out, color_);
    }
    render_ranking_diagnostics(result, out);
    render_timing_summary(result, out);
}

} // namespace acclorite
