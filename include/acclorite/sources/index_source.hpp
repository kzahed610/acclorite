#pragma once

#include <span>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "acclorite/sources/source.hpp"

namespace acclorite {

struct IndexStatus {
    bool sqlite_supported{false};
    bool exists{false};
    bool ready{false};
    bool fingerprinted{false};
    bool stale{false};
    std::filesystem::path path;
    std::uintmax_t size_bytes{0};
    std::vector<std::string> stale_sources;
};

class IndexSource final : public KnowledgeSource {
public:
    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::string_view diagnostic_name() const override { return "index"; }
    [[nodiscard]] std::vector<Candidate> search(const Query& query) const override;
    [[nodiscard]] std::vector<Candidate> inspect_commands(
        const Query& query,
        std::span<const std::string> commands
    ) const override;

    [[nodiscard]] bool rebuild() const;
    [[nodiscard]] static std::filesystem::path database_path();
    [[nodiscard]] static IndexStatus probe();

private:
    [[nodiscard]] bool ensure_ready() const;
    mutable bool ready_hint_{false};
};

} // namespace acclorite
