#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace acclorite {

enum class GuidanceSourceKind {
    Man,
    Info,
    Tldr,
    Curated,
    Package,
};

[[nodiscard]] inline std::string_view guidance_source_kind_name(const GuidanceSourceKind kind) {
    switch (kind) {
        case GuidanceSourceKind::Man: return "man";
        case GuidanceSourceKind::Info: return "info";
        case GuidanceSourceKind::Tldr: return "tldr";
        case GuidanceSourceKind::Curated: return "curated";
        case GuidanceSourceKind::Package: return "package";
    }
    return "unknown";
}

struct UsageExample {
    std::string text;
    GuidanceSourceKind source_kind{GuidanceSourceKind::Man};
    std::string source_reference;
    bool verified{true};
};

struct LearningResource {
    std::string label;
    std::string target;
    GuidanceSourceKind source_kind{GuidanceSourceKind::Man};
    std::string source_reference;
    bool verified{true};
};

struct GuidanceBundle {
    std::vector<UsageExample> examples;
    std::vector<LearningResource> learning_resources;
};

} // namespace acclorite
