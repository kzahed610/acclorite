#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "acclorite/core/guidance.hpp"

namespace acclorite {

enum class SyntaxSourceKind {
    Man,
    Info,
    Completion,
    Curated,
};

[[nodiscard]] inline std::string_view syntax_source_kind_name(const SyntaxSourceKind kind) {
    switch (kind) {
        case SyntaxSourceKind::Man: return "man";
        case SyntaxSourceKind::Info: return "info";
        case SyntaxSourceKind::Completion: return "completion";
        case SyntaxSourceKind::Curated: return "curated";
    }
    return "unknown";
}

struct SyntaxProvenance {
    SyntaxSourceKind source_kind{SyntaxSourceKind::Man};
    std::string source_reference;
    std::string section;
};

struct CommandOption {
    std::vector<std::string> names;
    std::string description;
    std::optional<std::string> value_name;
    bool value_shape_known{true};
    bool takes_value{false};
    bool value_required{false};
    SyntaxProvenance provenance;
};

struct ArgumentSpec {
    std::string name;
    bool required{true};
    bool variadic{false};
    SyntaxProvenance provenance;
};

struct SynopsisAlternative {
    std::string text;
    SyntaxProvenance provenance;
};

struct SubcommandSpec {
    std::string name;
    std::string description;
    std::vector<CommandOption> options;
    std::vector<ArgumentSpec> positionals;
    SyntaxProvenance provenance;
    std::vector<SynopsisAlternative> synopsis;
};

struct CommandGrammar {
    std::string command;
    std::vector<CommandOption> global_options;
    std::vector<SubcommandSpec> subcommands;
    std::vector<ArgumentSpec> positionals;
    std::vector<SynopsisAlternative> synopsis;
};

struct InvocationArgument {
    std::string value;
    bool placeholder{false};
};

struct CommandInvocation {
    std::string command;
    std::vector<InvocationArgument> arguments;
    bool complete{true};
};

enum class ActionSafety {
    ReadOnly,
    Mutating,
    Destructive,
    Privileged,
    Network,
    Unknown,
};

[[nodiscard]] inline std::string_view action_safety_name(const ActionSafety safety) {
    switch (safety) {
        case ActionSafety::ReadOnly: return "ReadOnly";
        case ActionSafety::Mutating: return "Mutating";
        case ActionSafety::Destructive: return "Destructive";
        case ActionSafety::Privileged: return "Privileged";
        case ActionSafety::Network: return "Network";
        case ActionSafety::Unknown: return "Unknown";
    }
    return "Unknown";
}

struct ActionableAnswer {
    std::string command;
    std::string explanation;
    std::optional<CommandInvocation> invocation;
    std::vector<CommandOption> relevant_options;
    std::vector<SubcommandSpec> relevant_subcommands;
    std::vector<UsageExample> examples;
    std::vector<LearningResource> resources;
    ActionSafety safety{ActionSafety::Unknown};
};

} // namespace acclorite
