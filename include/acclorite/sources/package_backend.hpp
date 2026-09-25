#pragma once

#include <string_view>

#include "acclorite/sources/source.hpp"
#include "acclorite/system/distro.hpp"

namespace acclorite {

// Common contract for distribution-specific package discovery. Backends project
// distro metadata into the normal Candidate model so ranking stays completely
// distribution-agnostic.
class PackageBackend : public KnowledgeSource {
public:
    ~PackageBackend() override = default;

    [[nodiscard]] virtual PackageFamily package_family() const = 0;
    [[nodiscard]] virtual std::string_view backend_id() const = 0;
};

} // namespace acclorite
