#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>

#include "acclorite/core/guidance.hpp"
#include "acclorite/ranking/trace.hpp"

namespace acclorite {

enum class InterfaceKind {
    Unknown,
    Cli,
    Gui,
    Hybrid,
};

struct Candidate {
    std::string command;
    std::string path;
    std::string summary;
    std::string source;
    std::string package;
    std::string repository;
    std::string package_version;

    bool installed{false};
    bool repository_available{false};
    bool cli_capable{false};
    bool gui_capable{false};

    std::vector<std::string> matched_terms;
    std::vector<std::string> provided_commands;
    // Descriptive text retained from merged evidence sources. `summary` remains
    // the preferred display synopsis, while this vector preserves alternate
    // source descriptions needed for query-relative role/specialization checks.
    std::vector<std::string> descriptive_evidence;
    // Post-ranking, source-backed usage guidance. These fields are observational
    // and never participate in candidate scoring or ordering.
    std::vector<UsageExample> examples;
    std::vector<LearningResource> learning_resources;
    std::vector<ranking::SourceEvidenceTrace> evidence_trace;
    std::vector<ranking::BaseMergeTrace> base_merge_trace;
    // Strongest command-specific semantic intent fit before source confidence,
    // installation friction, common-tool priors, or other recommendation preferences.
    double semantic_fit{0.0};
    // Query-relative ordering cache. Computing specialization constraints can be
    // comparatively expensive because it inspects merged descriptive evidence.
    // SearchEngine populates these once per ranking phase so std::sort compares
    // numbers instead of re-running semantic analysis O(N log N) times.
    double ordering_semantic_fit{-1.0};
    int ordering_semantic_tier{-1};
    double ranking_utility{0.0};
    double score{0.0};
    std::optional<ranking::RankingBreakdown> ranking;

    [[nodiscard]] InterfaceKind interface_kind() const {
        if (cli_capable && gui_capable) {
            return InterfaceKind::Hybrid;
        }
        if (cli_capable) {
            return InterfaceKind::Cli;
        }
        if (gui_capable) {
            return InterfaceKind::Gui;
        }
        return InterfaceKind::Unknown;
    }
};


[[nodiscard]] inline bool source_contains(
    const std::string_view sources,
    const std::string_view source
) {
    if (source.empty()) {
        return false;
    }

    std::size_t start = 0;
    while (start <= sources.size()) {
        const auto end = sources.find('+', start);
        const auto token = sources.substr(
            start,
            end == std::string_view::npos ? sources.size() - start : end - start
        );
        if (token == source) {
            return true;
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return false;
}

inline void append_source(std::string& target, const std::string_view source) {
    if (source.empty() || source_contains(target, source)) {
        return;
    }
    if (!target.empty()) {
        target.push_back('+');
    }
    target.append(source);
}

inline void merge_sources(std::string& target, const std::string_view incoming) {
    std::size_t start = 0;
    while (start <= incoming.size()) {
        const auto end = incoming.find('+', start);
        const auto token = incoming.substr(
            start,
            end == std::string_view::npos ? incoming.size() - start : end - start
        );
        append_source(target, token);
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
}

[[nodiscard]] inline std::string_view interface_kind_name(const InterfaceKind kind) {
    switch (kind) {
        case InterfaceKind::Cli: return "CLI";
        case InterfaceKind::Gui: return "GUI";
        case InterfaceKind::Hybrid: return "Hybrid";
        case InterfaceKind::Unknown: return "Unknown";
    }
    return "Unknown";
}

} // namespace acclorite
