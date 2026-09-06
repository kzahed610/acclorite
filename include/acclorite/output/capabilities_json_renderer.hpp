#pragma once

#include <iosfwd>
#include <string_view>

#include "acclorite/core/capabilities.hpp"

namespace acclorite {

class CapabilitiesJsonRenderer {
public:
    void render(
        const CapabilityReport& report,
        std::string_view acclorite_version,
        std::ostream& out
    ) const;
};

} // namespace acclorite
