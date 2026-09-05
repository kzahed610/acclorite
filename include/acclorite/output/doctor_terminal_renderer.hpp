#pragma once

#include <iosfwd>

#include "acclorite/diagnostics/doctor.hpp"

namespace acclorite {

class DoctorTerminalRenderer {
public:
    void render(const diagnostics::DoctorReport& report, std::ostream& out) const;
};

} // namespace acclorite
