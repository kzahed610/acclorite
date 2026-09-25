#include "acclorite/answer/composer.hpp"

#include "acclorite/answer/binder.hpp"
#include "acclorite/answer/safety_classifier.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace acclorite {
namespace {

std::string normalized_word(std::string_view input) {
    std::string word;
    word.reserve(input.size());
    for (const unsigned char ch : input) {
        if (std::isalnum(ch)) {
            word.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    if (word.size() > 3 && word.ends_with('s') && !word.ends_with("ss")) {
        word.pop_back();
    }
    return word;
}

std::vector<std::string> semantic_words(std::string_view input) {
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    std::string current;

    const auto flush = [&]() {
        if (current.empty()) {
            return;
        }
        std::string normalized = normalized_word(current);
        current.clear();
        if (normalized.size() < 2 || !seen.insert(normalized).second) {
            return;
        }
        result.push_back(std::move(normalized));
    };

    for (const unsigned char ch : input) {
        if (std::isalnum(ch)) {
            current.push_back(static_cast<char>(ch));
        } else {
            flush();
        }
    }
    flush();
    return result;
}

std::vector<std::string> option_name_words(const CommandOption& option) {
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    for (const auto& name : option.names) {
        for (auto word : semantic_words(name)) {
            if (seen.insert(word).second) {
                result.push_back(std::move(word));
            }
        }
    }
    return result;
}

bool contains_word(const std::vector<std::string>& haystack, const std::string& needle) {
    return std::ranges::find(haystack, needle) != haystack.end();
}

void prefer_readable_long_alias(CommandOption& option) {
    const auto long_name = std::ranges::find_if(option.names, [](const std::string& name) {
        return name.size() > 2 && name.starts_with("--");
    });
    if (long_name != option.names.end() && long_name != option.names.begin()) {
        std::iter_swap(option.names.begin(), long_name);
    }
}

std::optional<CommandOption> find_literal_option(
    const ActionTarget& target,
    const CommandGrammar& grammar
) {
    if (!target.literal) {
        return std::nullopt;
    }

    for (const auto& option : grammar.global_options) {
        const auto requested = std::ranges::find(option.names, *target.literal);
        if (requested == option.names.end()) {
            continue;
        }

        CommandOption matched = option;
        if (!matched.names.empty() && matched.names.front() != *target.literal) {
            auto requested_it = std::ranges::find(matched.names, *target.literal);
            if (requested_it != matched.names.end()) {
                std::iter_swap(matched.names.begin(), requested_it);
            }
        }
        return matched;
    }
    return std::nullopt;
}

struct SemanticOptionMatch {
    CommandOption option;
    double score{0.0};
    std::size_t matched_terms{0};
};

std::optional<CommandOption> find_semantic_option(
    const ActionTarget& target,
    const CommandGrammar& grammar
) {
    if (target.terms.empty()) {
        return std::nullopt;
    }

    std::vector<std::string> query_terms;
    std::unordered_set<std::string> seen;
    for (const auto& term : target.terms) {
        std::string normalized = normalized_word(term);
        if (normalized.size() >= 2 && seen.insert(normalized).second) {
            query_terms.push_back(std::move(normalized));
        }
    }
    if (query_terms.empty()) {
        return std::nullopt;
    }

    std::vector<SemanticOptionMatch> matches;
    matches.reserve(grammar.global_options.size());
    for (const auto& option : grammar.global_options) {
        const auto name_words = option_name_words(option);
        const auto description_words = semantic_words(option.description);

        double score = 0.0;
        std::size_t matched_terms = 0;
        for (const auto& term : query_terms) {
            const bool in_name = contains_word(name_words, term);
            const bool in_description = contains_word(description_words, term);
            if (!in_name && !in_description) {
                continue;
            }
            ++matched_terms;
            if (in_name) {
                score += 4.0;
            }
            if (in_description) {
                score += 2.0;
            }
        }

        if (matched_terms == 0) {
            continue;
        }

        const std::size_t required_matches = query_terms.size() <= 2
            ? query_terms.size()
            : std::max<std::size_t>(2, static_cast<std::size_t>(std::ceil(query_terms.size() * 0.67)));
        if (matched_terms < required_matches) {
            continue;
        }

        // Prefer an option whose documented text directly supports more of the
        // requested capability, but do not turn this into ranking evidence.
        score += static_cast<double>(matched_terms) * 1.5;
        matches.push_back(SemanticOptionMatch{
            .option = option,
            .score = score,
            .matched_terms = matched_terms,
        });
    }

    if (matches.empty()) {
        return std::nullopt;
    }

    std::ranges::sort(matches, [](const SemanticOptionMatch& lhs, const SemanticOptionMatch& rhs) {
        if (lhs.matched_terms != rhs.matched_terms) {
            return lhs.matched_terms > rhs.matched_terms;
        }
        if (std::abs(lhs.score - rhs.score) > 0.000001) {
            return lhs.score > rhs.score;
        }
        const std::string lhs_name = lhs.option.names.empty() ? std::string{} : lhs.option.names.front();
        const std::string rhs_name = rhs.option.names.empty() ? std::string{} : rhs.option.names.front();
        return lhs_name < rhs_name;
    });

    if (matches.size() > 1 &&
        matches[0].matched_terms == matches[1].matched_terms &&
        (matches[0].score - matches[1].score) < 1.0) {
        // A capability question is not permission to guess between equally
        // plausible flags. Ambiguous syntax is left unanswered for now.
        return std::nullopt;
    }

    CommandOption selected = matches.front().option;
    // Natural-language capability questions are easier to understand when the
    // self-describing long spelling is rendered first. Explicit syntax queries
    // still preserve the exact spelling the user typed via find_literal_option().
    prefer_readable_long_alias(selected);
    return selected;
}

struct SemanticSubcommandMatch {
    SubcommandSpec subcommand;
    double score{0.0};
    std::size_t matched_terms{0};
};

std::optional<SubcommandSpec> find_semantic_subcommand(
    const ActionTarget& target,
    const CommandGrammar& grammar
) {
    if (target.terms.empty()) {
        return std::nullopt;
    }

    std::vector<std::string> query_terms;
    std::unordered_set<std::string> seen;
    for (const auto& term : target.terms) {
        std::string normalized = normalized_word(term);
        if (normalized.size() >= 2 && seen.insert(normalized).second) {
            query_terms.push_back(std::move(normalized));
        }
    }
    if (query_terms.empty()) {
        return std::nullopt;
    }

    std::vector<SemanticSubcommandMatch> matches;
    matches.reserve(grammar.subcommands.size());
    for (const auto& subcommand : grammar.subcommands) {
        const auto name_words = semantic_words(subcommand.name);
        const auto description_words = semantic_words(subcommand.description);

        double score = 0.0;
        std::size_t matched_terms = 0;
        for (const auto& term : query_terms) {
            const bool in_name = contains_word(name_words, term);
            const bool in_description = contains_word(description_words, term);
            if (!in_name && !in_description) {
                continue;
            }
            ++matched_terms;
            if (in_name) {
                score += 4.0;
            }
            if (in_description) {
                score += 2.0;
            }
        }

        const std::size_t required_matches = query_terms.size() <= 2
            ? query_terms.size()
            : std::max<std::size_t>(2, static_cast<std::size_t>(std::ceil(query_terms.size() * 0.67)));
        if (matched_terms < required_matches) {
            continue;
        }

        score += static_cast<double>(matched_terms) * 1.5;
        matches.push_back(SemanticSubcommandMatch{
            .subcommand = subcommand,
            .score = score,
            .matched_terms = matched_terms,
        });
    }

    if (matches.empty()) {
        return std::nullopt;
    }

    std::ranges::sort(matches, [](const SemanticSubcommandMatch& lhs, const SemanticSubcommandMatch& rhs) {
        if (lhs.matched_terms != rhs.matched_terms) {
            return lhs.matched_terms > rhs.matched_terms;
        }
        if (std::abs(lhs.score - rhs.score) > 0.000001) {
            return lhs.score > rhs.score;
        }
        return lhs.subcommand.name < rhs.subcommand.name;
    });

    if (matches.size() > 1 &&
        matches[0].matched_terms == matches[1].matched_terms &&
        (matches[0].score - matches[1].score) < 1.0) {
        return std::nullopt;
    }

    return matches.front().subcommand;
}

struct DeepSubcommandOptionMatch {
    SubcommandSpec subcommand;
    CommandOption option;
    double score{0.0};
    std::size_t matched_terms{0};
    std::size_t unmatched_name_terms{0};
};

std::optional<DeepSubcommandOptionMatch> find_semantic_subcommand_option(
    const ActionTarget& target,
    const CommandGrammar& grammar,
    const AnswerComposer::SubcommandGrammarResolver& resolver
) {
    if (!resolver || target.terms.size() < 2 || grammar.subcommands.empty()) {
        return std::nullopt;
    }

    std::vector<std::string> query_terms;
    std::unordered_set<std::string> seen_terms;
    for (const auto& term : target.terms) {
        std::string normalized = normalized_word(term);
        if (normalized.size() >= 2 && seen_terms.insert(normalized).second) {
            query_terms.push_back(std::move(normalized));
        }
    }
    if (query_terms.size() < 2) {
        return std::nullopt;
    }

    struct RootSeed {
        SubcommandSpec subcommand;
        double score{0.0};
        std::size_t matched_terms{0};
    };
    std::vector<RootSeed> seeds;
    for (const auto& subcommand : grammar.subcommands) {
        const auto name_words = semantic_words(subcommand.name);
        const auto description_words = semantic_words(subcommand.description);
        std::size_t matched = 0;
        double score = 0.0;
        for (const auto& term : query_terms) {
            const bool in_name = contains_word(name_words, term);
            const bool in_description = contains_word(description_words, term);
            if (!in_name && !in_description) {
                continue;
            }
            ++matched;
            score += in_name ? 4.0 : 0.0;
            score += in_description ? 2.0 : 0.0;
        }
        if (matched > 0) {
            seeds.push_back(RootSeed{.subcommand = subcommand, .score = score, .matched_terms = matched});
        }
    }
    if (seeds.empty()) {
        return std::nullopt;
    }

    std::size_t best_root_coverage = 0;
    for (const auto& seed : seeds) {
        best_root_coverage = std::max(best_root_coverage, seed.matched_terms);
    }
    for (const auto& option : grammar.global_options) {
        const auto names = option_name_words(option);
        const auto description = semantic_words(option.description);
        std::size_t matched = 0;
        for (const auto& term : query_terms) {
            if (contains_word(names, term) || contains_word(description, term)) {
                ++matched;
            }
        }
        best_root_coverage = std::max(best_root_coverage, matched);
    }

    // Deeper grammar must explain something the root grammar could not. If a
    // root option/subcommand already covers the whole request, preferring a
    // child combination could introduce an unrequested extra operation.
    if (best_root_coverage >= query_terms.size()) {
        return std::nullopt;
    }

    std::ranges::sort(seeds, [](const RootSeed& lhs, const RootSeed& rhs) {
        if (lhs.matched_terms != rhs.matched_terms) return lhs.matched_terms > rhs.matched_terms;
        if (std::abs(lhs.score - rhs.score) > 0.000001) return lhs.score > rhs.score;
        return lhs.subcommand.name < rhs.subcommand.name;
    });

    std::vector<DeepSubcommandOptionMatch> matches;
    const std::size_t inspection_limit = std::min<std::size_t>(3, seeds.size());
    for (std::size_t i = 0; i < inspection_limit; ++i) {
        auto enriched = resolver(seeds[i].subcommand);
        if (!enriched) {
            continue;
        }

        const auto sub_name_words = semantic_words(enriched->name);
        const auto sub_description_words = semantic_words(enriched->description);
        for (const auto& raw_option : enriched->options) {
            const auto option_words = option_name_words(raw_option);
            const auto option_description_words = semantic_words(raw_option.description);
            std::size_t matched = 0;
            double score = 0.0;
            for (const auto& term : query_terms) {
                const bool in_sub_name = contains_word(sub_name_words, term);
                const bool in_sub_description = contains_word(sub_description_words, term);
                const bool in_option_name = contains_word(option_words, term);
                const bool in_option_description = contains_word(option_description_words, term);
                if (!in_sub_name && !in_sub_description && !in_option_name && !in_option_description) {
                    continue;
                }
                ++matched;
                score += in_sub_name ? 4.0 : 0.0;
                score += in_option_name ? 4.0 : 0.0;
                score += in_sub_description ? 2.0 : 0.0;
                score += in_option_description ? 2.0 : 0.0;
            }

            // Deep grammar exists to disambiguate compound operations. Require
            // every requested capability term to be supported across the proven
            // subcommand + option pair; partial child matches are not enough.
            if (matched != query_terms.size()) {
                continue;
            }

            std::size_t unmatched_name_terms = 0;
            for (const auto& word : option_words) {
                if (!contains_word(query_terms, word)) {
                    ++unmatched_name_terms;
                }
            }

            CommandOption option = raw_option;
            prefer_readable_long_alias(option);
            matches.push_back(DeepSubcommandOptionMatch{
                .subcommand = *enriched,
                .option = std::move(option),
                .score = score + static_cast<double>(matched) * 1.5,
                .matched_terms = matched,
                .unmatched_name_terms = unmatched_name_terms,
            });
        }
    }

    if (matches.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(matches, [](const DeepSubcommandOptionMatch& lhs, const DeepSubcommandOptionMatch& rhs) {
        if (lhs.matched_terms != rhs.matched_terms) return lhs.matched_terms > rhs.matched_terms;
        if (lhs.unmatched_name_terms != rhs.unmatched_name_terms) {
            return lhs.unmatched_name_terms < rhs.unmatched_name_terms;
        }
        if (std::abs(lhs.score - rhs.score) > 0.000001) return lhs.score > rhs.score;
        if (lhs.subcommand.name != rhs.subcommand.name) return lhs.subcommand.name < rhs.subcommand.name;
        const std::string lhs_name = lhs.option.names.empty() ? std::string{} : lhs.option.names.front();
        const std::string rhs_name = rhs.option.names.empty() ? std::string{} : rhs.option.names.front();
        return lhs_name < rhs_name;
    });

    if (matches.size() > 1 &&
        matches[0].matched_terms == matches[1].matched_terms &&
        matches[0].unmatched_name_terms == matches[1].unmatched_name_terms &&
        (matches[0].score - matches[1].score) < 1.0) {
        return std::nullopt;
    }
    return matches.front();
}

bool should_bind_invocation(const Query& query) {
    return query.frame.frame == QueryFrame::Discover ||
           query.frame.frame == QueryFrame::Inspect ||
           query.frame.frame == QueryFrame::Modify;
}

std::optional<CommandInvocation> bind_global_option_practically(
    const Query& query,
    const CommandGrammar& grammar,
    const CommandOption& option
) {
    auto partial = ArgumentBinder::bind_option(query, grammar, option);
    auto rooted = ArgumentBinder::bind_root_option(query, grammar, option);
    if (!rooted) {
        return partial;
    }
    if (rooted->complete) {
        return rooted;
    }
    if (!partial) {
        return rooted;
    }

    const auto literal_count = [](const CommandInvocation& invocation) {
        return std::ranges::count_if(invocation.arguments, [](const InvocationArgument& argument) {
            return !argument.placeholder;
        });
    };
    // An incomplete root composition is useful only when it actually binds more
    // user data than the established option-only template. Extra placeholders
    // alone are not progress and would destabilize previously useful answers.
    return literal_count(*rooted) > literal_count(*partial) ? rooted : partial;
}

ActionableAnswer classified_answer(
    const Query& query,
    const Candidate& candidate,
    ActionableAnswer answer
) {
    answer.safety = SafetyClassifier::classify(query, candidate, answer);
    return answer;
}

} // namespace

bool AnswerComposer::may_compose(const Query& query) {
    return query.action_target.has_value();
}

std::optional<ActionableAnswer> AnswerComposer::compose(
    const Query& query,
    const Candidate& candidate,
    const CommandGrammar& grammar,
    const SubcommandGrammarResolver& subcommand_resolver
) {
    if (candidate.command.empty() || grammar.command != candidate.command || !query.action_target) {
        return std::nullopt;
    }

    const auto& target = *query.action_target;
    if (target.kind == ActionTargetKind::Option) {
        std::optional<CommandOption> matched = target.explicit_syntax
            ? find_literal_option(target, grammar)
            : find_semantic_option(target, grammar);
        if (!matched) {
            return std::nullopt;
        }

        std::string explanation;
        if (!matched->description.empty()) {
            explanation = matched->description;
        } else if (!matched->names.empty()) {
            explanation = matched->names.front() + " is a verified option for " + candidate.command + ".";
        } else {
            explanation = "A verified option matches the requested capability.";
        }

        return classified_answer(query, candidate, ActionableAnswer{
            .command = candidate.command,
            .explanation = std::move(explanation),
            .invocation = should_bind_invocation(query)
                ? bind_global_option_practically(query, grammar, *matched)
                : std::nullopt,
            .relevant_options = {std::move(*matched)},
            .relevant_subcommands = {},
            .examples = candidate.examples,
            .resources = candidate.learning_resources,
            .safety = ActionSafety::Unknown,
        });
    }

    if (target.kind == ActionTargetKind::Subcommand && !target.explicit_syntax) {
        auto matched = find_semantic_subcommand(target, grammar);
        if (!matched) {
            return std::nullopt;
        }

        std::string explanation = !matched->description.empty()
            ? matched->description
            : matched->name + " is a verified subcommand for " + candidate.command + ".";
        return classified_answer(query, candidate, ActionableAnswer{
            .command = candidate.command,
            .explanation = std::move(explanation),
            .invocation = should_bind_invocation(query)
                ? ArgumentBinder::bind_subcommand(query, grammar, *matched)
                : std::nullopt,
            .relevant_options = {},
            .relevant_subcommands = {std::move(*matched)},
            .examples = candidate.examples,
            .resources = candidate.learning_resources,
            .safety = ActionSafety::Unknown,
        });
    }

    if (target.kind == ActionTargetKind::Operation && !target.explicit_syntax) {
        if (auto deep = find_semantic_subcommand_option(target, grammar, subcommand_resolver)) {
            std::string explanation = !deep->option.description.empty()
                ? deep->option.description
                : deep->subcommand.name + " plus " +
                    (deep->option.names.empty() ? std::string("a verified option") : deep->option.names.front()) +
                    " matches the requested operation.";
            return classified_answer(query, candidate, ActionableAnswer{
                .command = candidate.command,
                .explanation = std::move(explanation),
                .invocation = should_bind_invocation(query)
                    ? ArgumentBinder::bind_subcommand_option(
                        query, grammar, deep->subcommand, deep->option
                    )
                    : std::nullopt,
                .relevant_options = {std::move(deep->option)},
                .relevant_subcommands = {std::move(deep->subcommand)},
                .examples = candidate.examples,
                .resources = candidate.learning_resources,
                .safety = ActionSafety::Unknown,
            });
        }

        auto option = find_semantic_option(target, grammar);
        auto subcommand = find_semantic_subcommand(target, grammar);

        // Operation recovery is allowed to discover the syntactic species, but
        // not to guess between two independently plausible grammar facts.
        if (option && subcommand) {
            return std::nullopt;
        }
        if (option) {
            std::string explanation = !option->description.empty()
                ? option->description
                : (!option->names.empty()
                    ? option->names.front() + " is a verified option for " + candidate.command + "."
                    : "A verified option matches the requested operation.");
            return classified_answer(query, candidate, ActionableAnswer{
                .command = candidate.command,
                .explanation = std::move(explanation),
                .invocation = should_bind_invocation(query)
                    ? bind_global_option_practically(query, grammar, *option)
                    : std::nullopt,
                .relevant_options = {std::move(*option)},
                .relevant_subcommands = {},
                .examples = candidate.examples,
                .resources = candidate.learning_resources,
                .safety = ActionSafety::Unknown,
            });
        }
        if (subcommand) {
            std::string explanation = !subcommand->description.empty()
                ? subcommand->description
                : subcommand->name + " is a verified subcommand for " + candidate.command + ".";
            return classified_answer(query, candidate, ActionableAnswer{
                .command = candidate.command,
                .explanation = std::move(explanation),
                .invocation = should_bind_invocation(query)
                    ? ArgumentBinder::bind_subcommand(query, grammar, *subcommand)
                    : std::nullopt,
                .relevant_options = {},
                .relevant_subcommands = {std::move(*subcommand)},
                .examples = candidate.examples,
                .resources = candidate.learning_resources,
                .safety = ActionSafety::Unknown,
            });
        }

        if (should_bind_invocation(query)) {
            if (auto invocation = ArgumentBinder::bind_root(query, grammar)) {
                std::string explanation = candidate.summary.empty()
                    ? candidate.command + " has a verified root command shape matching the requested operation."
                    : candidate.summary;
                return classified_answer(query, candidate, ActionableAnswer{
                    .command = candidate.command,
                    .explanation = std::move(explanation),
                    .invocation = std::move(invocation),
                    .relevant_options = {},
                    .relevant_subcommands = {},
                    .examples = candidate.examples,
                    .resources = candidate.learning_resources,
                    .safety = ActionSafety::Unknown,
                });
            }
        }
        return std::nullopt;
    }

    // Positionals remain structural only until binding is independently proven.
    return std::nullopt;
}

} // namespace acclorite
