#include "acclorite/query/relevance.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <string>
#include <vector>

#include "acclorite/query/lexicon.hpp"

namespace acclorite::query {
namespace {

std::string lower(std::string_view input) {
    std::string out(input);
    std::ranges::transform(out, out.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

std::vector<std::string> words(std::string_view text) {
    std::vector<std::string> result;
    std::string current;

    for (const unsigned char ch : text) {
        if (std::isalnum(ch) || ch == '+' || ch == '#') {
            current.push_back(static_cast<char>(std::tolower(ch)));
        } else if (!current.empty()) {
            result.push_back(std::move(current));
            current.clear();
        }
    }

    if (!current.empty()) {
        result.push_back(std::move(current));
    }

    return result;
}

bool related_word(const std::string_view candidate, const std::string_view term) {
    if (candidate == term) {
        return true;
    }

    if (candidate.size() < 4 || term.size() < 4) {
        return false;
    }

    const std::size_t limit = std::min(candidate.size(), term.size());
    std::size_t common = 0;
    while (common < limit && candidate[common] == term[common]) {
        ++common;
    }

    // Cheap morphology until RapidFuzz / stronger stemming arrives.
    // This intentionally catches families such as archive/archiver/archiving.
    return common >= 4 && common + 2 >= std::min(candidate.size(), term.size());
}

double term_quality(
    const ConceptGroup& group,
    const std::vector<std::string>& name_words,
    const std::vector<std::string>& description_words
) {
    const auto contains = [](const std::vector<std::string>& haystack, const std::string_view needle) {
        return std::ranges::any_of(haystack, [&](const std::string& word) {
            return related_word(word, needle);
        });
    };

    if (contains(name_words, group.term)) {
        return 1.00;
    }
    if (contains(description_words, group.term)) {
        return 0.94;
    }

    for (const auto& alternative : group.alternatives) {
        if (alternative == group.term) {
            continue;
        }
        if (contains(name_words, alternative)) {
            return 0.80;
        }
        if (contains(description_words, alternative)) {
            return 0.70;
        }
    }

    return 0.0;
}

} // namespace

RelevanceMatch score_text(
    const Query& query,
    const std::string_view name,
    const std::string_view description,
    const double exact_name_score,
    const double full_match_ceiling
) {
    RelevanceMatch result;
    if (query.normalized.empty()) {
        return result;
    }

    const std::string normalized_name = lower(name);
    if (normalized_name == query.normalized) {
        result.score = exact_name_score;
        for (const auto& group : concept_groups(query)) {
            result.matched_terms.push_back(group.term);
        }
        return result;
    }

    const auto groups = concept_groups(query);
    if (groups.empty()) {
        return result;
    }

    const auto name_words = words(normalized_name);
    const auto description_words = words(description);

    std::size_t matched_groups = 0;
    double quality_sum = 0.0;

    for (const auto& group : groups) {
        const double quality = term_quality(group, name_words, description_words);
        if (quality <= 0.0) {
            continue;
        }

        ++matched_groups;
        quality_sum += quality;
        result.matched_terms.push_back(group.term);
    }

    if (matched_groups == 0) {
        return result;
    }

    const double coverage = static_cast<double>(matched_groups) /
                            static_cast<double>(groups.size());
    const double average_quality = quality_sum / static_cast<double>(matched_groups);

    if (matched_groups == groups.size()) {
        result.score = 0.68 + (0.22 * average_quality);
    } else {
        result.score = 0.14 + (0.32 * coverage) + (0.16 * average_quality);
    }

    result.score = std::min(result.score, full_match_ceiling);
    return result;
}

} // namespace acclorite::query
