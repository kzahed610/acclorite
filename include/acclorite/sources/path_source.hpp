#pragma once

#include <span>
#include <filesystem>
#include <vector>

#include "acclorite/sources/source.hpp"

namespace acclorite {

class PathSource final : public KnowledgeSource {
public:
    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::string_view diagnostic_name() const override { return "path"; }
    [[nodiscard]] std::vector<Candidate> search(const Query& query) const override;
    [[nodiscard]] std::vector<Candidate> inspect_commands(
        const Query& query,
        std::span<const std::string> commands
    ) const override;
    [[nodiscard]] static std::vector<Candidate> catalog();

private:
    [[nodiscard]] static std::vector<std::filesystem::path> path_directories();
    [[nodiscard]] static double score_name(const Query& query, const std::string& command);
};

} // namespace acclorite
