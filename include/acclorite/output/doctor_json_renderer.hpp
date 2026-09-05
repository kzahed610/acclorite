#pragma once

#include <iosfwd>

#include "acclorite/diagnostics/doctor.hpp"

namespace acclorite {

class DoctorJsonRenderer {
public:
    void render(const diagnostics::DoctorReport& report, std::ostream& out) const;
};

} // namespace acclorite
