#include "acclorite/ranking/preference.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "acclorite/query/lexicon.hpp"

namespace acclorite::ranking {
namespace {

bool has_concept(const Query& query, const std::initializer_list<std::string_view> terms) {
    const auto groups = query::concept_groups(query);
    return std::ranges::any_of(groups, [&](const query::ConceptGroup& group) {
        return std::ranges::find(terms, std::string_view(group.term)) != terms.end();
    });
}

bool has_explicit_concept(
    const Query& query,
    const std::initializer_list<std::string_view> terms
) {
    const auto groups = query::concept_groups(query);
    return std::ranges::any_of(groups, [&](const query::ConceptGroup& group) {
        return !group.implicit &&
            std::ranges::find(terms, std::string_view(group.term)) != terms.end();
    });
}


std::string lower(std::string_view input) {
    std::string out(input);
    std::ranges::transform(out, out.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

std::vector<std::string> words(std::string_view input) {
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

bool contains_word(const std::vector<std::string>& haystack, const std::string_view needle) {
    return std::ranges::find(haystack, needle) != haystack.end();
}

bool contains_any_word(
    const std::vector<std::string>& haystack,
    const std::initializer_list<std::string_view> needles
) {
    return std::ranges::any_of(needles, [&](const std::string_view needle) {
        return contains_word(haystack, needle);
    });
}

bool query_explicitly_mentions(
    const Query& query,
    const std::initializer_list<std::string_view> terms
) {
    return std::ranges::any_of(query.tokens, [&](const std::string& token) {
        return std::ranges::find(terms, std::string_view(token)) != terms.end();
    });
}

struct DomainSpecialization {
    std::string_view id;
    std::string_view label;
    double semantic_factor;
    double utility_factor;
};

struct SemanticPolicy {
    double front_door_floor{0.0};
    std::vector<DomainSpecialization> specializations;
};

SemanticPolicy semantic_policy(
    const Query& query,
    const Candidate& candidate
) {
    std::string summary = lower(candidate.summary);
    for (const auto& description : candidate.descriptive_evidence) {
        if (!description.empty()) {
            summary.push_back(' ');
            summary.append(lower(description));
        }
    }
    const std::string command = lower(candidate.command);
    const std::string package = lower(candidate.package);

    // Entity lookup is never a generic discovery request. If the user explicitly
    // named this command/package, its specialization is part of the requested
    // entity rather than an unwanted domain constraint.
    const bool explicitly_named = std::ranges::find(query.tokens, command) != query.tokens.end() ||
        (!package.empty() && std::ranges::find(query.tokens, package) != query.tokens.end());
    if (explicitly_named) {
        return SemanticPolicy{};
    }
    const auto summary_words = words(summary);
    const auto command_words = words(command);
    const auto package_words = words(package);

    // Positive role evidence: documentation for general archive front doors is
    // often intentionally terse (for example, "archiving utility") and may not
    // repeat the obvious input object "files/directories". For an explicit
    // folder-compression task, recognize that declared archive-creator role as
    // evidence that the tool accepts ordinary file/directory input. This is a
    // semantic floor, not a command whitelist. Narrower filesystem/backup,
    // comparison, development-library, and other constraints are applied *after*
    // the floor and can still demote an over-specialized candidate.
    const bool folder_compression_requested =
        has_explicit_concept(query, {"compress"}) &&
        has_explicit_concept(query, {"files", "file", "folder", "folders"});
    const bool archive_front_door =
        contains_word(summary_words, "archiver") ||
        summary.find("archiving utility") != std::string::npos ||
        summary.find("archive utility") != std::string::npos ||
        summary.find("archiving tool") != std::string::npos ||
        summary.find("archive tool") != std::string::npos ||
        summary.find("archive manager") != std::string::npos ||
        (contains_any_word(summary_words, {"archive", "archives"}) &&
         contains_any_word(summary_words, {"create", "creating", "compress", "compressed",
                                           "compression", "pack", "packing", "manager",
                                           "utility", "tool"}));

    SemanticPolicy policy;
    if (folder_compression_requested && archive_front_door) {
        // Just above the strong semantic tier. The candidate must still survive
        // all query-relative specialization constraints below.
        policy.front_door_floor = 0.845;
    }

    auto& result = policy.specializations;
    const auto add = [&](const bool specialized, const bool requested, DomainSpecialization rule) {
        if (specialized && !requested) {
            result.push_back(rule);
        }
    };

    const bool pdf_specialized =
        contains_word(summary_words, "pdf") || contains_word(package_words, "pdf") ||
        command.starts_with("pdf") || command.find("pdf") != std::string::npos;
    const bool pdf_requested = query_explicitly_mentions(query, {"pdf", "pdfs"});
    add(pdf_specialized, pdf_requested,
        {"pdf-domain", "unrequested PDF specialization", 0.84, 0.96});

    const bool archive_specialized =
        contains_any_word(summary_words, {"compressed", "compression", "archive", "archives", "zip", "gzip", "xz", "lzma", "bzip2"}) ||
        contains_any_word(package_words, {"zip", "gzip", "xz", "lzma", "bzip2"}) ||
        command.starts_with("zip") || command.starts_with("xz") || command.starts_with("lz") ||
        command.starts_with("bz") || command.starts_with("zstd");
    const bool archive_requested =
        has_concept(query, {"compress", "archive", "extract", "tar"}) ||
        query_explicitly_mentions(query, {"zip", "gzip", "xz", "lzma", "bzip", "bzip2", "zstd", "tar"});
    add(archive_specialized, archive_requested,
        {"archive-domain", "unrequested compressed/archive specialization", 0.90, 0.97});

    const bool vcs_specialized =
        contains_any_word(summary_words, {"git", "repository", "repositories", "commit", "commits"}) ||
        command.starts_with("git-") || package.starts_with("git-");
    const bool vcs_requested = query_explicitly_mentions(
        query, {"git", "repo", "repository", "repositories", "commit", "commits", "branch", "branches", "vcs"}
    );
    add(vcs_specialized, vcs_requested,
        {"vcs-domain", "unrequested version-control specialization", 0.90, 0.97});

    const bool fulltext_specialized =
        summary.find("full-text search") != std::string::npos ||
        summary.find("full text search") != std::string::npos ||
        summary.find("search engine") != std::string::npos ||
        contains_any_word(summary_words, {"indexer", "indexing"});
    const bool fulltext_requested = query_explicitly_mentions(
        query, {"full", "index", "indexer", "indexing", "database", "databases", "document", "documents"}
    );
    add(fulltext_specialized, fulltext_requested,
        {"fulltext-domain", "unrequested indexing/search-engine specialization", 0.88, 0.97});

    const bool catalog_specialized =
        contains_any_word(summary_words, {"gettext", "translation", "translations", "catalog", "catalogs", "localization"}) ||
        summary.find("message catalog") != std::string::npos;
    const bool catalog_requested = query_explicitly_mentions(
        query, {"gettext", "translation", "translations", "catalog", "catalogs", "message", "messages", "localization"}
    );
    add(catalog_specialized, catalog_requested,
        {"catalog-domain", "unrequested translation/catalog specialization", 0.88, 0.97});

    // Benchmark-driven subsystem constraints. These are not command-name bans:
    // they detect metadata that narrows a generic request to a specific technical
    // domain. The penalty disappears whenever the human explicitly names that
    // domain (or explicitly names the candidate itself, handled above).
    const bool netlink_specialized =
        contains_any_word(summary_words, {"netlink", "libnl"}) ||
        package.starts_with("libnl") || command.starts_with("nl-");
    const bool netlink_requested = query_explicitly_mentions(query, {"netlink", "libnl"});
    add(netlink_specialized, netlink_requested,
        {"netlink-domain", "unrequested netlink specialization", 0.82, 0.95});

    const bool topology_specialized =
        contains_any_word(summary_words, {"topology", "topologies", "hwloc"}) ||
        package == "hwloc" || command.starts_with("hwloc-");
    const bool topology_requested = query_explicitly_mentions(
        query, {"topology", "topologies", "hwloc", "numa"}
    );
    add(topology_specialized, topology_requested,
        {"topology-domain", "unrequested hardware-topology specialization", 0.78, 0.94});

    const bool font_specialized =
        contains_any_word(summary_words, {"font", "fonts", "fontforge", "glyph", "glyphs", "typeface", "typefaces"}) ||
        contains_any_word(package_words, {"fontforge", "fonttools"});
    const bool font_requested = query_explicitly_mentions(
        query, {"font", "fonts", "glyph", "glyphs", "typeface", "typefaces"}
    );
    add(font_specialized, font_requested,
        {"font-domain", "unrequested font specialization", 0.82, 0.96});

    const bool audio_specialized =
        contains_any_word(summary_words, {"audio", "sound", "sndfile", "waveform", "music"}) ||
        package.find("sndfile") != std::string::npos;
    const bool audio_requested = query_explicitly_mentions(
        query, {"audio", "sound", "music", "wave", "wav", "flac", "mp3"}
    );
    add(audio_specialized, audio_requested,
        {"audio-domain", "unrequested audio specialization", 0.84, 0.96});

    const bool filesystem_format_specialized =
        contains_any_word(summary_words, {"exfat", "ntfs", "ext4", "xfs", "btrfs"}) ||
        contains_any_word(command_words, {"exfat", "ntfs", "ext4", "xfs", "btrfs"}) ||
        contains_any_word(package_words, {"exfat", "ntfs", "ext4", "xfs", "btrfs"});
    const bool filesystem_format_requested = query_explicitly_mentions(
        query, {"exfat", "ntfs", "ext4", "xfs", "btrfs"}
    );
    add(filesystem_format_specialized, filesystem_format_requested,
        {"filesystem-format-domain", "unrequested filesystem-format specialization", 0.80, 0.95});

    // Tool-role constraints discovered by the first benchmark loop. A package may
    // describe the requested operation perfectly while still not be a sensible
    // user-facing front door for that operation. Keep this query-relative: an
    // explicit request for the narrower role removes the penalty.
    const bool backup_specialized =
        contains_any_word(summary_words, {"backup", "backups", "restore", "restoration",
                                          "deployment", "deploy", "clone", "cloning",
                                          "snapshot", "snapshots"}) ||
        summary.find("disk image") != std::string::npos ||
        summary.find("file-system backup") != std::string::npos ||
        summary.find("filesystem backup") != std::string::npos;
    const bool backup_requested = query_explicitly_mentions(
        query, {"backup", "backups", "restore", "restoration", "deploy", "deployment",
                "clone", "cloning", "snapshot", "snapshots", "image", "imaging",
                "filesystem", "filesystems", "partition", "partitions"}
    );
    add(backup_specialized, backup_requested,
        {"backup-role", "unrequested backup/deployment specialization", 0.80, 0.94});

    // A filesystem/partition-level archiver can lexically match ordinary folder
    // compression while operating at a much broader storage-management scope.
    // Treat that scope as a query-relative role constraint even when package
    // metadata does not literally contain the words backup/deployment.
    const bool filesystem_scope_specialized =
        (contains_any_word(summary_words, {"filesystem", "filesystems", "partition", "partitions"}) ||
         summary.find("file-system") != std::string::npos) &&
        (contains_any_word(summary_words, {"archive", "archives", "archiver", "compressed",
                                           "compression", "compress", "save", "image",
                                           "backup", "restore", "snapshot"}));
    const bool filesystem_scope_requested = query_explicitly_mentions(
        query, {"filesystem", "filesystems", "partition", "partitions"}
    );
    add(filesystem_scope_specialized, filesystem_scope_requested,
        {"filesystem-scope-role", "unrequested filesystem/partition scope", 0.78, 0.94});

    // Operation-direction guardrail. Archive-aware inspection/comparison tools can
    // mention files, directories, archives and recursive unpacking, which gives
    // them excellent lexical coverage for a request such as "compress a folder"
    // even though their actual front-door operation is comparison/analysis rather
    // than archive creation or extraction. Only apply this when the user explicitly
    // asked for a transforming archive action; explicit compare/inspect intent
    // removes the constraint.
    const bool comparison_role_specialized =
        contains_any_word(summary_words, {"compare", "compares", "compared", "comparing",
                                          "comparison", "comparisons", "difference",
                                          "differences", "diff", "diffs"}) ||
        summary.find("in-depth comparison") != std::string::npos ||
        summary.find("deep comparison") != std::string::npos;
    const bool archive_transform_requested =
        has_explicit_concept(query, {"compress", "extract"}) ||
        query_explicitly_mentions(query, {"pack", "package", "unpack", "decompress",
                                          "untar", "unarchive"});
    const bool comparison_requested =
        has_explicit_concept(query, {"compare", "inspect", "show", "list", "view", "check"}) ||
        query_explicitly_mentions(query, {"compare", "comparison", "diff", "difference",
                                          "differences", "analyze", "analyse", "inspect"});
    add(comparison_role_specialized && archive_transform_requested, comparison_requested,
        {"operation-comparison-role", "comparison/analysis role conflicts with requested archive transformation",
         0.72, 0.94});

    const bool development_library_specialized =
        package.starts_with("haskell-") ||
        contains_any_word(summary_words, {"library", "libraries", "bindings", "sdk"}) ||
        summary.find("development files") != std::string::npos ||
        summary.find("programming interface") != std::string::npos;
    const bool development_library_requested = query_explicitly_mentions(
        query, {"haskell", "python", "library", "libraries", "module", "modules",
                "bindings", "sdk", "api", "programming", "developer", "development"}
    );
    add(development_library_specialized, development_library_requested,
        {"development-library-role", "unrequested development-library specialization", 0.82, 0.94});

    return policy;
}

SemanticFitAssessment assess_semantic_fit_with_policy(
    const Candidate& candidate,
    const SemanticPolicy& policy
) {
    const double raw_fit = effective_semantic_fit(candidate);
    SemanticFitAssessment assessment{
        .raw_fit = raw_fit,
        .effective_fit = raw_fit,
        .adjustments = {},
    };

    if (policy.front_door_floor > assessment.effective_fit) {
        const double before = assessment.effective_fit;
        assessment.effective_fit = policy.front_door_floor;
        assessment.adjustments.push_back(RankingAdjustment{
            .id = "archive-front-door-role",
            .label = "generic archive front-door role satisfies folder-compression task",
            .kind = AdjustmentKind::Floor,
            .value = policy.front_door_floor,
            .before = before,
            .after = assessment.effective_fit,
        });
    }

    for (const auto& specialization : policy.specializations) {
        const double before = assessment.effective_fit;
        assessment.effective_fit *= specialization.semantic_factor;
        assessment.adjustments.push_back(RankingAdjustment{
            .id = std::string(specialization.id),
            .label = std::string(specialization.label),
            .kind = AdjustmentKind::Multiply,
            .value = specialization.semantic_factor,
            .before = before,
            .after = assessment.effective_fit,
        });
    }
    assessment.effective_fit = std::clamp(assessment.effective_fit, 0.0, 1.0);
    return assessment;
}

double canonical_utility_prior(const std::string_view command) {
    static constexpr std::array<std::string_view, 23> primary{
        "ss", "rg", "grep", "find", "fd", "du", "df", "fuser", "lsof",
        "btop", "htop", "tar", "zip", "unzip", "diff", "meld", "rsync",
        "fzf", "zoxide", "ncdu", "duf", "dust", "ark",
    };
    static constexpr std::array<std::string_view, 12> secondary{
        "cpio", "free", "ps", "ip", "networkctl", "systemctl", "journalctl",
        "top", "watch", "hardlink", "rdfind", "fdupes",
    };

    if (std::ranges::find(primary, command) != primary.end()) {
        return 0.022;
    }
    if (std::ranges::find(secondary, command) != secondary.end()) {
        return 0.012;
    }
    return 0.0;
}

int evidence_count(const std::string_view sources) {
    // Count only provenance that independently carries relevance evidence.
    // pkgfile is package -> executable/capability metadata. It is useful, but it
    // does not independently corroborate that the executable matches the query.
    int count = 0;
    std::size_t start = 0;
    while (start <= sources.size()) {
        const auto end = sources.find('+', start);
        const auto token = sources.substr(
            start,
            end == std::string_view::npos ? sources.size() - start : end - start
        );
        if (!token.empty() && token != "pkgfile") {
            ++count;
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return count;
}

bool has_direct_command_evidence(const Candidate& candidate) {
    return source_contains(candidate.source, "man") ||
           source_contains(candidate.source, "desktop");
}

double package_projection_factor(const Candidate& candidate) {
    if (!source_contains(candidate.source, "pkgfile") || candidate.provided_commands.size() < 2) {
        return 1.0;
    }

    const std::size_t fanout = candidate.provided_commands.size();
    const bool mapped_binary = !candidate.package.empty() && candidate.command != candidate.package;
    const bool direct_evidence = has_direct_command_evidence(candidate);

    if (fanout >= 8) {
        if (direct_evidence) {
            return 0.985;
        }
        return mapped_binary ? 0.86 : 0.94;
    }
    if (fanout >= 4) {
        if (direct_evidence) {
            return 0.99;
        }
        return mapped_binary ? 0.90 : 0.96;
    }

    if (mapped_binary && !direct_evidence) {
        return 0.97;
    }
    return 1.0;
}

void add_adjustment(
    RankingBreakdown& breakdown,
    const std::string_view id,
    const std::string_view label,
    const double amount
) {
    if (amount == 0.0) {
        return;
    }
    const double before = breakdown.final_utility;
    breakdown.final_utility += amount;
    breakdown.adjustments.push_back(RankingAdjustment{
        .id = std::string(id),
        .label = std::string(label),
        .kind = AdjustmentKind::Add,
        .value = amount,
        .before = before,
        .after = breakdown.final_utility,
    });
}

void multiply_adjustment(
    RankingBreakdown& breakdown,
    const std::string_view id,
    const std::string_view label,
    const double factor
) {
    if (factor == 1.0) {
        return;
    }
    const double before = breakdown.final_utility;
    breakdown.final_utility *= factor;
    breakdown.adjustments.push_back(RankingAdjustment{
        .id = std::string(id),
        .label = std::string(label),
        .kind = AdjustmentKind::Multiply,
        .value = factor,
        .before = before,
        .after = breakdown.final_utility,
    });
}

} // namespace

double effective_semantic_fit(const Candidate& candidate) {
    const double raw = candidate.semantic_fit > 0.0
        ? candidate.semantic_fit
        : std::clamp(candidate.score, 0.0, 1.0);
    return std::clamp(raw * package_projection_factor(candidate), 0.0, 1.0);
}

SemanticFitAssessment assess_semantic_fit(const Query& query, const Candidate& candidate) {
    return assess_semantic_fit_with_policy(candidate, semantic_policy(query, candidate));
}

double effective_semantic_fit(const Query& query, const Candidate& candidate) {
    return assess_semantic_fit(query, candidate).effective_fit;
}

int semantic_fit_tier(const double fit) {
    // The semantic formula has useful natural bands: a full canonical description
    // match lands around 0.887, while synonym-heavy full matches cluster around
    // 0.84-0.86. Keep those categories distinct, then let recommendation utility
    // decide within a category.
    if (fit >= 0.97) return 4;  // exact / explicitly named target
    if (fit >= 0.88) return 3;  // precise canonical intent match
    if (fit >= 0.84) return 2;  // strong but synonym/indirect intent match
    if (fit >= 0.70) return 1;  // plausible partial/weak semantic match
    return 0;
}

const char* semantic_fit_tier_name(const int tier) {
    switch (tier) {
        case 4: return "exact";
        case 3: return "precise";
        case 2: return "strong";
        case 1: return "plausible";
        default: return "weak";
    }
}

RankingBreakdown explain_preference_prior(const Query& query, const Candidate& candidate, double score) {
    // Specialization analysis is intentionally computed once per candidate. It
    // scans merged descriptive evidence and used to run twice here (semantic fit
    // and utility adjustment) in addition to repeated sort-comparator calls.
    const auto policy = semantic_policy(query, candidate);
    const auto semantic_assessment = assess_semantic_fit_with_policy(candidate, policy);
    const double semantic_fit = semantic_assessment.effective_fit;
    const int fit_tier = semantic_fit_tier(semantic_fit);
    RankingBreakdown breakdown{
        .raw_semantic_fit = semantic_assessment.raw_fit,
        .semantic_fit = semantic_fit,
        .semantic_tier = semantic_fit_tier_name(fit_tier),
        .semantic_adjustments = semantic_assessment.adjustments,
        .base_score = std::clamp(score, 0.0, 1.0),
        .base_utility = score,
        .source_evidence = candidate.evidence_trace,
        .base_merges = candidate.base_merge_trace,
        .adjustments = {},
        .final_utility = score,
        .final_score = std::clamp(score, 0.0, 1.0),
    };

    if (score <= 0.0) {
        return breakdown;
    }

    const bool path_only = candidate.source == "path" ||
        (candidate.summary.empty() || candidate.summary == "Executable available in PATH");
    if (path_only && breakdown.final_utility < 0.999) {
        multiply_adjustment(
            breakdown,
            "path-only-evidence",
            "PATH-only evidence confidence",
            0.90
        );
    }

    if (source_contains(candidate.source, "man")) {
        add_adjustment(breakdown, "man-evidence", "direct man-page evidence", 0.012);
    }

    const int sources = evidence_count(candidate.source);
    if (sources > 1) {
        add_adjustment(
            breakdown,
            "multi-source-confidence",
            "independent evidence sources",
            std::min(0.012, 0.004 * static_cast<double>(sources - 1))
        );
    }

    if (candidate.installed && breakdown.final_utility >= 0.45) {
        add_adjustment(breakdown, "installed", "installed / zero setup friction", 0.018);
    }

    multiply_adjustment(
        breakdown,
        "package-projection",
        "package description specificity",
        package_projection_factor(candidate)
    );

    for (const auto& specialization : policy.specializations) {
        multiply_adjustment(
            breakdown,
            specialization.id,
            specialization.label,
            specialization.utility_factor
        );
    }

    if (breakdown.final_utility >= 0.45) {
        add_adjustment(
            breakdown,
            "canonical-front-door",
            "established general-purpose utility prior",
            canonical_utility_prior(candidate.command)
        );
    }

    const double unclamped = breakdown.final_utility;
    breakdown.final_score = std::clamp(unclamped, 0.0, 1.0);
    if (breakdown.final_score != unclamped) {
        breakdown.adjustments.push_back(RankingAdjustment{
            .id = "score-clamp",
            .label = "display score clamp",
            .kind = AdjustmentKind::Clamp,
            .value = breakdown.final_score,
            .before = unclamped,
            .after = breakdown.final_score,
        });
    }
    return breakdown;
}

double apply_preference_prior_utility(
    const Query& query, const Candidate& candidate, const double score
) {
    return explain_preference_prior(query, candidate, score).final_utility;
}

double apply_preference_prior(const Query& query, const Candidate& candidate, const double score) {
    return explain_preference_prior(query, candidate, score).final_score;
}

} // namespace acclorite::ranking
