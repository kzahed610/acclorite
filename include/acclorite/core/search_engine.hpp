#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "acclorite/core/query.hpp"
#include "acclorite/core/result.hpp"
#include "acclorite/sources/source.hpp"

namespace acclorite {

class SearchEngine {
public:
    void add_source(std::unique_ptr<KnowledgeSource> source);

    [[nodiscard]] SearchResult search(const Query& query, std::size_t limit = 10) const;

private:
    std::vector<std::unique_ptr<KnowledgeSource>> sources_;
};

} // namespace acclorite
