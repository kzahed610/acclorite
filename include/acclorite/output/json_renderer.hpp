#pragma once

#include <string>

#include "acclorite/output/renderer.hpp"

namespace acclorite {

class JsonRenderer final : public Renderer {
public:
    void render(const SearchResult& result, std::ostream& out) const override;

private:
    [[nodiscard]] static std::string escape(std::string_view input);
};

} // namespace acclorite
