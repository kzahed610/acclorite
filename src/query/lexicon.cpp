#include "acclorite/query/lexicon.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

#include "acclorite/query/fuzzy.hpp"

namespace acclorite::query {
namespace {

struct VocabularyEntry {
    std::vector<std::string> alternatives;
    ConceptRole role{ConceptRole::Subject};
    double weight{1.0};
};

const std::unordered_map<std::string, VocabularyEntry>& vocabulary() {
    static const std::unordered_map<std::string, VocabularyEntry> groups{
        {"search", {{"search", "find", "locate", "match", "lookup", "matching"}, ConceptRole::Action, 1.60}},
        {"find", {{"find", "search", "locate", "lookup", "match"}, ConceptRole::Action, 1.60}},
        {"inspect", {{"inspect", "investigate", "show", "list", "view", "display", "status", "check", "examine", "see"}, ConceptRole::Action, 1.35}},
        {"show", {{"show", "inspect", "investigate", "list", "view", "display", "status", "check"}, ConceptRole::Action, 1.35}},
        {"list", {{"list", "show", "inspect", "view", "display", "enumerate"}, ConceptRole::Action, 1.30}},
        {"view", {{"view", "show", "inspect", "display", "list"}, ConceptRole::Action, 1.25}},
        {"check", {{"check", "inspect", "show", "status", "examine"}, ConceptRole::Action, 1.25}},
        {"edit", {{"edit", "editor", "modify", "change", "alter"}, ConceptRole::Action, 1.45}},
        {"manage", {{"manage", "management", "manager", "configure", "configuration", "settings", "control"}, ConceptRole::Action, 1.40}},
        {"configure", {{"configure", "configuration", "settings", "setup", "manage"}, ConceptRole::Action, 1.45}},
        {"text", {{"text", "line", "lines", "pattern", "patterns", "content", "regex", "txt"}, ConceptRole::Subject, 1.10}},
        {"disk", {{"disk", "storage", "drive", "filesystem", "space", "ssd", "hdd"}, ConceptRole::Subject, 1.20}},
        {"storage", {{"storage", "disk", "drive", "filesystem", "space", "ssd", "hdd"}, ConceptRole::Subject, 1.20}},
        {"usage", {{"usage", "utilization", "space", "size", "consumption", "consuming", "eating", "hogging", "using", "uses", "used"}, ConceptRole::Action, 1.00}},
        {"network", {{"network", "networking", "socket", "sockets", "tcp", "udp", "interface"}, ConceptRole::Subject, 1.20}},
        {"connections", {{"connections", "connection", "socket", "sockets", "session", "listen", "listening"}, ConceptRole::Subject, 1.10}},
        {"connection", {{"connection", "connections", "socket", "sockets", "session", "listen", "listening"}, ConceptRole::Subject, 1.10}},
        {"process", {{"process", "processes", "task", "tasks", "pid", "program"}, ConceptRole::Subject, 1.20}},
        {"processes", {{"processes", "process", "task", "tasks", "pid", "program"}, ConceptRole::Subject, 1.20}},
        {"monitor", {{"monitor", "monitoring", "watch", "resource", "stats", "statistics", "usage"}, ConceptRole::Action, 1.50}},
        {"monitoring", {{"monitoring", "monitor", "watch", "resource", "stats", "statistics", "usage"}, ConceptRole::Action, 1.50}},
        {"archive", {{"archive", "archives", "archiving", "archiver", "bundle", "packing", "tar", "zip"}, ConceptRole::Action, 1.55}},
        {"files", {{"files", "file", "directory", "directories", "folder", "folders", "filesystem"}, ConceptRole::Context, 0.65}},
        {"file", {{"file", "files", "directory", "folder", "path"}, ConceptRole::Context, 0.65}},
        {"duplicate", {{"duplicate", "duplicates", "identical", "copies", "deduplicate"}, ConceptRole::Action, 1.45}},
        {"duplicates", {{"duplicates", "duplicate", "identical", "copies", "deduplicate"}, ConceptRole::Action, 1.45}},
        {"compress", {{"compress", "compression", "compressed", "archive", "packing"}, ConceptRole::Action, 1.55}},
        {"extract", {{"extract", "extraction", "unpack", "decompress", "unarchive", "archive", "archiving"}, ConceptRole::Action, 1.55}},
        {"tar", {{"tar", "tar.gz", "tgz", "tarball"}, ConceptRole::Subject, 1.30}},
        {"memory", {{"memory", "ram", "rss", "swap"}, ConceptRole::Subject, 1.20}},
        {"ram", {{"ram", "memory", "rss", "swap"}, ConceptRole::Subject, 1.20}},
        {"cpu", {{"cpu", "processor", "processors", "processing"}, ConceptRole::Subject, 1.20}},
        {"port", {{"port", "ports", "socket", "sockets", "listen", "listening"}, ConceptRole::Subject, 1.20}},
        {"identify", {{"identify", "owner", "owns", "owned", "responsible"}, ConceptRole::Action, 1.45}},
        {"config", {{"config", "configuration", "settings", "conf"}, ConceptRole::Context, 0.80}},
        {"key", {{"key", "keys", "identity", "credential", "authentication"}, ConceptRole::Subject, 1.00}},
        {"compare", {{"compare", "comparison", "diff", "difference", "differences"}, ConceptRole::Action, 1.55}},
        {"folders", {{"folders", "folder", "directory", "directories", "filesystem"}, ConceptRole::Context, 0.70}},
        {"folder", {{"folder", "folders", "directory", "directories", "filesystem"}, ConceptRole::Context, 0.70}},
    };
    return groups;
}

std::string canonical_vocabulary_term(const std::string& input) {
    const auto& groups = vocabulary();
    if (groups.contains(input)) {
        return input;
    }

    // Exact aliases are cheap and deterministic. This also handles deliberate
    // shorthand such as "txt" without requiring fuzzy matching for tiny tokens.
    for (const auto& [canonical, entry] : groups) {
        if (std::ranges::find(entry.alternatives, input) != entry.alternatives.end()) {
            return canonical;
        }
    }

    // Unknown human tokens get one conservative fuzzy attempt against canonical
    // vocabulary words. This is typo recovery, not general semantic matching.
    if (input.size() < 4) {
        return {};
    }

    std::string best;
    double best_score = 0.0;
    for (const auto& [canonical, _] : groups) {
        const double score = fuzzy::similarity(input, canonical);
        if (score > best_score) {
            best = canonical;
            best_score = score;
        }
    }

    const double threshold = input.size() <= 5 ? 0.78 : 0.82;
    return best_score >= threshold ? best : std::string{};
}

} // namespace

bool is_stopword(const std::string_view token) {
    static const std::unordered_set<std::string> stopwords{
        "a", "all", "an", "and", "are", "bro", "can", "do", "does", "for", "how",
        "has", "have", "having", "i", "in", "is", "it", "lots", "many", "me", "much",
        "multiple", "my", "of", "on", "please", "that", "the", "thing", "this", "to",
        "two", "use", "used", "uses", "using", "what", "where", "which", "why", "with", "between", "tf"
    };
    return stopwords.contains(std::string(token));
}

std::vector<std::string> meaningful_terms(const Query& query) {
    std::vector<std::string> terms;
    std::unordered_set<std::string> seen;

    for (const auto& token : query.tokens) {
        const bool numeric = !token.empty() && std::ranges::all_of(token, [](const unsigned char ch) {
            return std::isdigit(ch);
        });
        // `using`/`uses` are filler in "what tool can I use", but they carry the
        // actual consumption/ownership relation in inspection questions such as
        // "what is using disk space" and "which process is using port 8080".
        const bool relational_usage =
            query.frame.frame == QueryFrame::Inspect &&
            (token == "using" || token == "uses" || token == "used");
        if (token.size() < 2 || token.starts_with('-') || numeric ||
            (is_stopword(token) && !relational_usage)) {
            continue;
        }
        if (seen.insert(token).second) {
            terms.push_back(token);
        }
        if (terms.size() == 6) {
            break;
        }
    }

    if (terms.empty() && !query.normalized.empty() && !query.normalized.starts_with('-')) {
        terms.push_back(query.normalized);
    }

    return terms;
}

std::vector<ConceptGroup> concept_groups(const Query& query) {
    std::vector<ConceptGroup> groups;
    std::unordered_set<std::string> seen;

    for (const auto& original_term : meaningful_terms(query)) {
        const std::string canonical = canonical_vocabulary_term(original_term);
        if (canonical.empty()) {
            if (seen.insert(original_term).second) {
                groups.push_back(ConceptGroup{
                    .term = original_term,
                    .alternatives = {original_term},
                    .role = ConceptRole::Subject,
                    .weight = 1.0,
                    .implicit = false,
                });
            }
            continue;
        }

        if (!seen.insert(canonical).second) {
            continue;
        }

        const auto& entry = vocabulary().at(canonical);
        groups.push_back(ConceptGroup{
            .term = canonical,
            .alternatives = entry.alternatives,
            .role = entry.role,
            .weight = entry.weight,
            .implicit = false,
        });
    }

    // A noun-heavy state query such as "network connections" usually means
    // "show/inspect network connections" unless the user explicitly supplied a
    // mutating action. Defaulting to read-only inspection is both useful and safe.
    const bool has_action = std::ranges::any_of(groups, [](const ConceptGroup& group) {
        return group.role == ConceptRole::Action;
    });
    const bool frame_allows_inspect_default =
        query.frame.frame == QueryFrame::Unknown ||
        query.frame.frame == QueryFrame::Discover ||
        query.frame.frame == QueryFrame::Inspect;
    const bool has_state_subject = std::ranges::any_of(groups, [](const ConceptGroup& group) {
        static const std::unordered_set<std::string> state_subjects{
            "network", "connections", "connection", "process", "processes",
            "memory", "ram", "cpu", "port", "disk", "storage"
        };
        return state_subjects.contains(group.term);
    });
    if (frame_allows_inspect_default && !has_action && has_state_subject && groups.size() >= 2) {
        groups.push_back(ConceptGroup{
            .term = "inspect",
            .alternatives = {"inspect", "investigate", "show", "list", "view", "display",
                             "status", "check", "examine"},
            .role = ConceptRole::Action,
            .weight = 1.05,
            .implicit = true,
        });
    }

    // "duplicate" on its own is overwhelmingly useful as a file-discovery intent
    // in this product. Add a deliberately low-weight file context so dedicated
    // duplicate-file tools beat unrelated uses of the word (for example gettext
    // message deduplication) without pretending the user explicitly typed "files".
    const bool has_duplicate_action = std::ranges::any_of(groups, [](const ConceptGroup& group) {
        return group.role == ConceptRole::Action && group.term == "duplicate";
    });
    const bool has_explicit_context = std::ranges::any_of(groups, [](const ConceptGroup& group) {
        return !group.implicit && (group.role == ConceptRole::Context || group.role == ConceptRole::Subject);
    });
    if (has_duplicate_action && !has_explicit_context) {
        groups.push_back(ConceptGroup{
            .term = "files",
            .alternatives = {"files", "file", "directory", "directories", "folder", "folders", "path"},
            .role = ConceptRole::Context,
            .weight = 0.55,
            .implicit = true,
        });
    }

    // Bare `search text` / `find text` is normally a content-search request, not a
    // request to discover a desktop indexing engine. Infer a low-weight file/content
    // context only when the user supplied no other domain term. Explicit phrases
    // such as `full text search` or `search text database` therefore remain free to
    // describe a different search domain.
    const bool has_search_action = std::ranges::any_of(groups, [](const ConceptGroup& group) {
        return !group.implicit && group.role == ConceptRole::Action &&
               (group.term == "search" || group.term == "find");
    });
    const bool has_text_subject = std::ranges::any_of(groups, [](const ConceptGroup& group) {
        return !group.implicit && group.term == "text";
    });
    const bool has_other_explicit_domain = std::ranges::any_of(groups, [](const ConceptGroup& group) {
        if (group.implicit) {
            return false;
        }
        return group.term != "search" && group.term != "find" && group.term != "text";
    });
    const bool frame_allows_content_default =
        query.frame.frame == QueryFrame::Unknown || query.frame.frame == QueryFrame::Discover;
    if (frame_allows_content_default && has_search_action && has_text_subject &&
        !has_other_explicit_domain) {
        groups.push_back(ConceptGroup{
            .term = "files",
            .alternatives = {"files", "file", "directory", "directories", "folder", "folders",
                             "filesystem", "path"},
            .role = ConceptRole::Context,
            .weight = 0.50,
            .implicit = true,
        });
    }

    // Compression of a directory normally implies creating an archive/container,
    // not performing an arbitrary domain-specific "compression" operation (for
    // example compressing hwloc topology XML). Keep this as a low-weight inferred
    // archive intent so general archive tools gain evidence without pretending the
    // user explicitly typed "archive".
    const bool has_explicit_compress = std::ranges::any_of(groups, [](const ConceptGroup& group) {
        return !group.implicit && group.term == "compress";
    });
    const bool has_directory_context = std::ranges::any_of(groups, [](const ConceptGroup& group) {
        return !group.implicit && (group.term == "files" || group.term == "file" ||
                                   group.term == "folder" || group.term == "folders");
    });
    const bool has_archive_action = std::ranges::any_of(groups, [](const ConceptGroup& group) {
        return group.term == "archive";
    });
    if (has_explicit_compress && has_directory_context && !has_archive_action) {
        groups.push_back(ConceptGroup{
            .term = "archive",
            .alternatives = {"archive", "archives", "archiving", "bundle", "packing", "tar", "zip"},
            .role = ConceptRole::Action,
            .weight = 0.85,
            .implicit = true,
        });
    }

    // Relation-aware inspection. Human phrasing such as "what is using port 8080"
    // asks for the owning process even though the word `process` is absent. Likewise
    // "what is eating RAM" normally asks which process consumes memory, not where
    // physical memory ranges live. These are deterministic relation inferences.
    const auto raw_has = [&](const std::initializer_list<std::string_view> needles) {
        return std::ranges::any_of(query.tokens, [&](const std::string& token) {
            return std::ranges::find(needles, std::string_view(token)) != needles.end();
        });
    };
    const auto has_term = [&](const std::initializer_list<std::string_view> needles) {
        return std::ranges::any_of(groups, [&](const ConceptGroup& group) {
            return std::ranges::find(needles, std::string_view(group.term)) != needles.end();
        });
    };
    const auto add_implicit = [&](std::string term, std::vector<std::string> alternatives,
                                  const ConceptRole role, const double weight) {
        if (std::ranges::any_of(groups, [&](const ConceptGroup& group) { return group.term == term; })) {
            return;
        }
        groups.push_back(ConceptGroup{
            .term = std::move(term),
            .alternatives = std::move(alternatives),
            .role = role,
            .weight = weight,
            .implicit = true,
        });
    };

    if (query.frame.frame == QueryFrame::Inspect && has_term({"port"}) &&
        raw_has({"using", "uses", "owns", "owner", "occupying", "listening"})) {
        add_implicit("process", {"process", "processes", "pid", "program", "application"},
                     ConceptRole::Subject, 1.15);
        add_implicit("identify", {"identify", "owner", "owns", "find", "show"},
                     ConceptRole::Action, 1.25);
    }

    if (query.frame.frame == QueryFrame::Inspect && has_term({"memory", "ram"}) &&
        raw_has({"using", "uses", "eating", "hogging", "consuming", "consumes"})) {
        add_implicit("process", {"process", "processes", "pid", "program", "application"},
                     ConceptRole::Subject, 1.15);
        add_implicit("monitor", {"monitor", "monitoring", "watch", "resource", "stats", "usage"},
                     ConceptRole::Action, 1.05);
    }

    return groups;
}

std::vector<std::string> retrieval_terms(const Query& query, const std::size_t limit) {
    std::vector<std::string> terms;
    std::unordered_set<std::string> seen;

    for (const auto& group : concept_groups(query)) {
        // Keep retrieval intentionally narrower than scoring. We want useful recall
        // without turning apropos / FTS into a firehose for every synonym we know.
        std::size_t added_from_group = 0;
        for (const auto& term : group.alternatives) {
            if (seen.insert(term).second) {
                terms.push_back(term);
                ++added_from_group;
            }
            if (terms.size() >= limit || added_from_group == 4) {
                break;
            }
        }
        if (terms.size() >= limit) {
            break;
        }
    }

    return terms;
}

} // namespace acclorite::query
