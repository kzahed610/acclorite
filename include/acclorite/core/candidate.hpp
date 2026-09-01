#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace acclorite {

enum class InterfaceKind {
    Unknown,
    Cli,
    Gui,
    Hybrid,
};

struct Candidate {
    std::string command;
    std::string path;
    std::string summary;
    std::string source;

    bool installed{false};
    bool repository_available{false};
    bool cli_capable{false};
    bool gui_capable{false};

    std::vector<std::string> matched_terms;
    double score{0.0};

    [[nodiscard]] InterfaceKind interface_kind() const {
        if (cli_capable && gui_capable) {
            return InterfaceKind::Hybrid;
        }
        if (cli_capable) {
            return InterfaceKind::Cli;
        }
        if (gui_capable) {
            return InterfaceKind::Gui;
        }
        return InterfaceKind::Unknown;
    }
};

[[nodiscard]] inline std::string_view interface_kind_name(const InterfaceKind kind) {
    switch (kind) {
        case InterfaceKind::Cli: return "CLI";
        case InterfaceKind::Gui: return "GUI";
        case InterfaceKind::Hybrid: return "Hybrid";
        case InterfaceKind::Unknown: return "Unknown";
    }
    return "Unknown";
}

} // namespace acclorite
