#include "acclorite/query/fuzzy.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace acclorite::query::fuzzy {
namespace {

std::string fold(std::string_view input) {
    std::string result;
    result.reserve(input.size());
    for (const unsigned char ch : input) {
        result.push_back(static_cast<char>(std::tolower(ch)));
    }
    return result;
}

} // namespace

double similarity(const std::string_view lhs_raw, const std::string_view rhs_raw) {
    const std::string lhs = fold(lhs_raw);
    const std::string rhs = fold(rhs_raw);

    if (lhs == rhs) {
        return 1.0;
    }
    if (lhs.empty() || rhs.empty()) {
        return 0.0;
    }

    // Optimal-string-alignment Damerau-Levenshtein only needs the current and
    // previous two rows. The old implementation allocated an (m+1)*(n+1)
    // matrix for every token comparison; relevance scoring can make thousands
    // of these calls for one FTS result set.
    //
    // Thread-local row buffers retain capacity across calls, eliminating the
    // remaining hot-loop allocations while preserving the exact distance
    // recurrence and therefore the same similarity values.
    thread_local std::vector<std::size_t> previous_previous;
    thread_local std::vector<std::size_t> previous;
    thread_local std::vector<std::size_t> current;

    const std::size_t columns = rhs.size() + 1;
    previous_previous.resize(columns);
    previous.resize(columns);
    current.resize(columns);

    for (std::size_t col = 0; col < columns; ++col) {
        previous[col] = col;
        previous_previous[col] = col;
    }

    for (std::size_t row = 1; row <= lhs.size(); ++row) {
        current[0] = row;

        for (std::size_t col = 1; col <= rhs.size(); ++col) {
            const std::size_t substitution_cost = lhs[row - 1] == rhs[col - 1] ? 0 : 1;
            current[col] = std::min({
                previous[col] + 1,
                current[col - 1] + 1,
                previous[col - 1] + substitution_cost,
            });

            if (row > 1 && col > 1 &&
                lhs[row - 1] == rhs[col - 2] &&
                lhs[row - 2] == rhs[col - 1]) {
                current[col] = std::min(
                    current[col],
                    previous_previous[col - 2] + 1
                );
            }
        }

        previous_previous.swap(previous);
        previous.swap(current);
    }

    const std::size_t edits = previous[rhs.size()];
    const std::size_t scale = std::max(lhs.size(), rhs.size());
    return 1.0 - (static_cast<double>(edits) / static_cast<double>(scale));
}

bool similar(const std::string_view lhs, const std::string_view rhs, const double threshold) {
    return similarity(lhs, rhs) >= threshold;
}

} // namespace acclorite::query::fuzzy
