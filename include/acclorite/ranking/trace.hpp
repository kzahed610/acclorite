#pragma once

#include <optional>
#include <string>
#include <vector>

#include "acclorite/query/relevance.hpp"

namespace acclorite::ranking {

enum class AdjustmentKind {
    Add,
    Multiply,
    Floor,
    Clamp,
};

struct RankingAdjustment {
    std::string id;
    std::string label;
    AdjustmentKind kind{AdjustmentKind::Add};
    double value{0.0};
    double before{0.0};
    double after{0.0};
};

struct SourceEvidenceTrace {
    std::string source;
    std::string method;
    std::optional<query::SemanticBreakdown> semantic;
    std::vector<RankingAdjustment> adjustments;
    double final_utility{0.0};
    double final_score{0.0};
};

struct BaseMergeTrace {
    std::string incoming_source;
    double before{0.0};
    double incoming_score{0.0};
    double strongest{0.0};
    double supporting{0.0};
    double raw_after{0.0};
    double after{0.0};
};

struct RankingBreakdown {
    double raw_semantic_fit{0.0};
    double semantic_fit{0.0};
    std::string semantic_tier;
    std::vector<RankingAdjustment> semantic_adjustments;
    double base_score{0.0};
    double base_utility{0.0};
    std::vector<SourceEvidenceTrace> source_evidence;
    std::vector<BaseMergeTrace> base_merges;
    std::vector<RankingAdjustment> adjustments;
    double final_utility{0.0};
    double final_score{0.0};
};

[[nodiscard]] inline const char* adjustment_kind_name(const AdjustmentKind kind) {
    switch (kind) {
        case AdjustmentKind::Add: return "add";
        case AdjustmentKind::Multiply: return "multiply";
        case AdjustmentKind::Floor: return "floor";
        case AdjustmentKind::Clamp: return "clamp";
    }
    return "add";
}

} // namespace acclorite::ranking
