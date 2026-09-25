#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace acclorite::lore {

struct Quote {
    std::string text;
    std::string attribution;
};

class QuoteProvider {
public:
    explicit QuoteProvider(std::filesystem::path quote_path = {});

    [[nodiscard]] bool available() const { return !quotes_.empty(); }
    [[nodiscard]] const std::filesystem::path& quote_path() const { return quote_path_; }

    // The lore feature is intentionally sparse: roughly one ordinary terminal query in
    // twelve receives flavor text. Explicit --lore bypasses this gate.
    [[nodiscard]] static bool should_surface(std::uint64_t entropy);
    [[nodiscard]] static std::uint64_t runtime_entropy();
    [[nodiscard]] std::optional<Quote> quote_for(std::uint64_t entropy) const;

private:
    std::filesystem::path quote_path_;
    std::vector<Quote> quotes_;
};

} // namespace acclorite::lore
