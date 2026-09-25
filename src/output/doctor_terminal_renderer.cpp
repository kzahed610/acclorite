#include "acclorite/output/doctor_terminal_renderer.hpp"

#include <algorithm>
#include <ostream>
#include <string>
#include <string_view>

namespace acclorite {
namespace {

constexpr std::string_view kRule = "────────────────────────────────────────";
constexpr std::string_view kReset = "\x1b[0m";
constexpr std::string_view kBold = "\x1b[1m";
constexpr std::string_view kDim = "\x1b[2m";
constexpr std::string_view kGreen = "\x1b[32m";
constexpr std::string_view kYellow = "\x1b[33m";
constexpr std::string_view kRed = "\x1b[31m";

void styled(
    std::ostream& out,
    const bool color,
    const std::string_view ansi,
    const std::string_view text
) {
    if (color) {
        out << ansi << text << kReset;
    } else {
        out << text;
    }
}

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

std::string_view state_color(const diagnostics::DoctorState state) {
    switch (state) {
    case diagnostics::DoctorState::Ready:
        return kGreen;
    case diagnostics::DoctorState::Warning:
    case diagnostics::DoctorState::Optional:
        return kYellow;
    case diagnostics::DoctorState::Error:
        return kRed;
    }
    return kDim;
}

std::string summary_line(const diagnostics::DoctorReport& report) {
    const auto count = [&](const diagnostics::DoctorState state) {
        return std::count_if(report.checks.begin(), report.checks.end(), [&](const auto& check) {
            return check.state == state;
        });
    };

    const auto ready = count(diagnostics::DoctorState::Ready);
    const auto warnings = count(diagnostics::DoctorState::Warning);
    const auto optional = count(diagnostics::DoctorState::Optional);
    const auto errors = count(diagnostics::DoctorState::Error);

    return std::to_string(report.checks.size()) + " checks · " +
        std::to_string(ready) + " ready · " +
        std::to_string(warnings) + " warning" + (warnings == 1 ? "" : "s") + " · " +
        std::to_string(optional) + " optional · " +
        std::to_string(errors) + " error" + (errors == 1 ? "" : "s");
}

void render_check(
    const diagnostics::DoctorCheck& item,
    std::ostream& out,
    const bool color
) {
    out << "  ";
    styled(out, color, state_color(item.state), glyph(item.state));
    out << ' ';
    styled(out, color, kBold, item.label);
    if (!item.detail.empty()) {
        out << "\n      ";
        styled(out, color, kDim, item.detail);
    }
    out << '\n';
    if (!item.hint.empty()) {
        out << "      ";
        styled(out, color, kYellow, "↳ ");
        out << item.hint << '\n';
    }
}

} // namespace

void DoctorTerminalRenderer::render(
    const diagnostics::DoctorReport& report,
    std::ostream& out
) const {
    styled(out, color_, kBold, "Acclorite Doctor");
    out << '\n';
    styled(out, color_, kDim, kRule);
    out << "\n\n";

    if (report.has_errors()) {
        styled(out, color_, kRed, "✗ broken");
    } else if (std::ranges::any_of(report.checks, [](const auto& check) {
                   return check.state == diagnostics::DoctorState::Warning;
               })) {
        styled(out, color_, kYellow, "⚠ usable with warnings");
    } else {
        styled(out, color_, kGreen, "✓ healthy");
    }
    out << '\n';
    styled(out, color_, kDim, summary_line(report));
    out << "\n";
    styled(out, color_, kDim, "Read-only diagnostic · no changes were made");
    out << "\n";

    std::string previous_section;
    for (const auto& item : report.checks) {
        if (item.section != previous_section) {
            out << "\n";
            styled(out, color_, kBold, item.section);
            out << '\n';
            previous_section = item.section;
        }
        render_check(item, out, color_);
    }

    out << "\n";
    styled(out, color_, kDim, "Status  ");
    if (report.has_errors()) {
        styled(out, color_, kRed, "broken");
    } else if (report.has_warnings()) {
        styled(out, color_, kYellow, "usable with warnings");
    } else {
        styled(out, color_, kGreen, "healthy");
    }
    out << '\n';
}

} // namespace acclorite
