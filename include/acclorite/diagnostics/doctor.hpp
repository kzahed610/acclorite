#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace acclorite::diagnostics {

enum class DoctorState {
    Ready,
    Warning,
    Optional,
    Error,
};

struct DoctorCheck {
    std::string id;
    std::string section;
    std::string label;
    DoctorState state{DoctorState::Ready};
    std::string detail;
    std::string hint;
};

struct DoctorReport {
    std::vector<DoctorCheck> checks;

    [[nodiscard]] bool has_errors() const;
    [[nodiscard]] bool has_warnings() const;
    [[nodiscard]] std::string status() const;
};

class Doctor {
public:
    [[nodiscard]] DoctorReport run() const;
};

[[nodiscard]] std::string_view doctor_state_name(DoctorState state);

} // namespace acclorite::diagnostics
