#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "acclorite/sources/source.hpp"

namespace acclorite {

class ManSource final : public KnowledgeSource {
public:
    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::string_view diagnostic_name() const override { return "man"; }
    [[nodiscard]] std::vector<Candidate> search(const Query& query) const override;
    [[nodiscard]] std::vector<Candidate> inspect_commands(
        const Query& query,
        std::span<const std::string> commands
    ) const override;
    [[nodiscard]] static std::vector<Candidate> catalog();

private:
    struct ManualEntry {
        std::string command;
        std::string section;
        std::string summary;
    };

    [[nodiscard]] static std::vector<std::string> search_terms(const Query& query);
    [[nodiscard]] static std::vector<ManualEntry> parse_apropos(std::string_view output);
    [[nodiscard]] static double score_entry(const Query& query, const ManualEntry& entry);
};

} // namespace acclorite
