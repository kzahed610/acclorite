#include "acclorite/lore/quote_provider.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <random>
#include <ranges>
#include <string_view>

#ifndef ACCLORITE_QUOTES_BUILD_FILE
#define ACCLORITE_QUOTES_BUILD_FILE ""
#endif

#ifndef ACCLORITE_QUOTES_INSTALL_FILE
#define ACCLORITE_QUOTES_INSTALL_FILE ""
#endif

namespace acclorite::lore {
namespace {

constexpr std::size_t kMaxCorpusBytes = 1024 * 1024;
constexpr std::size_t kMaxQuoteBytes = 512;
constexpr std::size_t kMaxAttributionBytes = 128;
constexpr std::uint64_t kSurfaceDenominator = 12;

std::string trim(std::string value) {
    const auto first = std::ranges::find_if(value, [](const unsigned char ch) {
        return !std::isspace(ch);
    });
    value.erase(value.begin(), first);
    const auto last = std::ranges::find_if(value | std::views::reverse, [](const unsigned char ch) {
        return !std::isspace(ch);
    }).base();
    value.erase(last, value.end());
    return value;
}

bool printable_field(std::string_view value, const std::size_t max_bytes) {
    if (value.empty() || value.size() > max_bytes) {
        return false;
    }
    return std::ranges::all_of(value, [](const unsigned char ch) {
        return ch >= 0x20 && ch != 0x7f;
    });
}

std::uint64_t mix(std::uint64_t value) {
    // SplitMix64 finalizer. This is not cryptographic; it simply separates the
    // appearance decision from quote selection while keeping tests deterministic.
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

std::optional<std::filesystem::path> executable_relative_path() {
#ifdef __linux__
    std::error_code error;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error && !executable.empty()) {
        const auto candidate = executable.parent_path().parent_path() /
                               "share" / "acclorite" / "quotes.tsv";
        if (std::filesystem::is_regular_file(candidate, error) && !error) {
            return candidate;
        }
    }
#endif
    return std::nullopt;
}

std::optional<std::filesystem::path> first_existing_path() {
    if (const auto relative = executable_relative_path()) {
        return relative;
    }

    for (const char* raw : {ACCLORITE_QUOTES_INSTALL_FILE, ACCLORITE_QUOTES_BUILD_FILE}) {
        if (raw == nullptr || *raw == '\0') {
            continue;
        }
        std::error_code error;
        const std::filesystem::path path(raw);
        if (std::filesystem::is_regular_file(path, error) && !error) {
            return path;
        }
    }
    return std::nullopt;
}

} // namespace

QuoteProvider::QuoteProvider(std::filesystem::path quote_path) {
    if (quote_path.empty()) {
        if (const char* override_path = std::getenv("ACCLORITE_QUOTES");
            override_path != nullptr && *override_path != '\0') {
            quote_path = std::filesystem::path(override_path);
        } else if (const auto detected = first_existing_path()) {
            quote_path = *detected;
        }
    }
    quote_path_ = std::move(quote_path);
    if (quote_path_.empty()) {
        return;
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(quote_path_, error) || error) {
        return;
    }
    const auto size = std::filesystem::file_size(quote_path_, error);
    if (error || size == 0 || size > kMaxCorpusBytes) {
        return;
    }

    std::ifstream file(quote_path_);
    if (!file) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim(std::move(line));
        if (line.empty() || line.starts_with('#')) {
            continue;
        }

        const auto tab = line.find('\t');
        if (tab == std::string::npos || line.find('\t', tab + 1) != std::string::npos) {
            continue;
        }

        std::string attribution = trim(line.substr(0, tab));
        std::string text = trim(line.substr(tab + 1));
        if (!printable_field(attribution, kMaxAttributionBytes) ||
            !printable_field(text, kMaxQuoteBytes)) {
            continue;
        }

        quotes_.push_back(Quote{
            .text = std::move(text),
            .attribution = std::move(attribution),
        });
    }
}

bool QuoteProvider::should_surface(const std::uint64_t entropy) {
    return mix(entropy) % kSurfaceDenominator == 0;
}

std::uint64_t QuoteProvider::runtime_entropy() {
    std::random_device random;
    const auto now = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()
    );
    return (static_cast<std::uint64_t>(random()) << 32U) ^
           static_cast<std::uint64_t>(random()) ^ now;
}

std::optional<Quote> QuoteProvider::quote_for(const std::uint64_t entropy) const {
    if (quotes_.empty()) {
        return std::nullopt;
    }
    const auto index = static_cast<std::size_t>(mix(entropy ^ 0xd1b54a32d192ed03ULL) % quotes_.size());
    return quotes_[index];
}

} // namespace acclorite::lore
