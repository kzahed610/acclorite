#pragma once

#include "acclorite/guidance/provider.hpp"

namespace acclorite {

class ManGuidanceProvider final : public GuidanceProvider {
public:
    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::string_view diagnostic_name() const override { return "man"; }
    [[nodiscard]] GuidanceBundle guide(const Candidate& candidate) const override;
};

} // namespace acclorite
