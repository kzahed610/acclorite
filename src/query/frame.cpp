#include "acclorite/query/frame.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "acclorite/query/lexicon.hpp"

namespace acclorite {

std::string_view query_frame_name(const QueryFrame frame) {
    switch (frame) {
        case QueryFrame::Discover: return "Discover";
        case QueryFrame::Explain: return "Explain";
        case QueryFrame::Locate: return "Locate";
        case QueryFrame::Inspect: return "Inspect";
        case QueryFrame::Modify: return "Modify";
        case QueryFrame::Compare: return "Compare";
        case QueryFrame::Diagnose: return "Diagnose";
        case QueryFrame::Unknown: return "Unknown";
    }
    return "Unknown";
}

} // namespace acclorite

namespace acclorite::query {
namespace {

struct FrameEvidence {
    double score{0.0};
    bool explicit_frame{false};
    std::vector<std::string> signals;
};

std::vector<std::string> lexical_words(const std::string_view input) {
    std::vector<std::string> result;
    std::string current;

    for (const unsigned char ch : input) {
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

std::string phrase_text(const std::vector<std::string>& words) {
    std::string result;
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (i != 0) {
            result.push_back(' ');
        }
        result += words[i];
    }
    return result;
}

bool contains_phrase(const std::string_view text, const std::string_view phrase) {
    if (text == phrase) {
        return true;
    }
    const std::string padded_text = " " + std::string(text) + " ";
    const std::string padded_phrase = " " + std::string(phrase) + " ";
    return padded_text.find(padded_phrase) != std::string::npos;
}

bool contains_word(const std::vector<std::string>& words, const std::string_view needle) {
    return std::ranges::find(words, needle) != words.end();
}

bool entity_compare_shape(const std::vector<std::string>& words, const std::string_view text) {
    // Compare is an entity-first frame ("rg vs grep", "difference between X and Y").
    // A task request such as "compare two folders" uses *compare* as the action
    // the desired tool should perform, so it must remain ordinary discovery.
    if (contains_phrase(text, "difference between") || contains_phrase(text, "differences between") ||
        contains_word(words, "vs") || contains_word(words, "versus")) {
        return true;
    }

    const auto marker = std::ranges::find_if(words, [](const std::string& word) {
        return word == "compare" || word == "comparing";
    });
    if (marker == words.end()) {
        return false;
    }

    // Quantity/object wording describes data to compare, not two named tools.
    // Check this *before* conjunctions: "compare these two directories and tell
    // me what's different" is still a task request, not an entity comparison.
    static constexpr std::array<std::string_view, 12> task_objects{
        "two", "files", "file", "folders", "folder", "directories", "directory",
        "images", "image", "texts", "text", "strings",
    };
    for (auto it = std::next(marker); it != words.end(); ++it) {
        if (std::ranges::find(task_objects, std::string_view(*it)) != task_objects.end()) {
            return false;
        }
    }

    // Explicit conjunction is a strong entity-comparison shape only after we
    // have ruled out ordinary task objects above ("compare rg and grep").
    if (std::ranges::find(std::next(marker), words.end(), "and") != words.end()) {
        return true;
    }

    // Preserve terse developer phrasing such as `compare rg grep`.
    return std::distance(std::next(marker), words.end()) >= 2;
}

void add_evidence(
    std::unordered_map<QueryFrame, FrameEvidence>& evidence,
    const QueryFrame frame,
    const double amount,
    const bool explicit_frame,
    std::string signal
) {
    auto& slot = evidence[frame];
    slot.score += amount;
    slot.explicit_frame = slot.explicit_frame || explicit_frame;
    if (std::ranges::find(slot.signals, signal) == slot.signals.end()) {
        slot.signals.push_back(std::move(signal));
    }
}

void add_phrase_patterns(
    std::unordered_map<QueryFrame, FrameEvidence>& evidence,
    const std::string_view text,
    const QueryFrame frame,
    const std::initializer_list<std::pair<std::string_view, double>> patterns
) {
    for (const auto& [phrase, weight] : patterns) {
        if (contains_phrase(text, phrase)) {
            add_evidence(evidence, frame, weight, true, std::string(phrase));
        }
    }
}

double confidence_from_score(const double best, const double runner_up, const bool explicit_frame) {
    if (best <= 0.0) {
        return 0.0;
    }

    // The absolute score says how much evidence we saw; separation says how
    // decisively this frame beat the alternatives. Keep inferred defaults lower.
    const double absolute = std::min(1.0, best / 5.0);
    const double separation = best <= 0.0 ? 0.0 : std::clamp((best - runner_up) / best, 0.0, 1.0);
    double confidence = 0.55 * absolute + 0.45 * separation;
    if (!explicit_frame) {
        confidence = std::min(confidence, 0.62);
    }
    return std::clamp(confidence, 0.0, 0.99);
}

} // namespace

QueryFrameResult recognize_frame(const Query& query) {
    const auto words = lexical_words(query.normalized);
    if (words.empty()) {
        return {};
    }

    const std::string text = phrase_text(words);
    std::unordered_map<QueryFrame, FrameEvidence> evidence;

    // Discover asks for a tool/command/way to accomplish something. "how to" is
    // only weak evidence because humans also use it for explanation or diagnosis.
    add_phrase_patterns(evidence, text, QueryFrame::Discover, {
        {"what tool", 4.2}, {"which tool", 4.2}, {"what command", 4.2},
        {"which command", 4.2}, {"what is the command", 5.2}, {"what s the command", 5.2},
        {"what is that command", 5.8}, {"what s that command", 5.8},
        {"what was that command", 5.8}, {"what is that tool", 5.8},
        {"what s that tool", 5.8}, {"what s that terminal thing", 5.8},
        {"what is that terminal thing", 5.8}, {"what was that terminal thing", 5.8},
        {"what can i use", 4.3}, {"what do i use", 4.3}, {"what should i use", 4.3},
        {"tool for", 3.6}, {"tool to", 3.6}, {"command for", 3.6}, {"command to", 3.8},
        {"program to", 3.5}, {"something for", 3.0}, {"need something", 2.6}, {"how to", 1.2},
        {"how do i", 1.2},
    });

    add_phrase_patterns(evidence, text, QueryFrame::Explain, {
        {"what is", 3.2}, {"what does", 3.6}, {"tell me about", 4.0},
        {"explain", 3.8}, {"meaning of", 3.8},
        {"how to use", 5.4}, {"how do i use", 5.4}, {"show me how to use", 5.8},
        {"usage of", 4.8},
        {"what flag", 5.2}, {"which flag", 5.2},
        {"what option", 5.2}, {"which option", 5.2},
        {"what subcommand", 5.2}, {"which subcommand", 5.2},
    });
    if ((contains_word(words, "what") || contains_word(words, "which")) &&
        (contains_word(words, "flag") || contains_word(words, "option") ||
         contains_word(words, "flags") || contains_word(words, "options"))) {
        add_evidence(evidence, QueryFrame::Explain, 5.2, true, "syntax option question");
    }
    if ((contains_word(words, "what") || contains_word(words, "which")) &&
        (contains_word(words, "subcommand") || contains_word(words, "subcommands"))) {
        add_evidence(evidence, QueryFrame::Explain, 5.2, true, "syntax subcommand question");
    }

    add_phrase_patterns(evidence, text, QueryFrame::Locate, {
        {"where is", 3.8}, {"where s", 3.8}, {"location of", 4.0}, {"path to", 4.0},
        {"which directory", 3.4}, {"which folder", 3.4},
    });

    // These phrases intentionally overpower the generic "where is"/"what is"
    // language. English question words are not reliable intent labels by themselves.
    add_phrase_patterns(evidence, text, QueryFrame::Inspect, {
        {"where do i see", 5.2}, {"where can i see", 5.2},
        {"where do i view", 5.2}, {"where can i view", 5.2},
        {"where do i monitor", 5.2}, {"what is using", 5.4}, {"what s using", 5.4},
        {"what is eating", 5.4}, {"what s eating", 5.4}, {"what is hogging", 5.4},
        {"what uses", 5.0}, {"who is using", 5.0}, {"show me", 3.2},
        {"who owns", 5.2}, {"who tf owns", 5.2}, {"what owns", 5.0},
        {"stole port", 4.8}, {"stolen port", 4.8},
        {"ate all the space", 5.0}, {"eating all the space", 5.0},
        {"ram hog", 4.8}, {"ram hogs", 4.8}, {"memory hog", 4.8},
    });

    add_phrase_patterns(evidence, text, QueryFrame::Modify, {
        {"where do i edit", 5.2}, {"where can i edit", 5.2},
        {"where do i change", 5.2}, {"where can i change", 5.2},
        {"where do i configure", 5.2}, {"how do i edit", 4.6},
        {"how do i configure", 4.6}, {"make a fresh branch", 4.8},
        {"make a new branch", 4.8}, {"create a branch", 4.8},
    });

    const bool entity_compare = entity_compare_shape(words, text);
    if (entity_compare) {
        add_phrase_patterns(evidence, text, QueryFrame::Compare, {
            {"difference between", 4.8}, {"differences between", 4.8},
            {"compare", 4.0}, {"comparing", 4.0}, {"versus", 4.0},
        });
    }

    add_phrase_patterns(evidence, text, QueryFrame::Diagnose, {
        {"not working", 3.6}, {"does not work", 3.6}, {"doesnt work", 3.6},
        {"can t", 3.2}, {"cant", 3.2}, {"won t", 3.2}, {"wont", 3.2},
        {"keeps failing", 3.8},
    });

    // Strong action words are useful even when the user didn't form a question.
    const auto add_word_set = [&](const QueryFrame frame, const double weight,
                                  const std::initializer_list<std::string_view> needles) {
        for (const auto needle : needles) {
            if (contains_word(words, needle)) {
                add_evidence(evidence, frame, weight, true, std::string(needle));
            }
        }
    };

    add_word_set(QueryFrame::Inspect, 2.8,
                 {"show", "list", "view", "inspect", "monitor", "watch", "status", "check", "see",
                  "owns", "owner", "hog", "hogs", "hogging", "ate", "eating"});
    add_word_set(QueryFrame::Modify, 3.1,
                 {"edit", "modify", "change", "configure", "manage", "settings", "set",
                  "create", "rename", "remove", "delete", "move", "switch", "enable",
                  "disable"});
    if (entity_compare) {
        add_word_set(QueryFrame::Compare, 3.6,
                     {"compare", "comparing", "difference", "differences", "vs", "versus"});
    }
    add_word_set(QueryFrame::Diagnose, 3.2,
                 {"why", "error", "failing", "failed", "broken", "issue", "problem", "refusing"});

    // "what is using" is inspection, not explanation. Likewise explicit action
    // language should beat generic question-form evidence when both are present.
    if (contains_phrase(text, "what is using") || contains_phrase(text, "what s using") ||
        contains_phrase(text, "what uses") || contains_phrase(text, "what is eating") ||
        contains_phrase(text, "what s eating") || contains_phrase(text, "what is hogging")) {
        evidence[QueryFrame::Explain].score *= 0.25;
    }
    if (evidence[QueryFrame::Discover].score >= 4.0) {
        evidence[QueryFrame::Explain].score *= 0.30;
    }
    if (evidence[QueryFrame::Modify].score >= 3.0) {
        evidence[QueryFrame::Locate].score *= 0.35;
        evidence[QueryFrame::Explain].score *= 0.50;
    }
    if (evidence[QueryFrame::Inspect].score >= 3.0) {
        evidence[QueryFrame::Locate].score *= 0.45;
        evidence[QueryFrame::Explain].score *= 0.45;
    }

    // Reuse the deterministic concept layer as a second-stage signal. It already
    // knows that noun-heavy state queries such as "network connections" imply a
    // safe read-only inspection action. Keep this inference deliberately weaker
    // than explicit human wording.
    const auto groups = concept_groups(query);
    const bool implicit_inspect = std::ranges::any_of(groups, [](const ConceptGroup& group) {
        return group.term == "inspect" && group.implicit;
    });
    if (implicit_inspect) {
        add_evidence(evidence, QueryFrame::Inspect, 2.0, false, "implicit inspect");
    }

    QueryFrame best_frame = QueryFrame::Unknown;
    FrameEvidence best;
    double runner_up = 0.0;

    for (const auto& [frame, frame_evidence] : evidence) {
        if (frame_evidence.score > best.score) {
            runner_up = best.score;
            best_frame = frame;
            best = frame_evidence;
        } else if (frame_evidence.score > runner_up) {
            runner_up = frame_evidence.score;
        }
    }

    if (best.score <= 0.0) {
        return QueryFrameResult{
            .frame = QueryFrame::Discover,
            .confidence = 0.35,
            .explicit_frame = false,
            .signals = {"default discovery"},
        };
    }

    // "how to" alone should not masquerade as high-confidence linguistic insight.
    // Discover remains the safe fallback, but its confidence stays modest.
    if (best_frame == QueryFrame::Discover && best.score <= 1.3) {
        return QueryFrameResult{
            .frame = QueryFrame::Discover,
            .confidence = 0.46,
            .explicit_frame = false,
            .signals = best.signals,
        };
    }

    return QueryFrameResult{
        .frame = best_frame,
        .confidence = confidence_from_score(best.score, runner_up, best.explicit_frame),
        .explicit_frame = best.explicit_frame,
        .signals = std::move(best.signals),
    };
}

} // namespace acclorite::query
