#include "acclorite/output/json_renderer.hpp"

#include <iomanip>
#include <ostream>
#include <sstream>

namespace acclorite {

std::string JsonRenderer::escape(const std::string_view input) {
    std::ostringstream out;

    for (const unsigned char ch : input) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    out << "\\u"
                        << std::hex << std::uppercase << std::setw(4)
                        << std::setfill('0') << static_cast<int>(ch)
                        << std::dec << std::nouppercase << std::setfill(' ');
                } else {
                    out << static_cast<char>(ch);
                }
        }
    }

    return out.str();
}

void JsonRenderer::render(const SearchResult& result, std::ostream& out) const {
    out << "{\n";
    out << "  \"schema_version\": 15,\n";
    out << "  \"query\": \"" << escape(result.raw_query) << "\",\n";
    out << "  \"normalized_query\": \"" << escape(result.normalized_query) << "\",\n";
    out << "  \"timing\": ";
    if (!result.timing.enabled) {
        out << "null,\n";
    } else {
        out << "{\n";
        out << "    \"total_ms\": " << std::fixed << std::setprecision(3)
            << result.timing.total_ms << ",\n";
        out << "    \"stages\": [";
        if (!result.timing.stages.empty()) {
            out << '\n';
        }
        for (std::size_t i = 0; i < result.timing.stages.size(); ++i) {
            const auto& stage = result.timing.stages[i];
            out << "      {\"name\":\"" << escape(stage.name)
                << "\",\"milliseconds\":" << std::fixed << std::setprecision(3)
                << stage.milliseconds << ",\"items\":" << stage.items << "}";
            if (i + 1 < result.timing.stages.size()) {
                out << ',';
            }
            out << '\n';
        }
        out << "    ]\n";
        out << "  },\n";
    }
    out << "  \"query_frame\": {\n";
    out << "    \"type\": \"" << query_frame_name(result.frame.frame) << "\",\n";
    out << "    \"confidence\": " << std::fixed << std::setprecision(4)
        << result.frame.confidence << ",\n";
    out << "    \"explicit\": " << (result.frame.explicit_frame ? "true" : "false") << ",\n";
    out << "    \"signals\": [";
    for (std::size_t signal_index = 0; signal_index < result.frame.signals.size(); ++signal_index) {
        if (signal_index != 0) {
            out << ", ";
        }
        out << "\"" << escape(result.frame.signals[signal_index]) << "\"";
    }
    out << "]\n";
    out << "  },\n";

    out << "  \"confidence\": {\n";
    out << "    \"top_candidate\": " << std::fixed << std::setprecision(4)
        << result.confidence.top_candidate << ",\n";
    out << "    \"level\": \"" << confidence_level_name(result.confidence.top_candidate) << "\",\n";
    out << "    \"interpretation\": " << result.confidence.interpretation << ",\n";
    out << "    \"separation\": " << result.confidence.separation << ",\n";
    out << "    \"semantic_gap\": " << result.confidence.semantic_gap << ",\n";
    out << "    \"utility_gap\": " << result.confidence.utility_gap << ",\n";
    out << "    \"ambiguity\": \"" << ambiguity_state_name(result.confidence.ambiguity) << "\",\n";
    out << "    \"signals\": [";
    for (std::size_t i = 0; i < result.confidence.signals.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << "\"" << escape(result.confidence.signals[i]) << "\"";
    }
    out << "]\n";
    out << "  },\n";

    out << "  \"clarifications\": [";
    if (!result.clarifications.empty()) {
        out << '\n';
    }
    for (std::size_t i = 0; i < result.clarifications.size(); ++i) {
        const auto& option = result.clarifications[i];
        out << "    {\"id\":\"" << escape(option.id)
            << "\",\"label\":\"" << escape(option.label) << "\"}";
        if (i + 1 < result.clarifications.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";

    out << "  \"targets\": [";
    for (std::size_t i = 0; i < result.targets.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << "\"" << escape(result.targets[i]) << "\"";
    }
    out << "],\n";

    out << "  \"locations\": [";
    if (!result.locations.empty()) {
        out << '\n';
    }
    for (std::size_t i = 0; i < result.locations.size(); ++i) {
        const auto& hit = result.locations[i];
        out << "    {\n";
        out << "      \"path\": \"" << escape(hit.path) << "\",\n";
        out << "      \"kind\": \"" << escape(hit.kind) << "\",\n";
        out << "      \"source\": \"" << escape(hit.source) << "\",\n";
        out << "      \"score\": " << std::fixed << std::setprecision(4) << hit.score << '\n';
        out << "    }";
        if (i + 1 < result.locations.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";

    out << "  \"results\": [";
    if (!result.candidates.empty()) {
        out << '\n';
    }

    for (std::size_t i = 0; i < result.candidates.size(); ++i) {
        const auto& candidate = result.candidates[i];
        out << "    {\n";
        out << "      \"command\": \"" << escape(candidate.command) << "\",\n";
        out << "      \"path\": \"" << escape(candidate.path) << "\",\n";
        out << "      \"summary\": \"" << escape(candidate.summary) << "\",\n";
        out << "      \"source\": \"" << escape(candidate.source) << "\",\n";
        out << "      \"package\": \"" << escape(candidate.package) << "\",\n";
        out << "      \"repository\": \"" << escape(candidate.repository) << "\",\n";
        out << "      \"package_version\": \"" << escape(candidate.package_version) << "\",\n";
        out << "      \"installed\": " << (candidate.installed ? "true" : "false") << ",\n";
        out << "      \"repository_available\": "
            << (candidate.repository_available ? "true" : "false") << ",\n";
        out << "      \"interface\": \""
            << interface_kind_name(candidate.interface_kind()) << "\",\n";
        out << "      \"cli_capable\": " << (candidate.cli_capable ? "true" : "false") << ",\n";
        out << "      \"gui_capable\": " << (candidate.gui_capable ? "true" : "false") << ",\n";
        out << "      \"matched_terms\": [";
        for (std::size_t term_index = 0; term_index < candidate.matched_terms.size(); ++term_index) {
            if (term_index != 0) {
                out << ", ";
            }
            out << "\"" << escape(candidate.matched_terms[term_index]) << "\"";
        }
        out << "],\n";
        out << "      \"provided_commands\": [";
        for (std::size_t command_index = 0; command_index < candidate.provided_commands.size(); ++command_index) {
            if (command_index != 0) {
                out << ", ";
            }
            out << "\"" << escape(candidate.provided_commands[command_index]) << "\"";
        }
        out << "],\n";
        out << "      \"examples\": [";
        if (!candidate.examples.empty()) {
            out << '\n';
        }
        for (std::size_t example_index = 0; example_index < candidate.examples.size(); ++example_index) {
            const auto& example = candidate.examples[example_index];
            out << "        {\n";
            out << "          \"text\": \"" << escape(example.text) << "\",\n";
            out << "          \"source_type\": \""
                << guidance_source_kind_name(example.source_kind) << "\",\n";
            out << "          \"source_reference\": \""
                << escape(example.source_reference) << "\",\n";
            out << "          \"verified\": " << (example.verified ? "true" : "false") << ",\n";
            out << "          \"verified_by\": \"" << escape(example.verified_by) << "\",\n";
            out << "          \"verified_on\": \"" << escape(example.verified_on) << "\"\n";
            out << "        }";
            if (example_index + 1 < candidate.examples.size()) {
                out << ',';
            }
            out << '\n';
        }
        out << "      ],\n";
        out << "      \"learning_resources\": [";
        if (!candidate.learning_resources.empty()) {
            out << '\n';
        }
        for (std::size_t resource_index = 0; resource_index < candidate.learning_resources.size(); ++resource_index) {
            const auto& resource = candidate.learning_resources[resource_index];
            out << "        {\n";
            out << "          \"label\": \"" << escape(resource.label) << "\",\n";
            out << "          \"target\": \"" << escape(resource.target) << "\",\n";
            out << "          \"source_type\": \""
                << guidance_source_kind_name(resource.source_kind) << "\",\n";
            out << "          \"source_reference\": \""
                << escape(resource.source_reference) << "\",\n";
            out << "          \"verified\": " << (resource.verified ? "true" : "false") << ",\n";
            out << "          \"verified_by\": \"" << escape(resource.verified_by) << "\",\n";
            out << "          \"verified_on\": \"" << escape(resource.verified_on) << "\"\n";
            out << "        }";
            if (resource_index + 1 < candidate.learning_resources.size()) {
                out << ',';
            }
            out << '\n';
        }
        out << "      ],\n";
        out << "      \"score\": " << std::fixed << std::setprecision(4) << candidate.score << ",\n";
        out << "      \"ranking\": ";
        if (!candidate.ranking) {
            out << "null\n";
        } else {
            const auto& ranking = *candidate.ranking;
            out << "{\n";
            out << "        \"raw_semantic_fit\": " << std::fixed << std::setprecision(4)
                << ranking.raw_semantic_fit << ",\n";
            out << "        \"semantic_fit\": " << std::fixed << std::setprecision(4)
                << ranking.semantic_fit << ",\n";
            out << "        \"semantic_adjustments\": [";
            for (std::size_t adjustment_index = 0; adjustment_index < ranking.semantic_adjustments.size(); ++adjustment_index) {
                if (adjustment_index != 0) {
                    out << ", ";
                }
                const auto& adjustment = ranking.semantic_adjustments[adjustment_index];
                out << "{\"id\":\"" << escape(adjustment.id)
                    << "\",\"label\":\"" << escape(adjustment.label)
                    << "\",\"kind\":\"" << ranking::adjustment_kind_name(adjustment.kind)
                    << "\",\"value\":" << adjustment.value
                    << ",\"before\":" << adjustment.before
                    << ",\"after\":" << adjustment.after << "}";
            }
            out << "],\n";
            out << "        \"semantic_tier\": \"" << escape(ranking.semantic_tier) << "\",\n";
            out << "        \"base_score\": " << std::fixed << std::setprecision(4)
                << ranking.base_score << ",\n";
            out << "        \"base_utility\": " << std::fixed << std::setprecision(4)
                << ranking.base_utility << ",\n";
            out << "        \"base_evidence\": [";
            if (!ranking.source_evidence.empty()) {
                out << '\n';
            }
            for (std::size_t evidence_index = 0; evidence_index < ranking.source_evidence.size(); ++evidence_index) {
                const auto& evidence = ranking.source_evidence[evidence_index];
                out << "          {\n";
                out << "            \"source\": \"" << escape(evidence.source) << "\",\n";
                out << "            \"method\": \"" << escape(evidence.method) << "\",\n";
                out << "            \"semantic\": ";
                if (!evidence.semantic) {
                    out << "null,\n";
                } else {
                    const auto& semantic = *evidence.semantic;
                    out << "{\n";
                    out << "              \"exact_name_match\": "
                        << (semantic.exact_name_match ? "true" : "false") << ",\n";
                    out << "              \"coverage\": " << std::fixed << std::setprecision(4)
                        << semantic.coverage << ",\n";
                    out << "              \"average_quality\": " << semantic.average_quality << ",\n";
                    out << "              \"formula_score\": " << semantic.formula_score << ",\n";
                    out << "              \"full_match_ceiling\": " << semantic.full_match_ceiling << ",\n";
                    out << "              \"after_ceiling\": " << semantic.after_ceiling << ",\n";
                    out << "              \"frame_before\": " << semantic.frame_before << ",\n";
                    out << "              \"frame_after\": " << semantic.frame_after << ",\n";
                    out << "              \"frame_effect\": \"" << escape(semantic.frame_effect) << "\",\n";
                    out << "              \"concepts\": [";
                    if (!semantic.concepts.empty()) {
                        out << '\n';
                    }
                    for (std::size_t concept_index = 0; concept_index < semantic.concepts.size(); ++concept_index) {
                        const auto& concept_entry = semantic.concepts[concept_index];
                        out << "                {\n";
                        out << "                  \"term\": \"" << escape(concept_entry.term) << "\",\n";
                        out << "                  \"role\": \"" << query::concept_role_name(concept_entry.role) << "\",\n";
                        out << "                  \"weight\": " << std::fixed << std::setprecision(4) << concept_entry.weight << ",\n";
                        out << "                  \"implicit\": " << (concept_entry.implicit ? "true" : "false") << ",\n";
                        out << "                  \"quality\": " << concept_entry.quality << ",\n";
                        out << "                  \"match_kind\": \"" << query::semantic_match_kind_name(concept_entry.kind) << "\",\n";
                        out << "                  \"location\": \"" << query::semantic_match_location_name(concept_entry.location) << "\",\n";
                        out << "                  \"matched_via\": \"" << escape(concept_entry.matched_via) << "\",\n";
                        out << "                  \"matched_token\": \"" << escape(concept_entry.matched_token) << "\"\n";
                        out << "                }";
                        if (concept_index + 1 < semantic.concepts.size()) {
                            out << ',';
                        }
                        out << '\n';
                    }
                    out << "              ]\n";
                    out << "            },\n";
                }
                out << "            \"adjustments\": [";
                if (!evidence.adjustments.empty()) {
                    out << '\n';
                }
                for (std::size_t source_adjustment_index = 0;
                     source_adjustment_index < evidence.adjustments.size(); ++source_adjustment_index) {
                    const auto& adjustment = evidence.adjustments[source_adjustment_index];
                    out << "              {\n";
                    out << "                \"id\": \"" << escape(adjustment.id) << "\",\n";
                    out << "                \"label\": \"" << escape(adjustment.label) << "\",\n";
                    out << "                \"kind\": \"" << ranking::adjustment_kind_name(adjustment.kind) << "\",\n";
                    out << "                \"value\": " << std::fixed << std::setprecision(4) << adjustment.value << ",\n";
                    out << "                \"before\": " << adjustment.before << ",\n";
                    out << "                \"after\": " << adjustment.after << "\n";
                    out << "              }";
                    if (source_adjustment_index + 1 < evidence.adjustments.size()) {
                        out << ',';
                    }
                    out << '\n';
                }
                out << "            ],\n";
                out << "            \"final_score\": " << std::fixed << std::setprecision(4)
                    << evidence.final_score << "\n";
                out << "          }";
                if (evidence_index + 1 < ranking.source_evidence.size()) {
                    out << ',';
                }
                out << '\n';
            }
            out << "        ],\n";
            out << "        \"base_merges\": [";
            if (!ranking.base_merges.empty()) {
                out << '\n';
            }
            for (std::size_t merge_index = 0; merge_index < ranking.base_merges.size(); ++merge_index) {
                const auto& merge = ranking.base_merges[merge_index];
                out << "          {\n";
                out << "            \"incoming_source\": \"" << escape(merge.incoming_source) << "\",\n";
                out << "            \"before\": " << std::fixed << std::setprecision(4) << merge.before << ",\n";
                out << "            \"incoming_score\": " << merge.incoming_score << ",\n";
                out << "            \"strongest\": " << merge.strongest << ",\n";
                out << "            \"supporting\": " << merge.supporting << ",\n";
                out << "            \"raw_after\": " << merge.raw_after << ",\n";
                out << "            \"after\": " << merge.after << "\n";
                out << "          }";
                if (merge_index + 1 < ranking.base_merges.size()) {
                    out << ',';
                }
                out << '\n';
            }
            out << "        ],\n";
            out << "        \"adjustments\": [";
            if (!ranking.adjustments.empty()) {
                out << '\n';
            }
            for (std::size_t adjustment_index = 0;
                 adjustment_index < ranking.adjustments.size(); ++adjustment_index) {
                const auto& adjustment = ranking.adjustments[adjustment_index];
                out << "          {\n";
                out << "            \"id\": \"" << escape(adjustment.id) << "\",\n";
                out << "            \"label\": \"" << escape(adjustment.label) << "\",\n";
                out << "            \"kind\": \""
                    << ranking::adjustment_kind_name(adjustment.kind) << "\",\n";
                out << "            \"value\": " << std::fixed << std::setprecision(4)
                    << adjustment.value << ",\n";
                out << "            \"before\": " << std::fixed << std::setprecision(4)
                    << adjustment.before << ",\n";
                out << "            \"after\": " << std::fixed << std::setprecision(4)
                    << adjustment.after << '\n';
                out << "          }";
                if (adjustment_index + 1 < ranking.adjustments.size()) {
                    out << ',';
                }
                out << '\n';
            }
            out << "        ],\n";
            out << "        \"final_utility\": " << std::fixed << std::setprecision(4)
                << ranking.final_utility << ",\n";
            out << "        \"final_score\": " << std::fixed << std::setprecision(4)
                << ranking.final_score << '\n';
            out << "      }\n";
        }
        out << "    }";
        if (i + 1 < result.candidates.size()) {
            out << ',';
        }
        out << '\n';
    }

    out << "  ]\n";
    out << "}\n";
}

} // namespace acclorite
