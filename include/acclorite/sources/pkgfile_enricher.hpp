#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "acclorite/sources/enricher.hpp"

namespace acclorite {

class PkgfileEnricher final : public CandidateEnricher {
public:
    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::string_view diagnostic_name() const override { return "pkgfile"; }
    void enrich(const Query& query, Candidate& candidate) const override;
    void enrich_many(
        const Query& query,
        std::vector<Candidate>& candidates,
        std::size_t count
    ) const override;

private:
    void enrich_candidate(const Query& query, Candidate& candidate) const;
    [[nodiscard]] static std::size_t worker_count(std::size_t candidate_count);
    [[nodiscard]] static std::vector<std::string> parse_binaries(std::string_view output);
    [[nodiscard]] static std::string preferred_command(
        const Query& query,
        const Candidate& candidate,
        const std::vector<std::string>& commands
    );
};

} // namespace acclorite
