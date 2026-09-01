#pragma once

#include <iosfwd>

#include "acclorite/core/result.hpp"

namespace acclorite {

class Renderer {
public:
    virtual ~Renderer() = default;
    virtual void render(const SearchResult& result, std::ostream& out) const = 0;
};

} // namespace acclorite
