#include "acclorite/query/lexicon.hpp"

#include <algorithm>
#include <array>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

namespace acclorite::query {
namespace {

using Alternatives = std::vector<std::string>;

const std::unordered_map<std::string, Alternatives>& vocabulary() {
    static const std::unordered_map<std::string, Alternatives> groups{
        {"search", {"search", "find", "locate", "match", "matching"}},
        {"find", {"find", "search", "locate", "lookup", "match"}},
        {"text", {"text", "line", "lines", "pattern", "patterns", "content", "regex"}},
        {"disk", {"disk", "storage", "drive", "filesystem", "space", "ssd", "hdd"}},
        {"storage", {"storage", "disk", "drive", "filesystem", "space", "ssd", "hdd"}},
        {"usage", {"usage", "utilization", "space", "size", "consumption"}},
        {"network", {"network", "networking", "socket", "sockets", "tcp", "udp", "interface"}},
        {"connections", {"connections", "connection", "socket", "sockets", "session", "listen", "listening"}},
        {"connection", {"connection", "connections", "socket", "sockets", "session", "listen", "listening"}},
        {"process", {"process", "processes", "task", "tasks", "pid", "program"}},
        {"processes", {"processes", "process", "task", "tasks", "pid", "program"}},
        {"monitor", {"monitor", "monitoring", "watch", "resource", "stats", "statistics", "usage"}},
        {"monitoring", {"monitoring", "monitor", "watch", "resource", "stats", "statistics", "usage"}},
        {"archive", {"archive", "archives", "archiving", "archiver", "bundle", "packing"}},
        {"files", {"files", "file", "directory", "directories", "folder", "folders", "filesystem"}},
        {"file", {"file", "files", "directory", "folder", "path"}},
        {"duplicate", {"duplicate", "duplicates", "identical", "copies", "deduplicate"}},
        {"duplicates", {"duplicates", "duplicate", "identical", "copies", "deduplicate"}},
        {"compress", {"compress", "compression", "compressed", "archive", "packing"}},
        {"extract", {"extract", "extraction", "unpack", "decompress", "unarchive"}},
        {"memory", {"memory", "ram", "rss", "swap"}},
        {"ram", {"ram", "memory", "rss", "swap"}},
        {"cpu", {"cpu", "processor", "processors", "processing"}},
        {"port", {"port", "ports", "socket", "sockets", "listen", "listening"}},
        {"compare", {"compare", "comparison", "diff", "difference", "differences"}},
        {"folders", {"folders", "folder", "directory", "directories", "filesystem"}},
        {"folder", {"folder", "folders", "directory", "directories", "filesystem"}},
    };
    return groups;
}

} // namespace

bool is_stopword(const std::string_view token) {
    static const std::unordered_set<std::string> stopwords{
        "a", "an", "and", "are", "bro", "can", "do", "does", "for", "how",
        "i", "in", "is", "it", "me", "my", "of", "on", "please", "that",
        "the", "thing", "this", "to", "use", "what", "which", "with"
    };
    return stopwords.contains(std::string(token));
}

std::vector<std::string> meaningful_terms(const Query& query) {
    std::vector<std::string> terms;
    std::unordered_set<std::string> seen;

    for (const auto& token : query.tokens) {
        if (token.size() < 2 || token.starts_with('-') || is_stopword(token)) {
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

    for (const auto& term : meaningful_terms(query)) {
        auto it = vocabulary().find(term);
        if (it == vocabulary().end()) {
            groups.push_back(ConceptGroup{.term = term, .alternatives = {term}});
            continue;
        }

        groups.push_back(ConceptGroup{.term = term, .alternatives = it->second});
    }

    return groups;
}

std::vector<std::string> retrieval_terms(const Query& query, const std::size_t limit) {
    std::vector<std::string> terms;
    std::unordered_set<std::string> seen;

    for (const auto& group : concept_groups(query)) {
        // Keep retrieval intentionally narrower than scoring. We want useful recall
        // without turning apropos into a firehose for every synonym we know.
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
