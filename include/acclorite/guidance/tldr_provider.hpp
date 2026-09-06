#pragma once

#include "acclorite/guidance/provider.hpp"

namespace acclorite {

class TldrGuidanceProvider final : public GuidanceProvider {
public:
    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::string_view diagnostic_name() const override { return "tldr"; }
    [[nodiscard]] GuidanceBundle guide(const Candidate& candidate) const override;
};

} // namespace acclorite
