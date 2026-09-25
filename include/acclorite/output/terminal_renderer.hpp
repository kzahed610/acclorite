#pragma once

#include "acclorite/output/renderer.hpp"

namespace acclorite {

class TerminalRenderer final : public Renderer {
public:
    explicit TerminalRenderer(bool color = false) : color_(color) {}

    void render(const SearchResult& result, std::ostream& out) const override;

private:
    bool color_{false};
};

} // namespace acclorite
