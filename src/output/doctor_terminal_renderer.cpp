#include "acclorite/output/doctor_terminal_renderer.hpp"

#include <ostream>
#include <string>

namespace acclorite {
namespace {

std::string_view glyph(const diagnostics::DoctorState state) {
    switch (state) {
    case diagnostics::DoctorState::Ready:
        return "✓";
    case diagnostics::DoctorState::Warning:
        return "⚠";
    case diagnostics::DoctorState::Optional:
        return "○";
    case diagnostics::DoctorState::Error:
        return "✗";
    }
    return "?";
}

} // namespace

void DoctorTerminalRenderer::render(
    const diagnostics::DoctorReport& report,
    std::ostream& out
) const {
    out << "Acclorite doctor\n";
    out << "────────────────────────────\n\n";

    std::string previous_section;
    for (const auto& item : report.checks) {
        if (item.section != previous_section) {
            if (!previous_section.empty()) {
                out << '\n';
            }
            out << item.section << '\n';
            previous_section = item.section;
        }

        out << glyph(item.state) << ' ' << item.label << '\n';
        if (!item.detail.empty()) {
            out << "  " << item.detail << '\n';
        }
        if (!item.hint.empty()) {
            out << "  → " << item.hint << '\n';
        }
    }

    out << "\nStatus\n";
    out << report.status() << '\n';
    out << "\nNo changes were made.\n";
}

} // namespace acclorite
