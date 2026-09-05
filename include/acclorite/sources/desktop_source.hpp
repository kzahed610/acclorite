#pragma once

#include <filesystem>
#include <vector>

#include "acclorite/sources/source.hpp"

namespace acclorite {

class DesktopSource final : public KnowledgeSource {
public:
    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::string_view diagnostic_name() const override { return "desktop"; }
    [[nodiscard]] std::vector<Candidate> search(const Query& query) const override;
    [[nodiscard]] static std::vector<Candidate> catalog();

    [[nodiscard]] static std::vector<std::filesystem::path> application_directories();
};

} // namespace acclorite
