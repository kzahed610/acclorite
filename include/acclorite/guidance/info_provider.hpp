#pragma once

#include "acclorite/guidance/provider.hpp"

namespace acclorite {

class InfoGuidanceProvider final : public GuidanceProvider {
public:
    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::string_view diagnostic_name() const override { return "info"; }
    [[nodiscard]] GuidanceBundle guide(const Candidate& candidate) const override;
};

} // namespace acclorite
