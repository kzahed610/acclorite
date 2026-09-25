#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "acclorite/core/candidate.hpp"
#include "acclorite/core/query.hpp"

namespace acclorite {

class KnowledgeSource {
public:
    virtual ~KnowledgeSource() = default;

    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual std::string_view diagnostic_name() const { return "source"; }
    [[nodiscard]] virtual std::vector<Candidate> search(const Query& query) const = 0;

    // Optional bounded exact inspection used when another recall mechanism has
    // proposed command identities. The hint itself is not evidence: a source may
    // return only candidates it can independently substantiate from its ordinary
    // local data, scored against the original query.
    [[nodiscard]] virtual std::vector<Candidate> inspect_commands(
        const Query&,
        std::span<const std::string>
    ) const {
        return {};
    }
};

} // namespace acclorite
