#pragma once

namespace acclorite::machine {

inline constexpr int kSearchSchemaVersion = 15;
inline constexpr int kDoctorSchemaVersion = 1;
inline constexpr int kCapabilitiesSchemaVersion = 1;

enum class ExitCode : int {
    Success = 0,
    NoResult = 1,
    UsageError = 2,
    OperationalError = 3,
    DiagnosticError = 4,
};

[[nodiscard]] constexpr int exit_code(const ExitCode code) {
    return static_cast<int>(code);
}

} // namespace acclorite::machine
