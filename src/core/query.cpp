#include "acclorite/core/query.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace acclorite {
namespace {

std::string normalize(std::string_view input) {
    std::string out;
    out.reserve(input.size());

    bool previous_space = true;
    for (const unsigned char ch : input) {
        if (std::isspace(ch)) {
            if (!previous_space) {
                out.push_back(' ');
                previous_space = true;
            }
            continue;
        }

        out.push_back(static_cast<char>(std::tolower(ch)));
        previous_space = false;
    }

    if (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }

    return out;
}

} // namespace

Query Query::parse(std::string raw_query) {
    Query query;
    query.raw = std::move(raw_query);
    query.normalized = normalize(query.raw);

    std::istringstream stream(query.normalized);
    std::string token;
    while (stream >> token) {
        query.tokens.push_back(std::move(token));
    }

    return query;
}

} // namespace acclorite
