#pragma once

#include <vector>

#include "acclorite/core/candidate.hpp"
#include "acclorite/core/query.hpp"

namespace acclorite {

class KnowledgeSource {
public:
    virtual ~KnowledgeSource() = default;

    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual std::vector<Candidate> search(const Query& query) const = 0;
};

} // namespace acclorite
