#pragma once

#include <string>

namespace acclorite {

// Query-relative identity hint from an auxiliary recall mechanism.
// The command must still be independently substantiated by an ordinary
// KnowledgeSource before it can enter ranking. relevance_floor is not
// provenance, syntax authority, or safety evidence; it is only a bounded
// relevance signal used by M11 research integrations.
struct CandidateHint {
    std::string command;
    double relevance_floor{0.0};
};

} // namespace acclorite
