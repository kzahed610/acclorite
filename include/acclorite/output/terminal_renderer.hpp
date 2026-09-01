#pragma once

#include "acclorite/output/renderer.hpp"

namespace acclorite {

class TerminalRenderer final : public Renderer {
public:
    void render(const SearchResult& result, std::ostream& out) const override;
};

} // namespace acclorite
