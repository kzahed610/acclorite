#include "acclorite/query/targets.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "acclorite/query/lexicon.hpp"

namespace acclorite::query {
namespace {

std::vector<std::string> words(std::string_view text) {
    std::vector<std::string> out;
    std::string current;
    for (const unsigned char ch : text) {
        if (std::isalnum(ch) || ch == '+' || ch == '#' || ch == '-' || ch == '_') {
            current.push_back(static_cast<char>(std::tolower(ch)));
        } else if (!current.empty()) {
            out.push_back(std::move(current));
            current.clear();
        }
    }
    if (!current.empty()) {
        out.push_back(std::move(current));
    }
    return out;
}

bool generic_target_word(std::string_view word) {
    static const std::unordered_set<std::string> generic{
        "a", "an", "and", "are", "about", "between", "bro", "can", "command",
        "difference", "differences", "do", "does", "explain", "for", "how", "i",
        "in", "is", "it", "me", "meaning", "my", "of", "on", "please", "program",
        "tell", "the", "thing", "this", "to", "tool", "usage", "use", "versus", "vs",
        "what", "where", "which", "why", "with"
    };
    return generic.contains(std::string(word));
}

std::vector<std::string> non_generic_words(const Query& query) {
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    for (const auto& word : words(query.normalized)) {
        if (word.size() < 2 || generic_target_word(word)) {
            continue;
        }
        if (seen.insert(word).second) {
            result.push_back(word);
        }
    }
    return result;
}

std::vector<std::string> compare_targets(const Query& query) {
    const auto tokens = words(query.normalized);
    if (tokens.empty()) {
        return {};
    }

    const auto pick_after = [&](const std::string_view marker) -> std::vector<std::string> {
        const auto marker_it = std::ranges::find(tokens, marker);
        if (marker_it == tokens.end()) {
            return {};
        }

        std::vector<std::string> result;
        for (auto it = std::next(marker_it); it != tokens.end(); ++it) {
            if (*it == "and" || *it == "vs" || *it == "versus") {
                continue;
            }
            if (generic_target_word(*it)) {
                continue;
            }
            result.push_back(*it);
            if (result.size() == 2) {
                break;
            }
        }
        return result;
    };

    if (auto result = pick_after("between"); result.size() >= 2) {
        return result;
    }

    // `rg vs grep` / `rg versus grep`
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i] != "vs" && tokens[i] != "versus") {
            continue;
        }
        std::vector<std::string> result;
        for (std::size_t left = i; left > 0; --left) {
            const auto& word = tokens[left - 1];
            if (!generic_target_word(word)) {
                result.push_back(word);
                break;
            }
        }
        for (std::size_t right = i + 1; right < tokens.size(); ++right) {
            const auto& word = tokens[right];
            if (!generic_target_word(word)) {
                result.push_back(word);
                break;
            }
        }
        if (result.size() == 2) {
            return result;
        }
    }

    if (auto result = pick_after("compare"); result.size() >= 2) {
        return result;
    }

    return {};
}

} // namespace

std::vector<std::string> frame_targets(const Query& query) {
    if (query.frame.frame == QueryFrame::Compare) {
        return compare_targets(query);
    }

    const auto terms = non_generic_words(query);
    if (terms.empty()) {
        return {};
    }

    if (query.frame.frame == QueryFrame::Explain) {
        // Explanation is normally about one named entity. Returning the first
        // concrete token prevents `what is rg` from becoming a search for every
        // package whose description happens to contain "rg".
        return {terms.front()};
    }

    if (query.frame.frame == QueryFrame::Diagnose) {
        // In diagnostic phrasing, the first concrete noun is usually the affected
        // tool/service (`ssh` in "why is ssh refusing my key"). This is a bias,
        // not proof; retrieval still falls back if the entity cannot be resolved.
        return {terms.front()};
    }

    if (query.frame.frame == QueryFrame::Locate) {
        // Location queries may contain a target plus resource kind (`ssh config`).
        // Keep both for the resource locator while the search engine can still use
        // the first target as the related tool/entity.
        return terms;
    }

    return {};
}

} // namespace acclorite::query
