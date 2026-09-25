#pragma once

#include <iosfwd>

#include "acclorite/diagnostics/doctor.hpp"

namespace acclorite {

class DoctorTerminalRenderer {
public:
    explicit DoctorTerminalRenderer(bool color = false) : color_(color) {}

    void render(const diagnostics::DoctorReport& report, std::ostream& out) const;

private:
    bool color_{false};
};

} // namespace acclorite
