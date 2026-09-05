#pragma once

#include <string_view>

#include "acclorite/core/candidate.hpp"
#include "acclorite/core/guidance.hpp"

namespace acclorite {

class GuidanceProvider {
public:
    virtual ~GuidanceProvider() = default;

    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual std::string_view diagnostic_name() const { return "guidance"; }
    [[nodiscard]] virtual GuidanceBundle guide(const Candidate& candidate) const = 0;
};

} // namespace acclorite
