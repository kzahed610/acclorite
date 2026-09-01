#pragma once

#include <filesystem>
#include <vector>

#include "acclorite/sources/source.hpp"

namespace acclorite {

class IndexSource final : public KnowledgeSource {
public:
    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::vector<Candidate> search(const Query& query) const override;

    [[nodiscard]] bool rebuild() const;
    [[nodiscard]] static std::filesystem::path database_path();

private:
    [[nodiscard]] bool ensure_ready() const;
};

} // namespace acclorite
