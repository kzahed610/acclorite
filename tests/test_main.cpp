#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include <sys/stat.h>

#include "acclorite/core/query.hpp"
#include "acclorite/core/search_engine.hpp"
#include "acclorite/diagnostics/doctor.hpp"
#include "acclorite/output/json_renderer.hpp"
#include "acclorite/output/terminal_renderer.hpp"
#include "acclorite/output/doctor_json_renderer.hpp"
#include "acclorite/query/fuzzy.hpp"
#include "acclorite/query/frame.hpp"
#include "acclorite/query/targets.hpp"
#include "acclorite/query/lexicon.hpp"
#include "acclorite/query/relevance.hpp"
#include "acclorite/ranking/preference.hpp"
#include "acclorite/ranking/confidence.hpp"
#include "acclorite/sources/desktop_source.hpp"
#include "acclorite/sources/index_source.hpp"
#include "acclorite/sources/filesystem_location_source.hpp"
#include "acclorite/sources/man_source.hpp"
#include "acclorite/sources/pacman_source.hpp"
#include "acclorite/sources/path_source.hpp"
#include "acclorite/sources/pkgfile_enricher.hpp"
#include "acclorite/guidance/man_provider.hpp"
#include "acclorite/guidance/provider.hpp"

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}


class ScopedEnv {
public:
    ScopedEnv(const char* name, const std::string& value) : name_(name) {
        const char* raw = std::getenv(name);
        existed_ = raw != nullptr;
        old_value_ = raw ? raw : "";
        ::setenv(name_.c_str(), value.c_str(), 1);
    }

    ~ScopedEnv() {
        if (existed_) {
            ::setenv(name_.c_str(), old_value_.c_str(), 1);
        } else {
            ::unsetenv(name_.c_str());
        }
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    std::string name_;
    std::string old_value_;
    bool existed_{false};
};

class ScopedPath {
public:
    explicit ScopedPath(const std::filesystem::path& path) {
        const char* raw = std::getenv("PATH");
        old_path_ = raw ? raw : "";
        ::setenv("PATH", path.c_str(), 1);
    }

    ~ScopedPath() {
        ::setenv("PATH", old_path_.c_str(), 1);
    }

    ScopedPath(const ScopedPath&) = delete;
    ScopedPath& operator=(const ScopedPath&) = delete;

private:
    std::string old_path_;
};

void write_executable(const std::filesystem::path& path, const std::string& contents) {
    {
        std::ofstream file(path);
        file << contents;
    }
    ::chmod(path.c_str(), 0755);
}

void write_apropos_fixture(const std::filesystem::path& directory, const std::string& output) {
    std::string script = "#!/bin/sh\n";
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        std::string escaped;
        for (const char ch : line) {
            if (ch == '\\' || ch == '\"' || ch == '$' || ch == '`') {
                escaped.push_back('\\');
            }
            escaped.push_back(ch);
        }
        script += "printf '%s\\n' \"" + escaped + "\"\n";
    }
    write_executable(directory / "apropos", script);
}

void write_pacman_fixture(const std::filesystem::path& directory, const std::string& output) {
    std::string script = "#!/bin/sh\n";
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        std::string escaped;
        for (const char ch : line) {
            if (ch == '\\' || ch == '"' || ch == '$' || ch == '`') {
                escaped.push_back('\\');
            }
            escaped.push_back(ch);
        }
        script += "printf '%s\\n' \"" + escaped + "\"\n";
    }
    write_executable(directory / "pacman", script);
}

void write_expac_fixture(const std::filesystem::path& directory, const std::string& output) {
    std::string script = "#!/bin/sh\n";
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        std::string escaped;
        for (const char ch : line) {
            if (ch == '\\' || ch == '"' || ch == '$' || ch == '`') {
                escaped.push_back('\\');
            }
            escaped.push_back(ch);
        }
        script += "printf '%s\\n' \"" + escaped + "\"\n";
    }
    write_executable(directory / "expac", script);
}

void write_pkgfile_fixture(const std::filesystem::path& directory, const std::string& output) {
    std::string script = "#!/bin/sh\n";
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        std::string escaped;
        for (const char ch : line) {
            if (ch == '\\' || ch == '"' || ch == '$' || ch == '`') {
                escaped.push_back('\\');
            }
            escaped.push_back(ch);
        }
        script += "printf '%s\\n' \"" + escaped + "\"\n";
    }
    write_executable(directory / "pkgfile", script);
}
void write_man_page_fixture(const std::filesystem::path& directory, const std::string& output) {
    std::string script = "#!/bin/sh\n";
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        std::string escaped;
        for (const char ch : line) {
            if (ch == '\\' || ch == '"' || ch == '$' || ch == '`') {
                escaped.push_back('\\');
            }
            escaped.push_back(ch);
        }
        script += "printf '%s\\n' \"" + escaped + "\"\n";
    }
    write_executable(directory / "man", script);
}



void test_preference_prior_favors_documented_canonical_front_door() {
    auto query = acclorite::Query::parse("network connections");
    query.frame = acclorite::query::recognize_frame(query);

    acclorite::Candidate obscure{
        .command = "nl-list-sockets",
        .path = "/usr/bin/nl-list-sockets",
        .summary = "Executable available in PATH",
        .source = "path",
        .installed = true,
        .cli_capable = true,
        .score = 0.91,
    };
    acclorite::Candidate canonical{
        .command = "ss",
        .path = "/usr/bin/ss",
        .summary = "utility to investigate sockets",
        .source = "path+man",
        .installed = true,
        .cli_capable = true,
        .score = 0.89,
    };

    const double obscure_score = acclorite::ranking::apply_preference_prior(
        query, obscure, obscure.score
    );
    const double canonical_score = acclorite::ranking::apply_preference_prior(
        query, canonical, canonical.score
    );
    expect(canonical_score > obscure_score,
           "documented canonical socket tool beats PATH-only obscure helper on close semantics");
}

void test_preference_prior_demotes_specialized_grep_for_generic_search() {
    auto query = acclorite::Query::parse("search text");
    query.frame = acclorite::query::recognize_frame(query);

    acclorite::Candidate rg{
        .command = "rg",
        .summary = "recursively search the current directory for lines matching a pattern",
        .source = "path+man",
        .installed = true,
        .cli_capable = true,
        .score = 0.92,
    };
    acclorite::Candidate zipgrep{
        .command = "zipgrep",
        .summary = "search files in a ZIP archive for lines matching a pattern",
        .source = "path+man",
        .installed = true,
        .cli_capable = true,
        .score = 0.92,
    };

    const double rg_score = acclorite::ranking::apply_preference_prior(query, rg, rg.score);
    const double zipgrep_score = acclorite::ranking::apply_preference_prior(
        query, zipgrep, zipgrep.score
    );
    expect(rg_score > zipgrep_score,
           "generic text search prefers normal front door over compressed-format wrapper");

    auto compressed_query = acclorite::Query::parse("search compressed files");
    compressed_query.frame = acclorite::query::recognize_frame(compressed_query);
    const double compressed_zipgrep = acclorite::ranking::apply_preference_prior(
        compressed_query, zipgrep, zipgrep.score
    );
    expect(compressed_zipgrep > zipgrep_score,
           "explicit compressed-file query removes generic-domain specialization penalty");
}

void test_package_projection_penalizes_multi_binary_suite_metadata() {
    auto query = acclorite::Query::parse("network connections");
    query.frame = acclorite::query::recognize_frame(query);

    acclorite::Candidate projected{
        .command = "nl-list-sockets",
        .path = "/usr/bin/nl-list-sockets",
        .summary = "Library for applications dealing with netlink sockets",
        .source = "path+expac+pkgfile",
        .package = "libnl",
        .repository = "core",
        .package_version = "3.12.0-1",
        .installed = true,
        .repository_available = true,
        .cli_capable = true,
        .provided_commands = {
            "genl-ctrl-list", "idiag-socket-details", "nf-ct-add", "nf-ct-events",
            "nf-ct-list", "nf-exp-add", "nf-exp-delete", "nl-list-sockets"
        },
        .score = 0.99,
    };
    acclorite::Candidate ss{
        .command = "ss",
        .path = "/usr/bin/ss",
        .summary = "another utility to investigate sockets",
        .source = "path+man",
        .installed = true,
        .cli_capable = true,
        .score = 0.95,
    };

    const double projected_score = acclorite::ranking::apply_preference_prior(
        query, projected, projected.score
    );
    const double ss_score = acclorite::ranking::apply_preference_prior(query, ss, ss.score);
    expect(ss_score > projected_score,
           "multi-binary package summary cannot overpower direct ss command documentation");
}

void test_generic_text_search_infers_file_content_context() {
    auto query = acclorite::Query::parse("search text");
    query.frame = acclorite::query::recognize_frame(query);
    const auto groups = acclorite::query::concept_groups(query);

    const auto files = std::find_if(groups.begin(), groups.end(), [](const auto& group) {
        return group.term == "files";
    });
    expect(files != groups.end(), "generic search-text query infers file/content context");
    if (files != groups.end()) {
        expect(files->implicit, "search-text file context is marked implicit");
        expect(files->weight < 1.0, "search-text file context stays low weight");
    }

    const auto rg = acclorite::query::score_text(
        query, "rg", "recursively search the current directory for lines matching a pattern"
    );
    const auto namazu = acclorite::query::score_text(
        query, "namazu", "full-text search engine intended for easy use"
    );
    expect(rg.score > namazu.score,
           "generic text search prefers file-content search evidence over indexing-engine wording");

    auto explicit_domain = acclorite::Query::parse("full text search");
    explicit_domain.frame = acclorite::query::recognize_frame(explicit_domain);
    const auto explicit_groups = acclorite::query::concept_groups(explicit_domain);
    const bool implicit_files = std::any_of(explicit_groups.begin(), explicit_groups.end(), [](const auto& group) {
        return group.term == "files" && group.implicit;
    });
    expect(!implicit_files, "explicit full-text domain suppresses file-content default");
}

void test_installed_bonus_is_small_not_absolute() {
    auto query = acclorite::Query::parse("duplicate files");
    query.frame = acclorite::query::recognize_frame(query);

    acclorite::Candidate installed_generic{
        .command = "hardlink",
        .summary = "link multiple copies of a file",
        .source = "path+man",
        .installed = true,
        .cli_capable = true,
        .score = 0.90,
    };
    acclorite::Candidate dedicated_repo{
        .command = "fdupes",
        .summary = "identify duplicate files in directories",
        .source = "expac+pkgfile",
        .package = "fdupes",
        .repository = "extra",
        .repository_available = true,
        .cli_capable = true,
        .provided_commands = {"fdupes"},
        .score = 0.98,
    };

    const auto installed_score = acclorite::ranking::apply_preference_prior(
        query, installed_generic, installed_generic.score
    );
    const auto repo_score = acclorite::ranking::apply_preference_prior(
        query, dedicated_repo, dedicated_repo.score
    );
    expect(repo_score > installed_score,
           "installed bonus does not override a clearly better dedicated repository tool");
}

void test_source_provenance_uses_exact_tokens() {
    expect(!acclorite::source_contains("pacman", "man"),
           "pacman provenance does not masquerade as man provenance");

    std::string sources = "pacman";
    acclorite::merge_sources(sources, "man+pkgfile");
    expect(sources == "pacman+man+pkgfile",
           "source merge preserves exact unique provenance tokens");
}

void test_query_normalization() {
    const auto query = acclorite::Query::parse("  Find   DUPLICATE  Files  ");
    expect(query.normalized == "find duplicate files", "query normalization");
    expect(query.tokens.size() == 3, "query tokenization");
}

void test_query_frame_recognizer_handles_normal_and_cursed_questions() {
    const auto expect_frame = [](const std::string& text, const acclorite::QueryFrame expected,
                                 const bool explicit_frame, const std::string& message) {
        const auto recognized = acclorite::query::recognize_frame(acclorite::Query::parse(text));
        expect(recognized.frame == expected, message);
        expect(recognized.explicit_frame == explicit_frame, message + " explicit/inferred state");
    };

    expect_frame("what do I use to find duplicate files", acclorite::QueryFrame::Discover, true,
                 "tool-seeking what-question is discovery");
    expect_frame("what is the command to monitor cpu", acclorite::QueryFrame::Discover, true,
                 "what-is-the-command is discovery rather than explanation");
    expect_frame("what is ripgrep", acclorite::QueryFrame::Explain, true,
                 "what-is entity question is explanation");
    expect_frame("where is ssh config", acclorite::QueryFrame::Locate, true,
                 "where-is path question is location");
    expect_frame("where's ssh config", acclorite::QueryFrame::Locate, true,
                 "where contraction still resolves to location");
    expect_frame("where do I see network connections", acclorite::QueryFrame::Inspect, true,
                 "where-do-I-see is inspection rather than location");
    expect_frame("edit network connections", acclorite::QueryFrame::Modify, true,
                 "explicit edit action is modification");
    expect_frame("difference between rg and grep", acclorite::QueryFrame::Compare, true,
                 "difference-between query is comparison");
    expect_frame("why is ssh refusing my key", acclorite::QueryFrame::Diagnose, true,
                 "why/refusing query is diagnosis");
    expect_frame("bro where tf can i see what is eating ram", acclorite::QueryFrame::Inspect, true,
                 "messy human question still resolves to inspection");
    expect_frame("fuzzy directory jumping thing", acclorite::QueryFrame::Discover, false,
                 "unframed human request safely defaults to discovery");
}

void test_question_words_do_not_override_actual_action() {
    const auto inspect = acclorite::query::recognize_frame(
        acclorite::Query::parse("what is using port 8080")
    );
    expect(inspect.frame == acclorite::QueryFrame::Inspect,
           "what-is-using is inspection rather than explanation");

    const auto contracted_inspect = acclorite::query::recognize_frame(
        acclorite::Query::parse("what's using port 8080")
    );
    expect(contracted_inspect.frame == acclorite::QueryFrame::Inspect,
           "what's-using contraction is inspection rather than explanation");

    const auto modify = acclorite::query::recognize_frame(
        acclorite::Query::parse("where do i configure wifi")
    );
    expect(modify.frame == acclorite::QueryFrame::Modify,
           "where-do-I-configure is modification rather than location");
}

void test_explain_frame_suppresses_noun_state_inspection_default() {
    auto query = acclorite::Query::parse("what is disk usage");
    query.frame = acclorite::query::recognize_frame(query);
    expect(query.frame.frame == acclorite::QueryFrame::Explain,
           "what-is disk usage resolves to explanation");

    const auto groups = acclorite::query::concept_groups(query);
    const bool has_implicit_inspect = std::any_of(groups.begin(), groups.end(), [](const auto& group) {
        return group.term == "inspect" && group.implicit;
    });
    expect(!has_implicit_inspect,
           "explain frame prevents contradictory implicit inspect intent");
}

void test_lexicon_expands_deterministically() {
    const auto query = acclorite::Query::parse("search text");
    const auto terms = acclorite::query::retrieval_terms(query);

    const auto contains = [&](const std::string& needle) {
        return std::find(terms.begin(), terms.end(), needle) != terms.end();
    };

    expect(contains("search"), "retrieval terms preserve original search token");
    expect(contains("match"), "retrieval terms expand search concept");
    expect(contains("text"), "retrieval terms preserve original text token");
    expect(contains("pattern"), "retrieval terms expand text concept");
}


void test_fuzzy_typo_recovery() {
    expect(acclorite::query::fuzzy::similarity("grpe", "grep") >= 0.74,
           "adjacent command typo has useful fuzzy similarity");
    expect(std::abs(acclorite::query::fuzzy::similarity("abcd", "acbd") - 0.75) < 1e-12,
           "OSA similarity preserves adjacent-transposition distance");
    expect(std::abs(acclorite::query::fuzzy::similarity("kitten", "sitting") -
                    (4.0 / 7.0)) < 1e-12,
           "OSA similarity preserves classic multi-edit distance");
    expect(acclorite::query::fuzzy::similarity("PROCESS", "process") == 1.0,
           "OSA similarity remains case-insensitive");

    const auto groups = acclorite::query::concept_groups(
        acclorite::Query::parse("serch txt")
    );
    const bool has_search = std::any_of(groups.begin(), groups.end(), [](const auto& group) {
        return group.term == "search" && !group.implicit;
    });
    const bool has_text = std::any_of(groups.begin(), groups.end(), [](const auto& group) {
        return group.term == "text" && !group.implicit;
    });
    const bool has_implicit_files = std::any_of(groups.begin(), groups.end(), [](const auto& group) {
        return group.term == "files" && group.implicit;
    });
    expect(has_search, "serch canonicalizes to explicit search concept");
    expect(has_text, "txt canonicalizes to explicit text concept");
    expect(has_implicit_files, "typo-normalized search text also receives file-content context");

    const auto match = acclorite::query::score_text(
        acclorite::Query::parse("serch txt"),
        "rg",
        "recursively search the current directory for lines matching a pattern"
    );
    expect(match.score > 0.80, "typo-normalized concepts still score semantic documentation strongly");
}

void test_action_weight_beats_generic_object_match() {
    const auto query = acclorite::Query::parse("archive files");
    const auto archiver = acclorite::query::score_text(
        query, "ark", "KDE archiving tool"
    );
    const auto file_browser = acclorite::query::score_text(
        query, "files", "browse and inspect files"
    );

    expect(archiver.score > file_browser.score,
           "matching archive action outweighs matching generic files context");
}


void test_specific_subject_beats_generic_usage_match() {
    const auto query = acclorite::Query::parse("disk usage");
    const auto duf = acclorite::query::score_text(
        query, "duf", "Disk Usage/Free Utility"
    );
    const auto pod2usage = acclorite::query::score_text(
        query, "pod2usage", "print usage messages from embedded documentation"
    );

    expect(duf.score > pod2usage.score,
           "disk-specific evidence beats an unrelated generic usage match");
}

void test_path_source_recovers_command_typo() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-path-typo";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);
    write_executable(temp / "grep", "#!/bin/sh\nexit 0\n");

    {
        ScopedPath scoped_path(temp);
        acclorite::PathSource source;
        const auto candidates = source.search(acclorite::Query::parse("grpe"));
        expect(!candidates.empty(), "PATH fuzzy matching recovers a transposed command name");
        if (!candidates.empty()) {
            expect(candidates.front().command == "grep", "grpe resolves to grep");
            expect(candidates.front().score < 0.90, "fuzzy PATH match stays below exact certainty");
        }
    }

    std::filesystem::remove_all(temp);
}

void test_path_source_exact_match() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-path";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    const auto executable = temp / "acclorite-test-tool";
    write_executable(executable, "#!/bin/sh\nexit 0\n");

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PathSource>());

        const auto result = engine.search(acclorite::Query::parse("acclorite-test-tool"));
        expect(!result.candidates.empty(), "PATH source finds executable");
        if (!result.candidates.empty()) {
            expect(result.candidates.front().command == "acclorite-test-tool", "exact PATH match ranks first");
            expect(result.candidates.front().score == 1.0, "exact PATH match has full score");
        }
    }

    std::filesystem::remove_all(temp);
}

void test_path_multiword_match_is_weak_evidence() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-path-multiword";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_executable(temp / "text2image", "#!/bin/sh\nexit 0\n");

    {
        ScopedPath scoped_path(temp);
        acclorite::PathSource source;
        const auto candidates = source.search(acclorite::Query::parse("search text"));
        expect(!candidates.empty(), "PATH keeps partial multiword matches for recall");
        if (!candidates.empty()) {
            expect(candidates.front().score < 0.40, "single-token PATH coincidence stays weak");
        }
    }

    std::filesystem::remove_all(temp);
}

void test_man_source_discovers_by_concepts() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-man";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_executable(temp / "rg", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(temp, "rg (1) - recursively search the current directory for lines matching a pattern\n");

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PathSource>());
        engine.add_source(std::make_unique<acclorite::ManSource>());

        const auto result = engine.search(acclorite::Query::parse("search text"));
        expect(!result.candidates.empty(), "man source finds command by semantic concepts");
        if (!result.candidates.empty()) {
            const auto& best = result.candidates.front();
            expect(best.command == "rg", "description concepts rank rg first");
            expect(best.installed, "man candidate detects installed executable");
        }
    }

    std::filesystem::remove_all(temp);
}

void test_multiword_semantics_beat_name_coincidence() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-relevance";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_executable(temp / "rg", "#!/bin/sh\nexit 0\n");
    write_executable(temp / "text2image", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(
        temp,
        "rg (1) - recursively search the current directory for lines matching a pattern\n"
        "text2image (1) - convert text into an image\n"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PathSource>());
        engine.add_source(std::make_unique<acclorite::ManSource>());

        const auto result = engine.search(acclorite::Query::parse("search text"));
        expect(!result.candidates.empty(), "semantic ranking returns candidates");
        if (!result.candidates.empty()) {
            expect(result.candidates.front().command == "rg", "semantic coverage beats text2image name coincidence");
        }
    }

    std::filesystem::remove_all(temp);
}

void test_man_source_filters_non_command_sections() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-man-sections";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    // Make even the bogus API-looking name executable so this test proves the
    // manual-section guardrail itself is doing the filtering.
    write_executable(temp / "HWLOC_CPUBIND_PROCESS", "#!/bin/sh\nexit 0\n");
    write_executable(temp / "btop", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(
        temp,
        "HWLOC_CPUBIND_PROCESS (3) - CPU binding process flag\n"
        "btop (1) - resource monitor that shows usage and statistics for processes\n"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::ManSource>());

        const auto result = engine.search(acclorite::Query::parse("process monitoring"));
        expect(!result.candidates.empty(), "command section result survives filtering");
        if (!result.candidates.empty()) {
            expect(result.candidates.front().command == "btop", "section 3 API noise is excluded");
        }
        const bool contains_hwloc = std::any_of(
            result.candidates.begin(), result.candidates.end(), [](const acclorite::Candidate& candidate) {
                return candidate.command == "HWLOC_CPUBIND_PROCESS";
            }
        );
        expect(!contains_hwloc, "non-command manual section never becomes candidate");
    }

    std::filesystem::remove_all(temp);
}

void test_network_connection_concepts_prefer_socket_tool() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-network";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_executable(temp / "ss", "#!/bin/sh\nexit 0\n");
    write_executable(temp / "NetworkManager", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(
        temp,
        "ss (8) - another utility to investigate sockets\n"
        "NetworkManager (8) - network management daemon\n"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PathSource>());
        engine.add_source(std::make_unique<acclorite::ManSource>());

        const auto result = engine.search(acclorite::Query::parse("network connections"));
        expect(!result.candidates.empty(), "network query returns command candidates");
        if (!result.candidates.empty()) {
            expect(result.candidates.front().command == "ss", "socket semantics beat generic network daemon");
        }
    }

    std::filesystem::remove_all(temp);
}


void test_search_engine_prefers_documented_socket_front_door_over_path_only_helper() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-preference-socket";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_executable(temp / "ss", "#!/bin/sh\nexit 0\n");
    write_executable(temp / "nl-list-sockets", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(temp, "ss (8) - utility to investigate sockets\n");

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PathSource>());
        engine.add_source(std::make_unique<acclorite::ManSource>());

        const auto result = engine.search(acclorite::Query::parse("network connections"));
        expect(!result.candidates.empty(), "preference socket fixture returns candidates");
        if (!result.candidates.empty()) {
            expect(result.candidates.front().command == "ss",
                   "documented socket front door beats PATH-only helper");
        }
    }

    std::filesystem::remove_all(temp);
}

void test_arch_suite_metadata_does_not_beat_direct_socket_docs() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-arch-suite-specificity";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_executable(temp / "ss", "#!/bin/sh\nexit 0\n");
    write_executable(temp / "nl-list-sockets", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(temp, "ss (8) - another utility to investigate sockets\n");
    write_expac_fixture(
        temp,
        "core\tlibnl\t3.12.0-1\tLibrary for applications dealing with netlink sockets\n"
    );
    write_pkgfile_fixture(
        temp,
        "core/libnl\t/usr/bin/genl-ctrl-list\n"
        "core/libnl\t/usr/bin/idiag-socket-details\n"
        "core/libnl\t/usr/bin/nf-ct-add\n"
        "core/libnl\t/usr/bin/nf-ct-events\n"
        "core/libnl\t/usr/bin/nf-ct-list\n"
        "core/libnl\t/usr/bin/nf-exp-add\n"
        "core/libnl\t/usr/bin/nf-exp-delete\n"
        "core/libnl\t/usr/bin/nl-list-sockets\n"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PathSource>());
        engine.add_source(std::make_unique<acclorite::ManSource>());
        engine.add_source(std::make_unique<acclorite::PacmanSource>());
        engine.add_enricher(std::make_unique<acclorite::PkgfileEnricher>());

        const auto result = engine.search(acclorite::Query::parse("network connections"));
        expect(!result.candidates.empty(), "Arch suite specificity fixture returns candidates");
        if (!result.candidates.empty()) {
            expect(result.candidates.front().command == "ss",
                   "libnl package fan-out cannot make nl-list-sockets outrank direct ss docs");
        }
    }

    std::filesystem::remove_all(temp);
}

void test_arch_full_text_package_does_not_beat_local_rg_for_generic_search() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-arch-search-specificity";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_executable(temp / "rg", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(
        temp,
        "rg (1) - recursively search the current directory for lines matching a pattern\n"
    );
    write_expac_fixture(
        temp,
        "extra\tnamazu\t2.0.21-7\tNamazu is a full-text search engine intended for easy use.\n"
    );
    write_pkgfile_fixture(
        temp,
        "extra/namazu\t/usr/bin/adnmz\n"
        "extra/namazu\t/usr/bin/bnamazu\n"
        "extra/namazu\t/usr/bin/gcnmz\n"
        "extra/namazu\t/usr/bin/kwnmz\n"
        "extra/namazu\t/usr/bin/lnnmz\n"
        "extra/namazu\t/usr/bin/mailutime\n"
        "extra/namazu\t/usr/bin/mknmz\n"
        "extra/namazu\t/usr/bin/namazu\n"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PathSource>());
        engine.add_source(std::make_unique<acclorite::ManSource>());
        engine.add_source(std::make_unique<acclorite::PacmanSource>());
        engine.add_enricher(std::make_unique<acclorite::PkgfileEnricher>());

        const auto result = engine.search(acclorite::Query::parse("search text"));
        expect(!result.candidates.empty(), "Arch generic-search specificity fixture returns candidates");
        if (!result.candidates.empty()) {
            expect(result.candidates.front().command == "rg",
                   "generic search text keeps local rg above multi-binary full-text engine package");
        }
    }

    std::filesystem::remove_all(temp);
}

void test_search_engine_prefers_general_text_search_over_compressed_wrapper() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-preference-search";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_executable(temp / "rg", "#!/bin/sh\nexit 0\n");
    write_executable(temp / "zipgrep", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(
        temp,
        "rg (1) - recursively search the current directory for lines matching a pattern\n"
        "zipgrep (1) - search files in a ZIP archive for lines matching a pattern\n"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PathSource>());
        engine.add_source(std::make_unique<acclorite::ManSource>());

        const auto result = engine.search(acclorite::Query::parse("search text"));
        expect(!result.candidates.empty(), "generic search preference fixture returns candidates");
        if (!result.candidates.empty()) {
            expect(result.candidates.front().command == "rg",
                   "generic text search prefers normal search front door");
        }
    }

    std::filesystem::remove_all(temp);
}

void test_noun_heavy_state_query_infers_read_only_inspection() {
    const auto groups = acclorite::query::concept_groups(
        acclorite::Query::parse("network connections")
    );

    const auto inspect = std::find_if(groups.begin(), groups.end(), [](const auto& group) {
        return group.term == "inspect";
    });
    expect(inspect != groups.end(), "noun-heavy state query infers inspect action");
    if (inspect != groups.end()) {
        expect(inspect->implicit, "inferred inspect action is marked implicit");
    }

    const auto socket_tool = acclorite::query::score_text(
        acclorite::Query::parse("network connections"),
        "ss",
        "utility to investigate sockets"
    );
    const auto editor = acclorite::query::score_text(
        acclorite::Query::parse("network connections"),
        "nm-connection-editor",
        "network connection editor for NetworkManager"
    );

    expect(socket_tool.score > editor.score,
           "bare network-connections query prefers inspection over editing");
    expect(std::find(socket_tool.matched_terms.begin(), socket_tool.matched_terms.end(), "inspect") ==
               socket_tool.matched_terms.end(),
           "implicit inspect action is not presented as user-supplied matched concept");
}

void test_explicit_edit_overrides_read_only_default() {
    const auto query = acclorite::Query::parse("edit network connections");
    const auto groups = acclorite::query::concept_groups(query);

    const bool implicit_inspect = std::any_of(groups.begin(), groups.end(), [](const auto& group) {
        return group.term == "inspect" && group.implicit;
    });
    expect(!implicit_inspect, "explicit edit action suppresses implicit inspect default");

    const auto editor = acclorite::query::score_text(
        query,
        "nm-connection-editor",
        "network connection editor for NetworkManager"
    );
    const auto socket_tool = acclorite::query::score_text(
        query,
        "ss",
        "utility to investigate sockets"
    );
    expect(editor.score > socket_tool.score,
           "explicit edit intent prefers network connection editor");
}

void test_duplicate_action_infers_low_weight_file_context() {
    const auto query = acclorite::Query::parse("duplcate");
    const auto groups = acclorite::query::concept_groups(query);

    const auto files = std::find_if(groups.begin(), groups.end(), [](const auto& group) {
        return group.term == "files";
    });
    expect(files != groups.end(), "duplicate action infers file context");
    if (files != groups.end()) {
        expect(files->implicit, "duplicate file context is marked implicit");
        expect(files->weight < 1.0, "inferred file context remains lower weight than explicit intent");
    }

    const auto hardlink = acclorite::query::score_text(
        query,
        "hardlink",
        "link multiple copies of a file"
    );
    const auto msguniq = acclorite::query::score_text(
        query,
        "msguniq",
        "unify duplicate translations in message catalog"
    );
    expect(hardlink.score > msguniq.score,
           "duplicate-file evidence beats unrelated duplicate message processing");
}

void test_sources_merge_instead_of_replacing_evidence() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-merge";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_executable(temp / "rg", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(temp, "rg (1) - recursively search text for a pattern\n");

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PathSource>());
        engine.add_source(std::make_unique<acclorite::ManSource>());

        const auto result = engine.search(acclorite::Query::parse("rg"));
        expect(!result.candidates.empty(), "merged search finds rg");
        if (!result.candidates.empty()) {
            const auto& best = result.candidates.front();
            expect(best.command == "rg", "merged result remains rg");
            expect(best.path == (temp / "rg").string(), "PATH evidence survives merge");
            expect(best.summary == "recursively search text for a pattern", "documentation enriches PATH summary");
            expect(best.source.find("path") != std::string::npos, "merged result retains path source");
            expect(best.source.find("man") != std::string::npos, "merged result retains man source");
            expect(best.installed, "installed state survives merge");
        }
    }

    std::filesystem::remove_all(temp);
}


void test_desktop_metadata_marks_hybrid_tools() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-desktop";
    const auto bin = temp / "bin";
    const auto apps = temp / "applications";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(bin);
    std::filesystem::create_directories(apps);

    write_executable(bin / "ark", "#!/bin/sh\nexit 0\n");
    {
        std::ofstream desktop(apps / "org.kde.ark.desktop");
        desktop << "[Desktop Entry]\n";
        desktop << "Type=Application\n";
        desktop << "Name=Ark\n";
        desktop << "GenericName=Archiving Tool\n";
        desktop << "Comment=Create and extract archive files\n";
        desktop << "Exec=ark %U\n";
    }

    {
        ScopedPath scoped_path(bin);
        ScopedEnv desktop_dirs("ACCLORITE_DESKTOP_DIRS", apps.string());
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PathSource>());
        engine.add_source(std::make_unique<acclorite::DesktopSource>());

        const auto result = engine.search(acclorite::Query::parse("archive files"));
        expect(!result.candidates.empty(), "desktop metadata participates in discovery");
        if (!result.candidates.empty()) {
            const auto& ark = result.candidates.front();
            expect(ark.command == "ark", "Ark is discoverable from desktop metadata");
            expect(ark.interface_kind() == acclorite::InterfaceKind::Hybrid,
                   "PATH + desktop evidence classifies Ark as Hybrid");
        }
    }

    std::filesystem::remove_all(temp);
}


#if ACCLORITE_HAS_SQLITE
void test_index_applies_implicit_inspection_intent() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-index-network-intent";
    const auto bin = temp / "bin";
    const auto apps = temp / "applications";
    const auto db = temp / "cache/index.db";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(bin);
    std::filesystem::create_directories(apps);

    write_executable(bin / "ss", "#!/bin/sh\nexit 0\n");
    write_executable(bin / "nm-connection-editor", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(
        bin,
        "ss (8) - utility to investigate sockets\n"
        "nm-connection-editor (1) - network connection editor for NetworkManager\n"
    );

    {
        ScopedPath scoped_path(bin);
        ScopedEnv desktop_dirs("ACCLORITE_DESKTOP_DIRS", apps.string());
        ScopedEnv index_path("ACCLORITE_INDEX_PATH", db.string());

        acclorite::IndexSource source;
        expect(source.rebuild(), "network intent fixture index rebuild succeeds");
        const auto candidates = source.search(acclorite::Query::parse("network connections"));
        expect(!candidates.empty(), "indexed network-connections query returns candidates");
        if (!candidates.empty()) {
            expect(candidates.front().command == "ss",
                   "indexed noun-heavy network query prefers socket inspection");
        }
    }

    std::filesystem::remove_all(temp);
}

void test_index_duplicate_default_context_beats_message_catalog() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-index-duplicate-intent";
    const auto bin = temp / "bin";
    const auto apps = temp / "applications";
    const auto db = temp / "cache/index.db";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(bin);
    std::filesystem::create_directories(apps);

    write_executable(bin / "hardlink", "#!/bin/sh\nexit 0\n");
    write_executable(bin / "msguniq", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(
        bin,
        "hardlink (1) - link multiple copies of a file\n"
        "msguniq (1) - unify duplicate translations in message catalog\n"
    );

    {
        ScopedPath scoped_path(bin);
        ScopedEnv desktop_dirs("ACCLORITE_DESKTOP_DIRS", apps.string());
        ScopedEnv index_path("ACCLORITE_INDEX_PATH", db.string());

        acclorite::IndexSource source;
        expect(source.rebuild(), "duplicate intent fixture index rebuild succeeds");
        const auto candidates = source.search(acclorite::Query::parse("duplcate"));
        expect(!candidates.empty(), "indexed duplicate typo query returns candidates");
        if (!candidates.empty()) {
            expect(candidates.front().command == "hardlink",
                   "indexed duplicate query prefers file-oriented duplicate tool");
        }
    }

    std::filesystem::remove_all(temp);
}
#endif

#if ACCLORITE_HAS_SQLITE
void test_index_persists_semantic_catalog() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-index";
    const auto bin = temp / "bin";
    const auto apps = temp / "applications";
    const auto db = temp / "cache/index.db";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(bin);
    std::filesystem::create_directories(apps);

    write_executable(bin / "rg", "#!/bin/sh\nexit 0\n");
    write_executable(bin / "text2image", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(
        bin,
        "rg (1) - recursively search text for lines matching a pattern\n"
        "text2image (1) - convert text into an image\n"
    );

    {
        ScopedPath scoped_path(bin);
        ScopedEnv desktop_dirs("ACCLORITE_DESKTOP_DIRS", apps.string());
        ScopedEnv index_path("ACCLORITE_INDEX_PATH", db.string());

        acclorite::IndexSource source;
        expect(source.rebuild(), "persistent index rebuild succeeds");
        expect(std::filesystem::exists(db), "persistent index database is created");

        const auto candidates = source.search(acclorite::Query::parse("search text"));
        expect(!candidates.empty(), "persistent index returns semantic candidates");
        if (!candidates.empty()) {
            expect(candidates.front().command == "rg", "indexed semantics beat text2image coincidence");
        }
    }

    std::filesystem::remove_all(temp);
}

void test_index_keeps_hybrid_metadata() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-index-hybrid";
    const auto bin = temp / "bin";
    const auto apps = temp / "applications";
    const auto db = temp / "cache/index.db";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(bin);
    std::filesystem::create_directories(apps);

    write_executable(bin / "ark", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(bin, "ark (1) - KDE archiving tool\n");
    {
        std::ofstream desktop(apps / "org.kde.ark.desktop");
        desktop << "[Desktop Entry]\nType=Application\nName=Ark\n";
        desktop << "Comment=Create and extract archive files\nExec=ark %U\n";
    }

    {
        ScopedPath scoped_path(bin);
        ScopedEnv desktop_dirs("ACCLORITE_DESKTOP_DIRS", apps.string());
        ScopedEnv index_path("ACCLORITE_INDEX_PATH", db.string());

        acclorite::IndexSource source;
        expect(source.rebuild(), "hybrid index rebuild succeeds");
        const auto candidates = source.search(acclorite::Query::parse("archive files"));
        expect(!candidates.empty(), "indexed archive query finds Ark");
        if (!candidates.empty()) {
            expect(candidates.front().command == "ark", "Ark survives indexed archive retrieval");
            expect(candidates.front().interface_kind() == acclorite::InterfaceKind::Hybrid,
                   "hybrid capability survives SQLite round-trip");
        }
    }

    std::filesystem::remove_all(temp);
}


void test_index_typo_query_uses_canonical_concepts() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-index-typo";
    const auto bin = temp / "bin";
    const auto apps = temp / "applications";
    const auto db = temp / "cache/index.db";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(bin);
    std::filesystem::create_directories(apps);

    write_executable(bin / "rg", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(
        bin,
        "rg (1) - recursively search text for lines matching a pattern\n"
    );

    {
        ScopedPath scoped_path(bin);
        ScopedEnv desktop_dirs("ACCLORITE_DESKTOP_DIRS", apps.string());
        ScopedEnv index_path("ACCLORITE_INDEX_PATH", db.string());

        acclorite::IndexSource source;
        expect(source.rebuild(), "typo fixture index rebuild succeeds");
        const auto candidates = source.search(acclorite::Query::parse("serch txt"));
        expect(!candidates.empty(), "indexed typo query finds semantic candidate");
        if (!candidates.empty()) {
            expect(candidates.front().command == "rg", "FTS retrieval uses canonicalized typo concepts");
        }
    }

    std::filesystem::remove_all(temp);
}


void test_index_recovers_misspelled_command_name() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-index-command-typo";
    const auto bin = temp / "bin";
    const auto apps = temp / "applications";
    const auto db = temp / "cache/index.db";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(bin);
    std::filesystem::create_directories(apps);

    write_executable(bin / "grep", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(bin, "grep (1) - print lines that match patterns\n");

    {
        ScopedPath scoped_path(bin);
        ScopedEnv desktop_dirs("ACCLORITE_DESKTOP_DIRS", apps.string());
        ScopedEnv index_path("ACCLORITE_INDEX_PATH", db.string());

        acclorite::IndexSource source;
        expect(source.rebuild(), "command typo fixture index rebuild succeeds");
        const auto candidates = source.search(acclorite::Query::parse("grpe"));
        const auto it = std::find_if(candidates.begin(), candidates.end(), [](const auto& candidate) {
            return candidate.command == "grep";
        });
        expect(it != candidates.end(), "persistent index recovers grpe as grep");
    }

    std::filesystem::remove_all(temp);
}


bool stale_source_contains(const acclorite::IndexStatus& status, const std::string_view source) {
    return std::find(status.stale_sources.begin(), status.stale_sources.end(), source) !=
           status.stale_sources.end();
}

void test_index_fingerprints_detect_path_change_and_auto_refresh() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-index-fingerprint-path";
    const auto bin = temp / "bin";
    const auto apps = temp / "applications";
    const auto man = temp / "man";
    const auto db = temp / "cache/index.db";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(bin);
    std::filesystem::create_directories(apps);
    std::filesystem::create_directories(man / "man1");

    write_executable(bin / "rg", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(bin, "rg (1) - recursively search text for lines matching a pattern\n");

    {
        ScopedPath scoped_path(bin);
        ScopedEnv desktop_dirs("ACCLORITE_DESKTOP_DIRS", apps.string());
        ScopedEnv index_path("ACCLORITE_INDEX_PATH", db.string());
        ScopedEnv manpath("MANPATH", man.string());

        acclorite::IndexSource source;
        expect(source.rebuild(), "fingerprint fixture index rebuild succeeds");
        const auto fresh = acclorite::IndexSource::probe();
        expect(fresh.ready && fresh.fingerprinted && !fresh.stale,
               "rebuilt index stores fresh source fingerprints");

        write_executable(bin / "fd", "#!/bin/sh\nexit 0\n");
        const auto stale = acclorite::IndexSource::probe();
        expect(stale.ready && stale.stale, "PATH directory change marks index stale");
        expect(stale_source_contains(stale, "PATH"), "stale probe identifies PATH source");

        expect(source.available(), "normal index availability auto-refreshes stale cache");
        const auto refreshed = acclorite::IndexSource::probe();
        expect(refreshed.ready && refreshed.fingerprinted && !refreshed.stale,
               "automatic stale refresh stores current fingerprints");

        const auto candidates = source.search(acclorite::Query::parse("fd"));
        expect(!candidates.empty() && candidates.front().command == "fd",
               "automatic refresh indexes newly added PATH command");
    }

    std::filesystem::remove_all(temp);
}


void test_fresh_index_does_not_rebuild_on_every_probe() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-index-no-repeat-rebuild";
    const auto bin = temp / "bin";
    const auto apps = temp / "applications";
    const auto man = temp / "man";
    const auto db = temp / "cache/index.db";
    const auto calls = temp / "apropos-calls";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(bin);
    std::filesystem::create_directories(apps);
    std::filesystem::create_directories(man / "man1");

    write_executable(bin / "rg", "#!/bin/sh\nexit 0\n");
    write_executable(
        bin / "apropos",
        "#!/bin/sh\n"
        "printf x >> '" + calls.string() + "'\n"
        "printf '%s\\n' 'rg (1) - recursively search text for lines matching a pattern'\n"
    );

    {
        ScopedPath scoped_path(bin);
        ScopedEnv desktop_dirs("ACCLORITE_DESKTOP_DIRS", apps.string());
        ScopedEnv index_path("ACCLORITE_INDEX_PATH", db.string());
        ScopedEnv manpath("MANPATH", man.string());

        acclorite::IndexSource source;
        expect(source.rebuild(), "fresh-index no-repeat fixture rebuild succeeds");
        std::error_code error;
        const auto calls_after_rebuild = std::filesystem::file_size(calls, error);
        expect(!error && calls_after_rebuild == 1,
               "initial rebuild invokes manual catalog exactly once");

        expect(source.available(), "first fresh availability probe succeeds");
        expect(source.available(), "second fresh availability probe succeeds");
        error.clear();
        const auto calls_after_probes = std::filesystem::file_size(calls, error);
        expect(!error && calls_after_probes == calls_after_rebuild,
               "matching fingerprints avoid rebuilding the index on every invocation");
    }

    std::filesystem::remove_all(temp);
}

void test_index_fingerprints_detect_manual_and_desktop_changes() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-index-fingerprint-knowledge";
    const auto bin = temp / "bin";
    const auto apps = temp / "applications";
    const auto man = temp / "man";
    const auto db = temp / "cache/index.db";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(bin);
    std::filesystem::create_directories(apps);
    std::filesystem::create_directories(man / "man1");

    write_executable(bin / "ark", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(bin, "ark (1) - KDE archiving tool\n");
    {
        std::ofstream desktop(apps / "org.kde.ark.desktop");
        desktop << "[Desktop Entry]\nType=Application\nName=Ark\nExec=ark\n";
    }

    {
        ScopedPath scoped_path(bin);
        ScopedEnv desktop_dirs("ACCLORITE_DESKTOP_DIRS", apps.string());
        ScopedEnv index_path("ACCLORITE_INDEX_PATH", db.string());
        ScopedEnv manpath("MANPATH", man.string());

        acclorite::IndexSource source;
        expect(source.rebuild(), "knowledge fingerprint fixture rebuild succeeds");

        {
            std::ofstream page(man / "man1" / "ark.1");
            page << "fixture manual page\n";
        }
        auto stale = acclorite::IndexSource::probe();
        expect(stale_source_contains(stale, "manuals"),
               "manual tree change is identified as stale input");

        expect(source.available(), "manual fingerprint mismatch auto-refreshes index");
        {
            std::ofstream desktop(apps / "org.kde.ark.desktop", std::ios::app);
            desktop << "Comment=Archive files with Ark\n";
        }
        stale = acclorite::IndexSource::probe();
        expect(stale_source_contains(stale, "desktop metadata"),
               "desktop metadata modification is identified as stale input");
    }

    std::filesystem::remove_all(temp);
}


#endif

void test_pacman_source_discovers_uninstalled_arch_tool() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-pacman-uninstalled";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_pacman_fixture(
        temp,
        "extra/fdupes 2.4.0-1\n"
        "    Program for identifying or deleting duplicate files residing within specified directories\n"
        "core/gettext 0.26-1 [installed]\n"
        "    GNU internationalization library and translation tools\n"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::PacmanSource source;
        expect(source.available(), "fake pacman capability is detected");
        const auto candidates = source.search(acclorite::Query::parse("duplicate files"));
        expect(!candidates.empty(), "pacman source returns repository candidate");
        if (!candidates.empty()) {
            const auto& best = candidates.front();
            expect(best.command == "fdupes", "repository description discovers fdupes");
            expect(!best.installed, "uninstalled package stays uninstalled");
            expect(best.repository_available, "sync package is marked repository-available");
            expect(best.package == "fdupes", "package identity is retained");
            expect(best.repository == "extra", "repository identity is retained");
            expect(best.package_version == "2.4.0-1", "package version is retained");
            expect(best.source == "pacman", "pacman provenance is retained");
        }
    }

    std::filesystem::remove_all(temp);
}


void test_expac_sync_catalog_cache_reuses_local_snapshot_and_invalidates_on_sync_change() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-expac-sync-cache";
    const auto bin = temp / "bin";
    const auto sync = temp / "pacman-sync";
    const auto cache = temp / "cache/arch-packages.tsv";
    const auto calls = temp / "expac-calls";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(bin);
    std::filesystem::create_directories(sync);

    {
        std::ofstream db(sync / "core.db");
        db << "sync-v1";
    }
    write_executable(
        bin / "expac",
        "#!/bin/sh\n"
        "printf x >> '" + calls.string() + "'\n"
        "printf '%s\\n' 'extra\tfdupes\t2.4.0-1\tProgram for identifying or deleting duplicate files residing within specified directories'\n"
        "printf '%s\\n' 'extra\tpdfgrep\t2.2.0-6\tA tool to search text in PDF files'\n"
    );

    {
        ScopedPath scoped_path(bin);
        ScopedEnv enable_cache("ACCLORITE_DISABLE_ARCH_CACHE", "0");
        ScopedEnv sync_dir("ACCLORITE_PACMAN_SYNC_DIR", sync.string());
        ScopedEnv cache_path("ACCLORITE_ARCH_CACHE_PATH", cache.string());

        acclorite::PacmanSource first_source;
        const auto first = first_source.search(acclorite::Query::parse("duplicate files"));
        expect(!first.empty() && first.front().command == "fdupes",
               "first expac cache query returns repository candidate");
        expect(std::filesystem::exists(cache),
               "first expac cache query persists synchronized catalog snapshot");
#if ACCLORITE_HAS_SQLITE
        {
            std::ifstream cache_file(cache, std::ios::binary);
            char header[16]{};
            cache_file.read(header, 16);
            expect(std::string(header, 15) == "SQLite format 3",
                   "SQLite builds persist the Arch snapshot as an indexed database");
        }
#else
        {
            std::ifstream cache_file(cache);
            std::string header;
            std::getline(cache_file, header);
            expect(header.starts_with("acclorite-arch-cache-v3\t"),
                   "no-SQLite builds retain the versioned TSV Arch snapshot");
        }
#endif

        std::error_code error;
        const auto calls_after_first = std::filesystem::file_size(calls, error);
        expect(!error && calls_after_first == 1,
               "first expac cache query enumerates synchronized metadata once");

        acclorite::PacmanSource second_source;
        const auto second = second_source.search(acclorite::Query::parse("search pdf text"));
        expect(!second.empty() && second.front().command == "pdfgrep",
               "second expac cache query searches persisted catalog locally");
        error.clear();
        const auto calls_after_second = std::filesystem::file_size(calls, error);
        expect(!error && calls_after_second == calls_after_first,
               "fresh synchronized package cache avoids a second expac process");

        {
            ScopedEnv disable_cache("ACCLORITE_DISABLE_ARCH_CACHE", "1");
            acclorite::PacmanSource live_source;
            const auto live = live_source.search(acclorite::Query::parse("search pdf text"));
            expect(!live.empty() && !second.empty() &&
                       live.front().command == second.front().command &&
                       live.front().score == second.front().score,
                   "cached Arch package retrieval preserves live expac winner and score");
        }

        {
            std::ofstream db(sync / "core.db", std::ios::app);
            db << "-changed";
        }
        acclorite::PacmanSource refreshed_source;
        const auto refreshed = refreshed_source.search(acclorite::Query::parse("duplicate files"));
        expect(!refreshed.empty() && refreshed.front().command == "fdupes",
               "sync metadata change still returns repository candidates after cache refresh");
        error.clear();
        const auto calls_after_refresh = std::filesystem::file_size(calls, error);
        expect(!error && calls_after_refresh == calls_after_first + 2,
               "pacman sync database fingerprint invalidates and rebuilds package cache after live parity probe");
    }

    std::filesystem::remove_all(temp);
}

void test_expac_cache_rejects_old_schema_and_changed_executable_identity() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-expac-cache-integrity";
    const auto bin = temp / "bin";
    const auto sync = temp / "pacman-sync";
    const auto cache = temp / "cache/arch-packages.tsv";
    const auto calls = temp / "expac-calls";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(bin);
    std::filesystem::create_directories(sync);

    {
        std::ofstream db(sync / "core.db");
        db << "sync-v1";
    }

    auto write_fixture = [&](std::string_view package_line, std::string_view marker) {
        write_executable(
            bin / "expac",
            "#!/bin/sh\n"
            "printf 'x' >> '" + calls.string() + "'\n"
            "printf '%s\\n' '" + std::string(package_line) + "'\n"
            "# " + std::string(marker) + "\n"
        );
    };

    write_fixture(
        "extra\tfdupes\t2.4.0-1\tProgram for identifying duplicate files",
        "fixture-one"
    );

    {
        ScopedPath scoped_path(bin);
        ScopedEnv enable_cache("ACCLORITE_DISABLE_ARCH_CACHE", "0");
        ScopedEnv sync_dir("ACCLORITE_PACMAN_SYNC_DIR", sync.string());
        ScopedEnv cache_path("ACCLORITE_ARCH_CACHE_PATH", cache.string());

        acclorite::PacmanSource source;
        const auto first = source.search(acclorite::Query::parse("duplicate files"));
        expect(!first.empty() && first.front().command == "fdupes",
               "integrity fixture creates initial synchronized package cache");

        std::error_code error;
        const auto calls_after_first = std::filesystem::file_size(calls, error);
        expect(!error && calls_after_first == 1,
               "initial synchronized package cache invokes expac once");

        // Replace the current cache with a deliberately obsolete pre-v0.1 text
        // snapshot. SQLite builds must reject the non-database legacy format, and
        // no-SQLite builds must reject its old schema marker. Either way, the next
        // query rebuilds from synchronized metadata exactly once.
        {
            std::ofstream output(cache, std::ios::trunc | std::ios::binary);
            output << "acclorite-arch-cache-v1\tlegacy-fixture\n";
        }

        acclorite::PacmanSource schema_refresh;
        const auto after_schema = schema_refresh.search(acclorite::Query::parse("duplicate files"));
        expect(!after_schema.empty() && after_schema.front().command == "fdupes",
               "old Arch cache schema is rebuilt transparently");
        error.clear();
        const auto calls_after_schema = std::filesystem::file_size(calls, error);
        expect(!error && calls_after_schema == calls_after_first + 1,
               "old Arch cache schema forces one expac rebuild");

        // Keep pacman's sync database byte-for-byte unchanged, but replace the expac
        // executable with a different fixture. The cache must invalidate because its
        // producer identity changed; otherwise a PATH-shadowed test binary could poison
        // the real user's production catalog indefinitely.
        write_fixture(
            "extra\tpdfgrep\t2.2.0-6\tA tool to search text in PDF files",
            "fixture-two-with-a-different-size"
        );

        acclorite::PacmanSource executable_refresh;
        const auto after_executable = executable_refresh.search(acclorite::Query::parse("search pdf text"));
        expect(!after_executable.empty() && after_executable.front().command == "pdfgrep",
               "changed expac executable identity invalidates synchronized package cache");
        error.clear();
        const auto calls_after_executable = std::filesystem::file_size(calls, error);
        expect(!error && calls_after_executable == calls_after_schema + 1,
               "changed expac executable identity triggers exactly one cache rebuild");
    }

    std::filesystem::remove_all(temp);
}


void test_pacman_metadata_merges_with_installed_tool() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-pacman-merge";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_executable(temp / "fdupes", "#!/bin/sh\nexit 0\n");
    write_pacman_fixture(
        temp,
        "extra/fdupes 2.4.0-1 [installed]\n"
        "    Program for identifying or deleting duplicate files residing within specified directories\n"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PathSource>());
        engine.add_source(std::make_unique<acclorite::PacmanSource>());

        const auto result = engine.search(acclorite::Query::parse("fdupes"));
        expect(!result.candidates.empty(), "installed tool and pacman metadata merge");
        if (!result.candidates.empty()) {
            const auto& best = result.candidates.front();
            expect(best.command == "fdupes", "merged package result remains fdupes");
            expect(best.installed, "installed state survives package merge");
            expect(best.repository_available, "repository availability survives package merge");
            expect(best.path == (temp / "fdupes").string(), "PATH survives package merge");
            expect(best.package == "fdupes", "package metadata survives merge");
            expect(best.source.find("path") != std::string::npos, "merged result keeps PATH source");
            expect(best.source.find("pacman") != std::string::npos, "merged result keeps pacman source");
        }
    }

    std::filesystem::remove_all(temp);
}


void test_expac_is_preferred_for_machine_formatted_arch_metadata() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-expac-backend";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_expac_fixture(
        temp,
        "extra\tfdupes\t2.4.0-1\tProgram for identifying duplicate files\n"
    );
    write_pacman_fixture(
        temp,
        "extra/definitely-wrong 1.0-1\n"
        "    unrelated package that proves expac was preferred\n"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::PacmanSource source;
        const auto candidates = source.search(acclorite::Query::parse("duplicate files"));
        expect(!candidates.empty(), "expac-backed Arch package search returns candidate");
        if (!candidates.empty()) {
            expect(candidates.front().command == "fdupes", "expac parser preserves package name");
            expect(candidates.front().repository == "extra", "expac parser preserves repository");
            expect(candidates.front().package_version == "2.4.0-1", "expac parser preserves version");
            expect(candidates.front().source == "expac", "expac is preferred over pacman when available");
        }
    }

    std::filesystem::remove_all(temp);
}

void test_pkgfile_maps_package_name_to_actual_command() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-pkgfile-command-map";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_pacman_fixture(
        temp,
        "extra/ripgrep 14.1.1-1\n"
        "    A search tool that recursively searches text for regex patterns\n"
    );
    write_pkgfile_fixture(
        temp,
        "extra/ripgrep\t/usr/bin/rg\n"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PacmanSource>());
        engine.add_enricher(std::make_unique<acclorite::PkgfileEnricher>());

        const auto result = engine.search(acclorite::Query::parse("search text"));
        expect(!result.candidates.empty(), "pkgfile-enriched repository search returns candidate");
        if (!result.candidates.empty()) {
            const auto& best = result.candidates.front();
            expect(best.command == "rg", "pkgfile maps ripgrep package to rg executable");
            expect(best.package == "ripgrep", "package identity remains separate from executable name");
            expect(best.repository_available, "pkgfile mapping preserves repository availability");
            expect(!best.installed, "uninstalled mapped executable remains uninstalled");
            expect(best.cli_capable, "repository binary metadata marks candidate CLI-capable");
            expect(best.interface_kind() == acclorite::InterfaceKind::Cli,
                   "uninstalled mapped binary reports CLI interface");
            expect(best.source.find("pkgfile") != std::string::npos,
                   "pkgfile provenance is retained");
            expect(best.provided_commands.size() == 1 && best.provided_commands.front() == "rg",
                   "provided command list is retained");
        }
    }

    std::filesystem::remove_all(temp);
}


void test_pkgfile_retries_unqualified_package_for_arch_derivative_repo() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-pkgfile-derivative-repo";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_pacman_fixture(
        temp,
        "cachyos-extra-v4/ripgrep 14.1.1-1.1\n"
        "    A search tool that recursively searches text for regex patterns\n"
    );
    write_executable(
        temp / "pkgfile",
        "#!/bin/sh\n"
        "case \"$3\" in\n"
        "  cachyos-extra-v4/ripgrep) exit 1 ;;\n"
        "  ripgrep) printf 'extra/ripgrep\\t/usr/bin/rg\\n' ;;\n"
        "esac\n"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PacmanSource>());
        engine.add_enricher(std::make_unique<acclorite::PkgfileEnricher>());

        const auto result = engine.search(acclorite::Query::parse("search text"));
        expect(!result.candidates.empty(), "pkgfile derivative-repo fallback returns candidate");
        if (!result.candidates.empty()) {
            expect(result.candidates.front().command == "rg",
                   "pkgfile retries package name when derivative repo-qualified lookup is unavailable");
            expect(result.candidates.front().repository == "cachyos-extra-v4",
                   "original derivative repository metadata remains authoritative");
        }
    }

    std::filesystem::remove_all(temp);
}

void test_pkgfile_mapped_package_merges_with_local_binary() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-pkgfile-local-merge";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_executable(temp / "rg", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(temp, "rg (1) - recursively search text for a pattern\n");
    write_pacman_fixture(
        temp,
        "extra/ripgrep 14.1.1-1 [installed]\n"
        "    A search tool that recursively searches text for regex patterns\n"
    );
    write_pkgfile_fixture(
        temp,
        "extra/ripgrep\t/usr/bin/rg\n"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PathSource>());
        engine.add_source(std::make_unique<acclorite::ManSource>());
        engine.add_source(std::make_unique<acclorite::PacmanSource>());
        engine.add_enricher(std::make_unique<acclorite::PkgfileEnricher>());

        const auto result = engine.search(acclorite::Query::parse("search text"));
        const auto it = std::find_if(result.candidates.begin(), result.candidates.end(), [](const auto& candidate) {
            return candidate.command == "rg";
        });
        expect(it != result.candidates.end(), "pkgfile-mapped package merges under local rg command");
        if (it != result.candidates.end()) {
            expect(it->installed, "local mapped binary remains installed after merge");
            expect(it->path == (temp / "rg").string(), "local mapped binary path survives merge");
            expect(it->package == "ripgrep", "ripgrep package metadata merges into rg candidate");
            expect(it->source.find("man") != std::string::npos, "merged mapped result retains local documentation evidence");
            expect(it->source.find("pkgfile") != std::string::npos, "merged mapped result retains pkgfile evidence");
        }
    }

    std::filesystem::remove_all(temp);
}


void test_query_frame_targets_extract_named_entities() {
    {
        auto query = acclorite::Query::parse("what is rg");
        query.frame = acclorite::query::recognize_frame(query);
        const auto targets = acclorite::query::frame_targets(query);
        expect(targets.size() == 1 && targets.front() == "rg",
               "Explain frame extracts concrete entity target");
    }

    {
        auto query = acclorite::Query::parse("difference between rg and grep");
        query.frame = acclorite::query::recognize_frame(query);
        const auto targets = acclorite::query::frame_targets(query);
        expect(targets.size() == 2, "Compare frame extracts two targets");
        if (targets.size() == 2) {
            expect(targets[0] == "rg" && targets[1] == "grep",
                   "Compare targets preserve named tool order");
        }
    }

    {
        auto query = acclorite::Query::parse("why is ssh refusing my key");
        query.frame = acclorite::query::recognize_frame(query);
        const auto targets = acclorite::query::frame_targets(query);
        expect(!targets.empty() && targets.front() == "ssh",
               "Diagnose frame extracts affected tool target");
    }
}

void test_explain_frame_resolves_exact_entity_before_substring_noise() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-explain-target";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_executable(temp / "rg", "#!/bin/sh\nexit 0\n");
    write_executable(temp / "ruby-minitest-rg", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(
        temp,
        "rg (1) - recursively search text for lines matching a pattern\n"
        "ruby-minitest-rg (1) - colorful test reporter plugin\n"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PathSource>());
        engine.add_source(std::make_unique<acclorite::ManSource>());

        const auto result = engine.search(acclorite::Query::parse("what is rg"));
        expect(result.frame.frame == acclorite::QueryFrame::Explain,
               "Explain routing preserves frame");
        expect(result.targets.size() == 1 && result.targets.front() == "rg",
               "Explain result exposes resolved target");
        expect(result.candidates.size() == 1,
               "Exact Explain target suppresses substring alternatives");
        if (!result.candidates.empty()) {
            expect(result.candidates.front().command == "rg",
                   "Explain query resolves exact rg entity");
        }
    }

    std::filesystem::remove_all(temp);
}

void test_compare_frame_resolves_named_tools_instead_of_compare_command() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-compare-targets";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_executable(temp / "rg", "#!/bin/sh\nexit 0\n");
    write_executable(temp / "grep", "#!/bin/sh\nexit 0\n");
    write_executable(temp / "compare", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(
        temp,
        "rg (1) - recursively search text for a pattern\n"
        "grep (1) - print lines that match patterns\n"
        "compare (1) - compare two images\n"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PathSource>());
        engine.add_source(std::make_unique<acclorite::ManSource>());

        const auto result = engine.search(acclorite::Query::parse("difference between rg and grep"));
        expect(result.candidates.size() == 2,
               "Compare frame returns the two resolved named tools");
        if (result.candidates.size() == 2) {
            expect(result.candidates[0].command == "rg" && result.candidates[1].command == "grep",
                   "Compare frame preserves target order instead of ranking compare(1)");
        }
    }

    std::filesystem::remove_all(temp);
}

void test_locate_frame_finds_existing_bounded_config_paths() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-locate-config";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp / ".ssh");
    std::filesystem::create_directories(temp / "etc/ssh");
    {
        std::ofstream config(temp / ".ssh/config");
        config << "Host example\n";
    }
    {
        std::ofstream config(temp / "etc/ssh/ssh_config");
        config << "Host *\n";
    }

    {
        ScopedEnv home("HOME", temp.string());
        ScopedEnv xdg("XDG_CONFIG_HOME", (temp / ".config").string());
        acclorite::SearchEngine engine;
        engine.add_location_source(std::make_unique<acclorite::FilesystemLocationSource>(temp / "etc"));

        const auto result = engine.search(acclorite::Query::parse("where is ssh config"));
        expect(result.frame.frame == acclorite::QueryFrame::Locate,
               "Locate frame remains location request");
        expect(!result.locations.empty(), "Locate frame finds existing user config path");

        const auto user_config = (temp / ".ssh/config").string();
        const auto system_config = (temp / "etc/ssh/ssh_config").string();
        const bool has_user_config = std::any_of(result.locations.begin(), result.locations.end(), [&](const auto& hit) {
            return hit.path == user_config;
        });
        const bool has_system_config = std::any_of(result.locations.begin(), result.locations.end(), [&](const auto& hit) {
            return hit.path == system_config;
        });
        expect(has_user_config, "Locate frame includes ~/.ssh/config when it exists");
        expect(has_system_config, "Locate frame includes system ssh_config when it exists");
        if (!result.locations.empty()) {
            expect(result.locations.front().path == user_config,
                   "user config outranks system config when both exist");
        }
    }

    std::filesystem::remove_all(temp);
}

void test_inspect_relations_infer_process_for_port_owner_query() {
    auto query = acclorite::Query::parse("what is using port 8080");
    query.frame = acclorite::query::recognize_frame(query);
    const auto groups = acclorite::query::concept_groups(query);

    const bool process_inferred = std::any_of(groups.begin(), groups.end(), [](const auto& group) {
        return group.term == "process" && group.implicit;
    });
    const bool identify_inferred = std::any_of(groups.begin(), groups.end(), [](const auto& group) {
        return group.term == "identify" && group.implicit;
    });
    expect(process_inferred, "port-owner inspection infers process subject");
    expect(identify_inferred, "port-owner inspection infers identify action");

    const auto fuser = acclorite::query::score_text(
        query, "fuser", "identify processes using files or sockets"
    );
    const auto aseqdump = acclorite::query::score_text(
        query, "aseqdump", "show events received at an ALSA sequencer port"
    );
    expect(fuser.score > aseqdump.score,
           "process/socket ownership evidence beats unrelated port terminology");
}

void test_human_disk_space_query_preserves_consumption_relation() {
    auto query = acclorite::Query::parse("what is using all my disk space");
    query.frame = acclorite::query::recognize_frame(query);
    const auto groups = acclorite::query::concept_groups(query);

    const auto has = [&](const std::string& term) {
        return std::any_of(groups.begin(), groups.end(), [&](const auto& group) {
            return group.term == term;
        });
    };
    expect(query.frame.frame == acclorite::QueryFrame::Inspect,
           "human disk-space consumption phrasing remains Inspect");
    expect(has("usage"), "relational 'using' becomes the usage/consumption action");
    expect(has("disk"), "disk-space human phrasing keeps disk subject");
    expect(!has("all"), "quantity word 'all' is not treated as a semantic subject");

    const auto duf = acclorite::query::score_text(
        query, "duf", "disk usage and free-space utility"
    );
    const auto dump_exfat = acclorite::query::score_text(
        query, "dump.exfat", "show on-disk information of exfat filesystem"
    );
    expect(duf.score > dump_exfat.score + 0.25,
           "disk consumption intent strongly prefers usage tooling over filesystem dump metadata");
}

void test_port_owner_human_relation_prefers_owner_lookup_over_monitor() {
    auto query = acclorite::Query::parse("which process is using port 8080");
    query.frame = acclorite::query::recognize_frame(query);
    const auto groups = acclorite::query::concept_groups(query);

    const auto has = [&](const std::string& term) {
        return std::any_of(groups.begin(), groups.end(), [&](const auto& group) {
            return group.term == term;
        });
    };
    expect(has("usage"), "port-owner phrasing keeps relational using/usage action");
    expect(has("identify"), "port-owner phrasing infers owner-identification action");

    const auto fuser = acclorite::query::score_text(
        query, "fuser", "identify processes using files or sockets"
    );
    const auto htop = acclorite::query::score_text(
        query, "htop", "interactive process viewer showing CPU memory and resource usage"
    );
    expect(fuser.score > htop.score + 0.25,
           "socket/process ownership relation beats generic process monitoring");
}

void test_tar_gz_format_intent_prefers_tar_family_over_generic_extractor() {
    auto query = acclorite::Query::parse("extract a tar.gz");
    query.frame = acclorite::query::recognize_frame(query);
    const auto groups = acclorite::query::concept_groups(query);
    const bool tar_format = std::any_of(groups.begin(), groups.end(), [](const auto& group) {
        return group.term == "tar" && !group.implicit;
    });
    expect(tar_format, "tar.gz is recognized as an explicit tar-format concept");

    const auto tar = acclorite::query::score_text(query, "tar", "an archiving utility");
    const auto unzip = acclorite::query::score_text(
        query, "unzip", "list, test and extract compressed files in a ZIP archive"
    );
    expect(tar.score > unzip.score + 0.25,
           "explicit tar.gz format outweighs a generic extraction verb match from unzip");
}

void test_task_compare_is_discovery_not_entity_compare() {
    auto task = acclorite::Query::parse("compare two folders");
    task.frame = acclorite::query::recognize_frame(task);
    expect(task.frame.frame == acclorite::QueryFrame::Discover,
           "compare two folders remains tool discovery rather than entity comparison");

    auto entities = acclorite::Query::parse("compare rg and grep");
    entities.frame = acclorite::query::recognize_frame(entities);
    expect(entities.frame.frame == acclorite::QueryFrame::Compare,
           "compare rg and grep remains entity-first Compare frame");

    const auto groups = acclorite::query::concept_groups(task);
    const bool quantity_leaked = std::any_of(groups.begin(), groups.end(), [](const auto& group) {
        return group.term == "two";
    });
    expect(!quantity_leaked, "quantity word two is not scored as a comparison subject");
}

void test_benchmark_driven_subsystem_specializations_are_query_relative() {
    auto network = acclorite::Query::parse("network connections");
    network.frame = acclorite::query::recognize_frame(network);
    acclorite::Candidate nl{
        .command = "nl-list-sockets",
        .summary = "libnl utility to list netlink sockets",
        .source = "expac+pkgfile",
        .package = "libnl",
        .repository_available = true,
        .cli_capable = true,
        .semantic_fit = 0.881,
        .score = 0.901,
    };
    expect(acclorite::ranking::effective_semantic_fit(network, nl) < 0.75,
           "generic network-connections intent discounts netlink-specific helper");

    auto netlink = acclorite::Query::parse("list netlink sockets");
    netlink.frame = acclorite::query::recognize_frame(netlink);
    expect(std::abs(acclorite::ranking::effective_semantic_fit(netlink, nl) - 0.881) < 0.000001,
           "explicit netlink intent removes netlink specialization penalty");

    auto compress = acclorite::Query::parse("compress a folder");
    compress.frame = acclorite::query::recognize_frame(compress);
    acclorite::Candidate hwloc{
        .command = "hwloc-compress-dir",
        .summary = "Compress a directory of XML topologies",
        .source = "path+man",
        .installed = true,
        .cli_capable = true,
        .semantic_fit = 0.88,
        .score = 0.92,
    };
    expect(acclorite::ranking::effective_semantic_fit(compress, hwloc) < 0.70,
           "generic folder compression discounts hardware-topology compressor");

    auto folders = acclorite::Query::parse("compare two folders");
    folders.frame = acclorite::query::recognize_frame(folders);
    acclorite::Candidate sfddiff{
        .command = "sfddiff",
        .summary = "compare two font files",
        .source = "path+man",
        .installed = true,
        .cli_capable = true,
        .semantic_fit = 0.86,
        .score = 0.90,
    };
    expect(acclorite::ranking::effective_semantic_fit(folders, sfddiff) < 0.75,
           "generic folder comparison discounts font-specific comparator");

    auto disk = acclorite::Query::parse("disk usage");
    disk.frame = acclorite::query::recognize_frame(disk);
    acclorite::Candidate exfat{
        .command = "dump.exfat",
        .summary = "show on-disk information of exfat filesystem",
        .source = "path+man",
        .installed = true,
        .cli_capable = true,
        .semantic_fit = 0.86,
        .score = 0.90,
    };
    expect(acclorite::ranking::effective_semantic_fit(disk, exfat) < 0.70,
           "generic disk intent discounts filesystem-format-specific diagnostic helper");
}

void test_inspect_relations_infer_process_for_memory_hog_query() {
    auto query = acclorite::Query::parse("bro where tf can i see what is eating ram");
    query.frame = acclorite::query::recognize_frame(query);

    const auto btop = acclorite::query::score_text(
        query,
        "btop",
        "resource monitor showing processor memory and processes usage and statistics"
    );
    const auto lsmem = acclorite::query::score_text(
        query,
        "lsmem",
        "list ranges of available memory with their online status"
    );
    expect(btop.score > lsmem.score,
           "memory-hog inspection prefers process resource monitor over memory-map listing");
}

void test_diagnose_frame_promotes_named_affected_tool() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-diagnose-target";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_executable(temp / "ssh", "#!/bin/sh\nexit 0\n");
    write_executable(temp / "ssh-keygen", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(
        temp,
        "ssh (1) - OpenSSH remote login client\n"
        "ssh-keygen (1) - OpenSSH authentication key utility\n"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PathSource>());
        engine.add_source(std::make_unique<acclorite::ManSource>());

        const auto result = engine.search(acclorite::Query::parse("why is ssh refusing my key"));
        expect(!result.candidates.empty(), "Diagnose frame returns related tools");
        if (!result.candidates.empty()) {
            expect(result.candidates.front().command == "ssh",
                   "Diagnose frame promotes explicitly affected ssh tool");
        }
    }

    std::filesystem::remove_all(temp);
}


const acclorite::diagnostics::DoctorCheck* find_doctor_check(
    const acclorite::diagnostics::DoctorReport& report,
    const std::string_view id
) {
    const auto it = std::find_if(report.checks.begin(), report.checks.end(), [&](const auto& item) {
        return item.id == id;
    });
    return it == report.checks.end() ? nullptr : &*it;
}

void write_pkgfile_missing_metadata_fixture(const std::filesystem::path& directory) {
    write_executable(
        directory / "pkgfile",
        "#!/bin/sh\n"
        "exit 1\n"
    );
}

void test_doctor_is_read_only_when_index_is_missing() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-doctor-read-only";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp / "bin");

    write_executable(temp / "bin" / "man", "#!/bin/sh\nexit 0\n");
    ScopedPath scoped_path(temp / "bin");
    ScopedEnv index_path("ACCLORITE_INDEX_PATH", (temp / "missing-index.db").string());
    ScopedEnv desktop_dirs("ACCLORITE_DESKTOP_DIRS", (temp / "no-applications").string());

    const auto report = acclorite::diagnostics::Doctor{}.run();
    const auto* index = find_doctor_check(report, "index");
#if ACCLORITE_HAS_SQLITE
    expect(index != nullptr, "doctor reports persistent index state");
    if (index) {
        expect(index->state == acclorite::diagnostics::DoctorState::Warning,
               "missing SQLite index is reported as a warning");
    }
    expect(!std::filesystem::exists(temp / "missing-index.db"),
           "doctor never creates or rebuilds a missing index");
#else
    expect(index == nullptr, "no-SQLite doctor does not invent persistent index readiness");
#endif

    std::filesystem::remove_all(temp);
}


void test_doctor_reports_stale_index_without_refreshing_it() {
#if ACCLORITE_HAS_SQLITE
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-doctor-stale-index";
    const auto bin = temp / "bin";
    const auto apps = temp / "applications";
    const auto man = temp / "man";
    const auto db = temp / "cache/index.db";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(bin);
    std::filesystem::create_directories(apps);
    std::filesystem::create_directories(man / "man1");

    write_executable(bin / "rg", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(bin, "rg (1) - recursively search text for lines matching a pattern\n");

    {
        ScopedPath scoped_path(bin);
        ScopedEnv desktop_dirs("ACCLORITE_DESKTOP_DIRS", apps.string());
        ScopedEnv index_path("ACCLORITE_INDEX_PATH", db.string());
        ScopedEnv manpath("MANPATH", man.string());

        acclorite::IndexSource source;
        expect(source.rebuild(), "doctor stale fixture rebuild succeeds");
        write_executable(bin / "fd", "#!/bin/sh\nexit 0\n");
        expect(acclorite::IndexSource::probe().stale, "fixture is stale before doctor runs");

        const auto report = acclorite::diagnostics::Doctor{}.run();
        const auto* index = find_doctor_check(report, "index");
        expect(index != nullptr, "doctor includes stale persistent index check");
        if (index) {
            expect(index->state == acclorite::diagnostics::DoctorState::Warning,
                   "doctor reports stale index as warning");
            expect(index->detail.find("stale") != std::string::npos,
                   "doctor stale warning says the index is stale");
        }
        expect(acclorite::IndexSource::probe().stale,
               "doctor leaves stale index untouched instead of rebuilding it");
    }

    std::filesystem::remove_all(temp);
#endif
}

void test_doctor_distinguishes_pkgfile_binary_from_metadata_readiness() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-doctor-pkgfile";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp / "bin");

    write_expac_fixture(temp / "bin", "pacman\n");
    write_pacman_fixture(temp / "bin", "core/pacman 7.0.0\n    package manager\n");
    write_pkgfile_missing_metadata_fixture(temp / "bin");

    {
        ScopedPath scoped_path(temp / "bin");
        ScopedEnv index_path("ACCLORITE_INDEX_PATH", (temp / "index.db").string());
        ScopedEnv desktop_dirs("ACCLORITE_DESKTOP_DIRS", (temp / "no-applications").string());
        const auto report = acclorite::diagnostics::Doctor{}.run();
        const auto* helper = find_doctor_check(report, "pkgfile");
        const auto* metadata = find_doctor_check(report, "pkgfile-metadata");
        expect(helper != nullptr, "doctor reports pkgfile helper state");
        expect(metadata != nullptr, "doctor reports pkgfile metadata state");
        if (helper) {
            expect(helper->state == acclorite::diagnostics::DoctorState::Ready,
                   "installed pkgfile helper is reported ready even when metadata is missing");
        }
        if (metadata) {
            expect(metadata->state == acclorite::diagnostics::DoctorState::Warning,
                   "installed pkgfile with unusable metadata is a warning");
            expect(metadata->hint.find("pkgfile --update") != std::string::npos,
                   "pkgfile metadata warning includes explicit repair command");
        }
    }

    write_pkgfile_fixture(temp / "bin", "core/pacman\t/usr/bin/pacman\n");
    {
        ScopedPath scoped_path(temp / "bin");
        ScopedEnv index_path("ACCLORITE_INDEX_PATH", (temp / "index.db").string());
        ScopedEnv desktop_dirs("ACCLORITE_DESKTOP_DIRS", (temp / "no-applications").string());
        const auto report = acclorite::diagnostics::Doctor{}.run();
        const auto* pkgfile = find_doctor_check(report, "pkgfile");
        const auto* metadata = find_doctor_check(report, "pkgfile-metadata");
        expect(pkgfile != nullptr, "doctor reports ready pkgfile helper state");
        expect(metadata != nullptr, "doctor reports ready pkgfile metadata state");
        if (metadata) {
            expect(metadata->state == acclorite::diagnostics::DoctorState::Ready,
                   "usable pkgfile metadata is reported ready");
        }
    }

    std::filesystem::remove_all(temp);
}

void test_doctor_json_is_machine_readable_and_declares_no_mutation() {
    acclorite::diagnostics::DoctorReport report;
    report.checks.push_back(acclorite::diagnostics::DoctorCheck{
        .id = "pkgfile",
        .section = "Arch integration",
        .label = "Package → executable mapping",
        .state = acclorite::diagnostics::DoctorState::Warning,
        .detail = "metadata unavailable",
        .hint = "Run: sudo pkgfile --update",
    });

    std::ostringstream out;
    acclorite::DoctorJsonRenderer{}.render(report, out);
    const auto json = out.str();
    expect(json.find("\"doctor_schema_version\": 1") != std::string::npos,
           "doctor JSON has its own schema version");
    expect(json.find("\"mutated_system\": false") != std::string::npos,
           "doctor JSON explicitly declares read-only behavior");
    expect(json.find("\"status\": \"degraded\"") != std::string::npos,
           "doctor JSON exposes aggregate health state");
    expect(json.find("pkgfile --update") != std::string::npos,
           "doctor JSON retains repair hint");
}


void test_ranking_breakdown_tracks_actual_preference_steps() {
    auto query = acclorite::Query::parse("network connections");
    query.frame = acclorite::query::recognize_frame(query);

    acclorite::Candidate candidate{
        .command = "ss",
        .path = "/usr/bin/ss",
        .summary = "utility to investigate sockets",
        .source = "path+man",
        .installed = true,
        .cli_capable = true,
        .score = 0.89,
    };

    const auto breakdown = acclorite::ranking::explain_preference_prior(
        query, candidate, candidate.score
    );
    expect(std::abs(breakdown.base_score - 0.89) < 0.000001,
           "ranking trace preserves the pre-preference base score");
    expect(std::abs(breakdown.final_score - acclorite::ranking::apply_preference_prior(
        query, candidate, candidate.score
    )) < 0.000001, "ranking trace final score matches normal preference scoring");

    const auto has_adjustment = [&](const std::string& id) {
        return std::any_of(
            breakdown.adjustments.begin(), breakdown.adjustments.end(),
            [&](const acclorite::ranking::RankingAdjustment& adjustment) {
                return adjustment.id == id;
            }
        );
    };
    expect(has_adjustment("man-evidence"), "ranking trace records man evidence bonus");
    expect(has_adjustment("multi-source-confidence"), "ranking trace records multi-source bonus");
    expect(has_adjustment("installed"), "ranking trace records installed-tool bonus");
    expect(has_adjustment("canonical-front-door"), "ranking trace records canonical utility prior");

    double previous = breakdown.base_score;
    for (const auto& adjustment : breakdown.adjustments) {
        expect(std::abs(adjustment.before - previous) < 0.000001,
               "ranking trace adjustment begins at previous score");
        previous = adjustment.after;
    }
    expect(std::abs(previous - breakdown.final_score) < 0.000001,
           "ranking trace adjustment chain ends at final score");
}

void test_search_engine_ranking_diagnostics_are_opt_in_and_renderable() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-ranking-trace";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_executable(temp / "rg", "#!/bin/sh\nexit 0\n");
    write_executable(temp / "zipgrep", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(
        temp,
        "rg (1) - recursively search the current directory for lines matching a pattern\n"
        "zipgrep (1) - search files in a ZIP archive for lines matching a pattern\n"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PathSource>());
        engine.add_source(std::make_unique<acclorite::ManSource>());

        const auto normal = engine.search(acclorite::Query::parse("search text"));
        expect(normal.candidates.size() >= 2, "normal ranking fixture returns competing candidates");
        if (!normal.candidates.empty()) {
            expect(!normal.candidates.front().ranking.has_value(),
                   "normal search does not attach ranking diagnostics");
        }
        std::ostringstream normal_terminal;
        acclorite::TerminalRenderer{}.render(normal, normal_terminal);
        expect(normal_terminal.str().find("Ranking diagnostics") == std::string::npos,
               "normal terminal output stays free of ranking-debug noise");

        const auto explained = engine.search(
            acclorite::Query::parse("search text"), 10, true
        );
        expect(explained.candidates.size() >= 2,
               "ranking diagnostics fixture keeps competing candidates");
        expect(explained.candidates.size() == normal.candidates.size(),
               "enabling ranking diagnostics does not change candidate count");
        const std::size_t comparable = std::min(explained.candidates.size(), normal.candidates.size());
        for (std::size_t i = 0; i < comparable; ++i) {
            expect(explained.candidates[i].command == normal.candidates[i].command,
                   "enabling ranking diagnostics does not change candidate ordering");
            expect(std::abs(explained.candidates[i].score - normal.candidates[i].score) < 0.000001,
                   "enabling ranking diagnostics does not change actual candidate scores");
            expect(std::abs(explained.candidates[i].ranking_utility -
                            normal.candidates[i].ranking_utility) < 0.000001,
                   "enabling ranking diagnostics does not change ranking utility");
        }
        if (!explained.candidates.empty()) {
            expect(explained.candidates.front().ranking.has_value(),
                   "explain-ranking search attaches structured trace");
            if (explained.candidates.front().ranking) {
                expect(std::abs(explained.candidates.front().ranking->final_score -
                                explained.candidates.front().score) < 0.000001,
                       "candidate score equals ranking trace final score");
            }
        }

        std::ostringstream terminal;
        acclorite::TerminalRenderer{}.render(explained, terminal);
        const auto text = terminal.str();
        expect(text.find("Ranking diagnostics") != std::string::npos,
               "terminal renderer shows opt-in ranking diagnostics section");
        expect(text.find("Base ranking utility") != std::string::npos,
               "terminal ranking diagnostics explain bounded score vs ranking utility");
        expect(text.find("rg\n  Base evidence") != std::string::npos,
               "terminal ranking diagnostics include winning candidate base evidence");
        expect(text.find("zipgrep\n  Base evidence") != std::string::npos,
               "terminal ranking diagnostics include competing candidate base evidence");
        expect(text.find("Coverage") != std::string::npos,
               "terminal ranking diagnostics decompose semantic coverage");

        std::ostringstream json_out;
        acclorite::JsonRenderer{}.render(explained, json_out);
        const auto json = json_out.str();
        expect(json.find("\"ranking\": {") != std::string::npos,
               "JSON exposes ranking object when diagnostics are requested");
        expect(json.find("\"adjustments\"") != std::string::npos,
               "JSON ranking diagnostics include structured adjustments");
        expect(json.find("\"base_evidence\"") != std::string::npos,
               "JSON ranking diagnostics include source-level base evidence");
        expect(json.find("\"concepts\"") != std::string::npos,
               "JSON ranking diagnostics include semantic concept traces");
    }

    std::filesystem::remove_all(temp);
}


void test_semantic_breakdown_exposes_weighted_concept_matches() {
    auto query = acclorite::Query::parse("search text");
    query.frame = acclorite::query::recognize_frame(query);
    query.explain_ranking = true;

    const auto match = acclorite::query::score_text(
        query,
        "rg",
        "recursively search the current directory for lines matching a pattern",
        0.99,
        0.96
    );

    expect(match.breakdown.has_value(),
           "semantic scorer emits structured breakdown only when ranking diagnostics are enabled");
    if (!match.breakdown) {
        return;
    }

    const auto& breakdown = *match.breakdown;
    expect(breakdown.coverage > 0.99, "semantic breakdown records full weighted concept coverage");
    expect(breakdown.average_quality > 0.65, "semantic breakdown records average match quality");
    expect(!breakdown.concepts.empty(), "semantic breakdown exposes per-concept evidence");

    const auto find_concept = [&](const std::string& term) -> const acclorite::query::ConceptScoreTrace* {
        const auto it = std::find_if(
            breakdown.concepts.begin(), breakdown.concepts.end(),
            [&](const acclorite::query::ConceptScoreTrace& item) { return item.term == term; }
        );
        return it == breakdown.concepts.end() ? nullptr : &*it;
    };

    const auto* search = find_concept("search");
    const auto* text = find_concept("text");
    const auto* files = find_concept("files");
    expect(search != nullptr && search->quality > 0.0,
           "semantic trace explains the explicit search action match");
    expect(text != nullptr && text->quality > 0.0,
           "semantic trace explains the explicit text subject match");
    expect(files != nullptr && files->implicit && files->quality > 0.0,
           "semantic trace exposes the inferred file-content context separately");
}

class FixedEvidenceSource final : public acclorite::KnowledgeSource {
public:
    FixedEvidenceSource(std::string source, double score)
        : source_(std::move(source)), score_(score) {}

    [[nodiscard]] bool available() const override { return true; }

    [[nodiscard]] std::vector<acclorite::Candidate> search(const acclorite::Query& query) const override {
        acclorite::Candidate candidate{
            .command = "same-tool",
            .path = "/usr/bin/same-tool",
            .summary = "search text in files",
            .source = source_,
            .installed = true,
            .cli_capable = true,
            .score = score_,
        };
        if (query.explain_ranking) {
            candidate.evidence_trace.push_back(acclorite::ranking::SourceEvidenceTrace{
                .source = source_,
                .method = "test source evidence",
                .semantic = std::nullopt,
                .adjustments = {},
                .final_score = score_,
            });
        }
        return {std::move(candidate)};
    }

private:
    std::string source_;
    double score_{0.0};
};

void test_base_merge_trace_exposes_preference_headroom_loss() {
    acclorite::SearchEngine engine;
    engine.add_source(std::make_unique<FixedEvidenceSource>("source-a", 0.96));
    engine.add_source(std::make_unique<FixedEvidenceSource>("source-b", 0.90));

    const auto result = engine.search(acclorite::Query::parse("search text"), 10, true);
    expect(!result.candidates.empty(), "base-merge diagnostics fixture produces a candidate");
    if (result.candidates.empty() || !result.candidates.front().ranking) {
        return;
    }

    const auto& ranking = *result.candidates.front().ranking;
    expect(ranking.base_utility > 1.0,
           "independent source merge preserves unbounded base ranking utility");
    expect(result.candidates.front().ranking_utility > 1.0,
           "candidate retains ranking headroom above the public score ceiling");
    expect(std::abs(result.candidates.front().score - 1.0) < 0.000001,
           "public candidate score remains bounded despite ranking headroom");
    expect(ranking.source_evidence.size() == 2,
           "base diagnostics retain both independent source scores");
    expect(!ranking.base_merges.empty(),
           "base diagnostics expose source merge arithmetic");
    if (!ranking.base_merges.empty()) {
        const auto& merge = ranking.base_merges.back();
        expect(merge.raw_after > 1.0,
               "base merge trace preserves the raw score that exceeded bounded headroom");
        expect(std::abs(merge.after - 1.0) < 0.000001,
               "base merge trace records the current clamp separately from raw support");
    }
}


void test_same_source_variants_do_not_double_vote() {
    acclorite::SearchEngine engine;
    engine.add_source(std::make_unique<FixedEvidenceSource>("expac", 0.90));
    engine.add_source(std::make_unique<FixedEvidenceSource>("expac", 0.91));

    const auto result = engine.search(acclorite::Query::parse("search text"), 10, true);
    expect(!result.candidates.empty(), "same-source evidence fixture returns a candidate");
    if (result.candidates.empty() || !result.candidates.front().ranking) {
        return;
    }

    const auto& candidate = result.candidates.front();
    const auto& ranking = *candidate.ranking;
    expect(std::abs(ranking.base_utility - 0.91) < 0.000001,
           "same relevance source keeps the strongest variant instead of adding support");
    expect(ranking.base_merges.empty(),
           "same-source variants do not create independent base-merge support");
    expect(ranking.source_evidence.size() == 1,
           "same-source diagnostic evidence is deduplicated");
}


void test_arch_repo_variants_do_not_self_corrobate() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-repo-variant-evidence";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_expac_fixture(
        temp,
        "cachyos-extra-v4\tfdupes\t2.4.0-2.1\tProgram for identifying duplicate files\n"
        "extra\tfdupes\t2.4.0-2\tProgram for identifying duplicate files\n"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PacmanSource>());
        const auto result = engine.search(
            acclorite::Query::parse("duplicate files"), 10, true
        );
        expect(!result.candidates.empty(),
               "Arch repo-variant fixture returns a package candidate");
        if (!result.candidates.empty() && result.candidates.front().ranking) {
            const auto& ranking = *result.candidates.front().ranking;
            expect(ranking.source_evidence.size() == 1,
                   "CachyOS and Arch variants of one package yield one expac evidence trace");
            expect(ranking.base_merges.empty(),
                   "repo variants from the same backend do not self-corrobate");
            expect(ranking.base_utility <= 0.96 + 0.000001,
                   "repo variants do not inflate package relevance above source ceiling");
        }
    }

    std::filesystem::remove_all(temp);
}

void test_pkgfile_provenance_is_not_semantic_corroboration() {
    auto query = acclorite::Query::parse("duplicate files");
    query.frame = acclorite::query::recognize_frame(query);

    acclorite::Candidate candidate{
        .command = "fdupes",
        .summary = "identify duplicate files",
        .source = "expac+pkgfile",
        .package = "fdupes",
        .repository_available = true,
        .cli_capable = true,
        .provided_commands = {"fdupes"},
        .score = 0.90,
    };

    const auto breakdown = acclorite::ranking::explain_preference_prior(
        query, candidate, candidate.score
    );
    const bool has_multi_source_bonus = std::any_of(
        breakdown.adjustments.begin(), breakdown.adjustments.end(),
        [](const acclorite::ranking::RankingAdjustment& adjustment) {
            return adjustment.id == "multi-source-confidence";
        }
    );
    expect(!has_multi_source_bonus,
           "pkgfile capability provenance does not count as independent semantic evidence");
}

class SaturatedRankingSource final : public acclorite::KnowledgeSource {
public:
    [[nodiscard]] bool available() const override { return true; }

    [[nodiscard]] std::vector<acclorite::Candidate> search(const acclorite::Query&) const override {
        return {
            acclorite::Candidate{
                .command = "duf",
                .summary = "disk usage utility",
                .source = "test",
                .cli_capable = true,
                .score = 0.995,
            },
            acclorite::Candidate{
                .command = "ncdu",
                .summary = "disk usage analyzer",
                .source = "test",
                .cli_capable = true,
                .score = 0.990,
            },
        };
    }
};

void test_ranking_utility_preserves_order_beyond_display_clamp() {
    acclorite::SearchEngine engine;
    engine.add_source(std::make_unique<SaturatedRankingSource>());

    const auto result = engine.search(acclorite::Query::parse("disk usage"), 10, true);
    expect(result.candidates.size() == 2,
           "ranking-headroom fixture returns both saturated candidates");
    if (result.candidates.size() != 2) {
        return;
    }

    const auto& first = result.candidates[0];
    const auto& second = result.candidates[1];
    expect(first.command == "duf",
           "higher unbounded ranking utility wins even when public scores tie");
    expect(std::abs(first.score - 1.0) < 0.000001 &&
           std::abs(second.score - 1.0) < 0.000001,
           "both saturated candidates retain bounded public score 1.0");
    expect(first.ranking_utility > second.ranking_utility,
           "ranking utility preserves ordering information beyond display saturation");
    if (first.ranking && second.ranking) {
        expect(first.ranking->final_utility > second.ranking->final_utility,
               "diagnostics expose the same utility used for sorting");
    }
}



class SemanticGuardSource final : public acclorite::KnowledgeSource {
public:
    [[nodiscard]] bool available() const override { return true; }

    [[nodiscard]] std::vector<acclorite::Candidate> search(const acclorite::Query&) const override {
        return {
            acclorite::Candidate{
                .command = "hardlink",
                .summary = "link multiple copies of a file",
                .source = "path+man",
                .installed = true,
                .cli_capable = true,
                .semantic_fit = 0.8503,
                .score = 0.9039,
            },
            acclorite::Candidate{
                .command = "fdupes",
                .summary = "identify duplicate files in directories",
                .source = "expac+pkgfile",
                .package = "fdupes",
                .repository = "extra",
                .repository_available = true,
                .cli_capable = true,
                .provided_commands = {"fdupes"},
                .semantic_fit = 0.8868,
                .score = 0.9068,
            },
        };
    }
};

void test_semantic_fit_tier_prevents_convenience_from_overriding_precision() {
    acclorite::SearchEngine engine;
    engine.add_source(std::make_unique<SemanticGuardSource>());

    const auto result = engine.search(acclorite::Query::parse("duplicate files"), 10, true);
    expect(result.candidates.size() == 2,
           "semantic-fit guard fixture returns both candidates");
    if (result.candidates.size() != 2) {
        return;
    }

    expect(result.candidates.front().command == "fdupes",
           "precise duplicate-file tool beats installed synonym-heavy generic tool");
    expect(result.candidates.front().ranking_utility < result.candidates.back().ranking_utility,
           "semantic tier can intentionally outrank a higher convenience utility");
    expect(acclorite::ranking::semantic_fit_tier(
               acclorite::ranking::effective_semantic_fit(result.candidates.front())) >
           acclorite::ranking::semantic_fit_tier(
               acclorite::ranking::effective_semantic_fit(result.candidates.back())),
           "dedicated tool occupies a stronger semantic-fit tier");
    if (result.candidates.front().ranking) {
        expect(result.candidates.front().ranking->semantic_tier == "precise",
               "ranking diagnostics expose the semantic-fit tier");
    }
}


class GenericSearchSpecializationSource final : public acclorite::KnowledgeSource {
public:
    [[nodiscard]] bool available() const override { return true; }

    [[nodiscard]] std::vector<acclorite::Candidate> search(const acclorite::Query&) const override {
        return {
            acclorite::Candidate{
                .command = "rg",
                .summary = "recursively search the current directory for lines matching a pattern",
                .source = "path+man",
                .installed = true,
                .cli_capable = true,
                .semantic_fit = 0.8604,
                .score = 0.9154,
            },
            acclorite::Candidate{
                .command = "pdfgrep",
                .summary = "A tool to search text in PDF files",
                .source = "expac+pkgfile",
                .package = "pdfgrep",
                .repository = "extra",
                .repository_available = true,
                .cli_capable = true,
                .provided_commands = {"pdfgrep"},
                .semantic_fit = 0.8868,
                .score = 0.9068,
            },
        };
    }
};

void test_unrequested_pdf_specialization_does_not_beat_generic_text_search() {
    acclorite::SearchEngine engine;
    engine.add_source(std::make_unique<GenericSearchSpecializationSource>());

    const auto generic = engine.search(acclorite::Query::parse("search text"), 10, true);
    expect(generic.candidates.size() == 2,
           "generic-search specialization fixture returns both candidates");
    if (generic.candidates.size() != 2) {
        return;
    }
    expect(generic.candidates.front().command == "rg",
           "generic text search prefers general tool over unrequested PDF-only tool");
    const auto& pdf = generic.candidates.back();
    expect(acclorite::ranking::effective_semantic_fit(
               acclorite::Query::parse("search text"), pdf) < pdf.semantic_fit,
           "unrequested PDF domain reduces effective semantic fit before tiering");
    if (pdf.ranking) {
        expect(!pdf.ranking->semantic_adjustments.empty(),
               "ranking diagnostics expose the PDF specialization adjustment");
    }

    auto pdf_query = acclorite::Query::parse("search pdf text");
    pdf_query.frame = acclorite::query::recognize_frame(pdf_query);
    acclorite::Candidate pdf_candidate{
        .command = "pdfgrep",
        .summary = "A tool to search text in PDF files",
        .source = "expac+pkgfile",
        .package = "pdfgrep",
        .repository_available = true,
        .cli_capable = true,
        .provided_commands = {"pdfgrep"},
        .semantic_fit = 0.8868,
        .score = 0.9068,
    };
    expect(std::abs(acclorite::ranking::effective_semantic_fit(pdf_query, pdf_candidate) - 0.8868) < 0.000001,
           "explicit PDF query removes PDF-domain semantic penalty");
}


void test_explicit_specialized_tool_name_bypasses_domain_penalty() {
    auto query = acclorite::Query::parse("pdfgrep");
    query.frame = acclorite::query::recognize_frame(query);
    acclorite::Candidate pdfgrep{
        .command = "pdfgrep",
        .summary = "A tool to search text in PDF files",
        .source = "expac+pkgfile",
        .package = "pdfgrep",
        .repository_available = true,
        .cli_capable = true,
        .provided_commands = {"pdfgrep"},
        .semantic_fit = 1.0,
        .score = 1.0,
    };
    expect(std::abs(acclorite::ranking::effective_semantic_fit(query, pdfgrep) - 1.0) < 0.000001,
           "explicitly named specialized tool bypasses domain penalty");
}

void test_full_text_request_keeps_search_engine_specialization_relevant() {
    auto generic = acclorite::Query::parse("search text");
    generic.frame = acclorite::query::recognize_frame(generic);
    auto full = acclorite::Query::parse("full text search");
    full.frame = acclorite::query::recognize_frame(full);

    acclorite::Candidate namazu{
        .command = "namazu",
        .summary = "Namazu is a full-text search engine intended for easy use.",
        .source = "expac",
        .repository_available = true,
        .cli_capable = true,
        .semantic_fit = 0.8868,
        .score = 0.9068,
    };

    expect(acclorite::ranking::effective_semantic_fit(generic, namazu) < 0.80,
           "generic text search discounts full-text search-engine specialization");
    expect(std::abs(acclorite::ranking::effective_semantic_fit(full, namazu) - 0.8868) < 0.000001,
           "explicit full-text request keeps search-engine specialization unpenalized");
}



class FrontDoorRoleSource final : public acclorite::KnowledgeSource {
public:
    [[nodiscard]] bool available() const override { return true; }

    [[nodiscard]] std::vector<acclorite::Candidate> search(const acclorite::Query& query) const override {
        if (query.normalized.find("extract") != std::string::npos) {
            return {
                acclorite::Candidate{
                    .command = "haskell-tar-conduit",
                    .summary = "Extract and create tar files using conduit for streaming",
                    .source = "expac",
                    .package = "haskell-tar-conduit",
                    .repository_available = true,
                    .semantic_fit = 0.8868,
                    .score = 0.9068,
                },
                acclorite::Candidate{
                    .command = "tar",
                    .summary = "archive utility for creating and extracting tar archives",
                    .source = "path+man",
                    .installed = true,
                    .cli_capable = true,
                    .semantic_fit = 0.8641,
                    .score = 0.9191,
                },
            };
        }
        return {
            acclorite::Candidate{
                .command = "fsarchiver",
                .summary = "Safe and flexible file-system backup and deployment tool",
                .source = "expac+pkgfile",
                .package = "fsarchiver",
                .repository_available = true,
                .cli_capable = true,
                .provided_commands = {"fsarchiver"},
                .semantic_fit = 0.8868,
                .score = 0.9068,
            },
            acclorite::Candidate{
                .command = "tar",
                .summary = "archive utility for creating compressed archives from directories",
                .source = "path+man",
                .installed = true,
                .cli_capable = true,
                .semantic_fit = 0.8641,
                .score = 0.9191,
            },
        };
    }
};

void test_front_door_role_constraints_prefer_user_facing_archive_tools() {
    acclorite::SearchEngine engine;
    engine.add_source(std::make_unique<FrontDoorRoleSource>());

    const auto compress = engine.search(acclorite::Query::parse("compress a folder"), 10, true);
    expect(compress.candidates.size() == 2, "archive role fixture returns both compression candidates");
    if (compress.candidates.size() == 2) {
        expect(compress.candidates.front().command == "tar",
               "generic folder compression prefers front-door archiver over filesystem backup tool");
        const auto& fsarchiver = compress.candidates.back();
        expect(acclorite::ranking::effective_semantic_fit(
                   acclorite::Query::parse("compress a folder"), fsarchiver) < 0.75,
               "unrequested backup/deployment role lowers fsarchiver semantic tier");
        if (fsarchiver.ranking) {
            const bool traced = std::ranges::any_of(fsarchiver.ranking->semantic_adjustments,
                [](const auto& adjustment) { return adjustment.id == "backup-role"; });
            expect(traced, "ranking diagnostics expose backup-role constraint");
        }
    }

    const auto extract = engine.search(acclorite::Query::parse("extract a tar.gz"), 10, true);
    expect(extract.candidates.size() == 2, "archive role fixture returns both extraction candidates");
    if (extract.candidates.size() == 2) {
        expect(extract.candidates.front().command == "tar",
               "tar extraction prefers runnable front-door tool over development library package");
        const auto& library = extract.candidates.back();
        expect(acclorite::ranking::effective_semantic_fit(
                   acclorite::Query::parse("extract a tar.gz"), library) < 0.75,
               "unrequested development-library role lowers library package semantic tier");
    }
}



class MergedFrontDoorRoleSource final : public acclorite::KnowledgeSource {
public:
    [[nodiscard]] bool available() const override { return true; }

    [[nodiscard]] std::vector<acclorite::Candidate> search(const acclorite::Query&) const override {
        return {
            acclorite::Candidate{
                .command = "fsarchiver",
                .path = "/usr/bin/fsarchiver",
                .summary = "filesystem archiver - save a filesystem to a compressed archive",
                .source = "path+man",
                .installed = true,
                .cli_capable = true,
                .semantic_fit = 0.8868,
                .score = 0.9380,
            },
            acclorite::Candidate{
                .command = "fsarchiver",
                .summary = "Safe and flexible file-system backup and deployment tool",
                .source = "expac",
                .package = "fsarchiver",
                .repository = "extra",
                .repository_available = true,
                .cli_capable = true,
                .semantic_fit = 0.8868,
                .score = 0.9068,
            },
            acclorite::Candidate{
                .command = "tar",
                .path = "/usr/bin/tar",
                .summary = "archive utility for creating compressed archives from directories",
                .source = "path+man",
                .installed = true,
                .cli_capable = true,
                .semantic_fit = 0.8641,
                .score = 0.9191,
            },
        };
    }
};

void test_merged_descriptive_evidence_preserves_package_role_metadata() {
    acclorite::SearchEngine engine;
    engine.add_source(std::make_unique<MergedFrontDoorRoleSource>());

    const auto result = engine.search(acclorite::Query::parse("compress a folder"), 10, true);
    expect(result.candidates.size() == 2, "merged role fixture returns tar and merged fsarchiver");
    if (result.candidates.size() != 2) {
        return;
    }

    expect(result.candidates.front().command == "tar",
           "package backup-role metadata survives a local-summary merge and keeps tar as front door");
    const auto fsarchiver = std::ranges::find_if(result.candidates, [](const auto& candidate) {
        return candidate.command == "fsarchiver";
    });
    expect(fsarchiver != result.candidates.end(), "merged fsarchiver candidate remains present");
    if (fsarchiver != result.candidates.end()) {
        const bool retained_package_description = std::ranges::any_of(
            fsarchiver->descriptive_evidence,
            [](const std::string& text) { return text.find("backup and deployment") != std::string::npos; }
        );
        expect(retained_package_description,
               "merged candidate preserves non-display package description for role classification");
        expect(acclorite::ranking::effective_semantic_fit(
                   acclorite::Query::parse("compress a folder"), *fsarchiver) < 0.75,
               "retained package description activates backup-role semantic constraint");
    }
}


class ArchiveFrontDoorFloorSource final : public acclorite::KnowledgeSource {
public:
    [[nodiscard]] bool available() const override { return true; }

    [[nodiscard]] std::vector<acclorite::Candidate> search(const acclorite::Query&) const override {
        return {
            acclorite::Candidate{
                .command = "storage-archiver",
                .summary = "filesystem archiver - save a filesystem to a compressed archive",
                .source = "path+man+expac+pkgfile",
                .package = "storage-archiver",
                .installed = true,
                .repository_available = true,
                .cli_capable = true,
                .semantic_fit = 0.8868,
                .score = 0.95,
            },
            acclorite::Candidate{
                .command = "general-archive",
                .summary = "GNU-style archiving utility",
                .source = "path+man",
                .installed = true,
                .cli_capable = true,
                // Intentionally weak raw fit: the synopsis omits the obvious input
                // object (files/directories), matching the real front-door recall
                // failure that kept generic archive tools below fsarchiver.
                .semantic_fit = 0.56,
                .score = 0.61,
            },
        };
    }
};

void test_folder_compression_uses_positive_archive_front_door_role_evidence() {
    auto query = acclorite::Query::parse("compress a folder");
    query.frame = acclorite::query::recognize_frame(query);

    acclorite::Candidate generic{
        .command = "general-archive",
        .summary = "GNU-style archiving utility",
        .source = "path+man",
        .installed = true,
        .cli_capable = true,
        .semantic_fit = 0.56,
        .score = 0.61,
    };
    const auto generic_assessment = acclorite::ranking::assess_semantic_fit(query, generic);
    expect(generic_assessment.effective_fit >= 0.84,
           "generic archiving front door gets strong semantic compatibility for folder compression");
    const bool floor_traced = std::ranges::any_of(
        generic_assessment.adjustments,
        [](const auto& adjustment) { return adjustment.id == "archive-front-door-role"; }
    );
    expect(floor_traced, "archive front-door semantic floor is traceable");

    acclorite::Candidate filesystem{
        .command = "storage-archiver",
        .summary = "filesystem archiver - save a filesystem to a compressed archive",
        .source = "path+man+expac+pkgfile",
        .package = "storage-archiver",
        .installed = true,
        .repository_available = true,
        .cli_capable = true,
        .semantic_fit = 0.8868,
        .score = 0.95,
    };
    const auto filesystem_assessment = acclorite::ranking::assess_semantic_fit(query, filesystem);
    expect(filesystem_assessment.effective_fit < 0.70,
           "positive archive-role evidence does not cancel filesystem-scope specialization");

    acclorite::SearchEngine engine;
    engine.add_source(std::make_unique<ArchiveFrontDoorFloorSource>());
    const auto result = engine.search(query, 10, true);
    expect(result.candidates.size() == 2, "front-door floor fixture returns both archive candidates");
    if (result.candidates.size() == 2) {
        expect(result.candidates.front().command == "general-archive",
               "generic archive front door outranks filesystem-scope archiver for folder compression");
    }
}

void test_filesystem_scope_role_does_not_require_backup_wording() {
    auto generic = acclorite::Query::parse("compress a folder");
    generic.frame = acclorite::query::recognize_frame(generic);

    acclorite::Candidate filesystem_archiver{
        .command = "storage-archiver",
        .summary = "filesystem archiver - save a filesystem to a compressed archive",
        .source = "path+man",
        .installed = true,
        .cli_capable = true,
        .semantic_fit = 0.8868,
        .score = 0.9380,
    };

    const auto generic_assessment = acclorite::ranking::assess_semantic_fit(
        generic, filesystem_archiver
    );
    expect(generic_assessment.effective_fit < 0.70,
           "generic folder compression discounts filesystem-scope archivers even without backup wording");
    const bool traced = std::ranges::any_of(generic_assessment.adjustments, [](const auto& adjustment) {
        return adjustment.id == "filesystem-scope-role";
    });
    expect(traced, "filesystem-scope role adjustment is traceable");

    auto explicit_filesystem = acclorite::Query::parse("archive a filesystem");
    explicit_filesystem.frame = acclorite::query::recognize_frame(explicit_filesystem);
    expect(std::abs(acclorite::ranking::effective_semantic_fit(
               explicit_filesystem, filesystem_archiver) - 0.8868) < 0.000001,
           "explicit filesystem intent removes filesystem-scope role constraint");
}

class ArchiveOperationRoleSource final : public acclorite::KnowledgeSource {
public:
    [[nodiscard]] bool available() const override { return true; }

    [[nodiscard]] std::vector<acclorite::Candidate> search(const acclorite::Query&) const override {
        return {
            acclorite::Candidate{
                .command = "deep-compare",
                .summary = "in-depth comparison of files, archives, and directories",
                .source = "expac+pkgfile",
                .package = "deep-compare",
                .repository_available = true,
                .cli_capable = true,
                .semantic_fit = 0.8941,
                .score = 0.9141,
            },
            acclorite::Candidate{
                .command = "tar",
                .summary = "archiving utility for creating compressed archives from directories",
                .source = "path+man",
                .installed = true,
                .cli_capable = true,
                .semantic_fit = 0.8641,
                .score = 0.9191,
            },
        };
    }
};

void test_archive_transform_intent_rejects_comparison_role() {
    acclorite::SearchEngine engine;
    engine.add_source(std::make_unique<ArchiveOperationRoleSource>());

    const auto compress = engine.search(acclorite::Query::parse("compress a folder"), 10, true);
    expect(compress.candidates.size() == 2,
           "operation-role fixture returns both archive candidates");
    if (compress.candidates.size() == 2) {
        expect(compress.candidates.front().command == "tar",
               "folder compression prefers archive creator over archive comparison tool");
        const auto comparator = std::ranges::find_if(compress.candidates, [](const auto& candidate) {
            return candidate.command == "deep-compare";
        });
        expect(comparator != compress.candidates.end(),
               "comparison-role candidate remains discoverable as an alternative");
        if (comparator != compress.candidates.end()) {
            expect(acclorite::ranking::effective_semantic_fit(
                       acclorite::Query::parse("compress a folder"), *comparator) < 0.70,
                   "archive transformation discounts a comparison-only front door");
            if (comparator->ranking) {
                const bool traced = std::ranges::any_of(
                    comparator->ranking->semantic_adjustments,
                    [](const auto& adjustment) { return adjustment.id == "operation-comparison-role"; }
                );
                expect(traced, "operation-role mismatch is visible in ranking diagnostics");
            }
        }
    }

    auto compare = acclorite::Query::parse("compare two folders");
    compare.frame = acclorite::query::recognize_frame(compare);
    acclorite::Candidate comparator{
        .command = "deep-compare",
        .summary = "in-depth comparison of files, archives, and directories",
        .source = "path+man",
        .installed = true,
        .cli_capable = true,
        .semantic_fit = 0.8868,
        .score = 0.93,
    };
    const auto compare_assessment = acclorite::ranking::assess_semantic_fit(compare, comparator);
    const bool comparison_role_applied = std::ranges::any_of(
        compare_assessment.adjustments,
        [](const auto& adjustment) { return adjustment.id == "operation-comparison-role"; }
    );
    expect(!comparison_role_applied,
           "explicit comparison intent removes the operation-role constraint");
}

void test_explicit_role_requests_remove_front_door_constraints() {
    acclorite::Candidate fsarchiver{
        .command = "fsarchiver",
        .summary = "Safe and flexible file-system backup and deployment tool",
        .source = "expac+pkgfile",
        .package = "fsarchiver",
        .repository_available = true,
        .cli_capable = true,
        .provided_commands = {"fsarchiver"},
        .semantic_fit = 0.8868,
        .score = 0.9068,
    };
    auto backup = acclorite::Query::parse("backup a filesystem");
    backup.frame = acclorite::query::recognize_frame(backup);
    expect(std::abs(acclorite::ranking::effective_semantic_fit(backup, fsarchiver) - 0.8868) < 0.000001,
           "explicit filesystem-backup intent removes backup-role constraint");

    acclorite::Candidate tar_library{
        .command = "haskell-tar-conduit",
        .summary = "Extract and create tar files using conduit for streaming",
        .source = "expac",
        .package = "haskell-tar-conduit",
        .repository_available = true,
        .semantic_fit = 0.8868,
        .score = 0.9068,
    };
    auto library_query = acclorite::Query::parse("Haskell tar library");
    library_query.frame = acclorite::query::recognize_frame(library_query);
    expect(std::abs(acclorite::ranking::effective_semantic_fit(library_query, tar_library) - 0.8868) < 0.000001,
           "explicit development-library intent removes library-role constraint");
}

void test_confidence_marks_archive_query_ambiguous_with_clarifications() {
    auto query = acclorite::Query::parse("archive files");
    query.frame = acclorite::query::recognize_frame(query);

    std::vector<acclorite::Candidate> candidates{
        acclorite::Candidate{
            .command = "cpio",
            .summary = "copy files to and from archives",
            .source = "path+man+expac+pkgfile",
            .installed = true,
            .cli_capable = true,
            .semantic_fit = 0.8868,
            .ranking_utility = 1.0605,
            .score = 1.0,
        },
        acclorite::Candidate{
            .command = "ark",
            .summary = "KDE archiving tool",
            .source = "path+man+desktop",
            .installed = true,
            .cli_capable = true,
            .gui_capable = true,
            .semantic_fit = 0.8868,
            .ranking_utility = 1.0018,
            .score = 1.0,
        },
    };

    const auto assessment = acclorite::ranking::assess_confidence(query, candidates);
    expect(assessment.confidence.ambiguity == acclorite::AmbiguityState::Ambiguous,
           "archive files is recognized as query-level ambiguity, not merely a ranking tie");
    expect(assessment.confidence.interpretation < 0.70,
           "underspecified archive operation lowers interpretation confidence");
    expect(assessment.clarifications.size() == 3,
           "archive ambiguity exposes create, extract, and manage clarification options");
    expect(assessment.confidence.top_candidate >= 0.55 && assessment.confidence.top_candidate < 0.75,
           "ambiguous archive query keeps a medium rather than high top-candidate confidence");
}

void test_confidence_distinguishes_competitive_tools_from_ambiguous_query() {
    auto query = acclorite::Query::parse("duplicate files");
    query.frame = acclorite::query::recognize_frame(query);

    std::vector<acclorite::Candidate> candidates{
        acclorite::Candidate{
            .command = "fdupes",
            .summary = "identify duplicate files",
            .source = "expac+pkgfile",
            .semantic_fit = 0.8868,
            .ranking_utility = 0.9188,
            .score = 0.9188,
        },
        acclorite::Candidate{
            .command = "rdfind",
            .summary = "find duplicate files",
            .source = "expac+pkgfile",
            .semantic_fit = 0.8868,
            .ranking_utility = 0.9188,
            .score = 0.9188,
        },
    };

    const auto assessment = acclorite::ranking::assess_confidence(query, candidates);
    expect(assessment.confidence.ambiguity == acclorite::AmbiguityState::Competitive,
           "near-equivalent duplicate-file tools are competitive, not misunderstood intent");
    expect(assessment.clarifications.empty(),
           "competitive candidates do not invent a query clarification when interpretation is clear");
    expect(assessment.confidence.interpretation >= 0.80,
           "candidate competition does not reduce interpretation confidence");
}

void test_confidence_marks_process_monitoring_clear() {
    auto query = acclorite::Query::parse("process monitoring");
    query.frame = acclorite::query::recognize_frame(query);

    std::vector<acclorite::Candidate> candidates{
        acclorite::Candidate{
            .command = "btop",
            .summary = "resource monitor for processes",
            .source = "path+man+desktop",
            .installed = true,
            .semantic_fit = 0.8868,
            .ranking_utility = 1.0018,
            .score = 1.0,
        },
        acclorite::Candidate{
            .command = "plasma-systemmonitor",
            .summary = "monitor app and process resource usage",
            .source = "path+desktop+expac+pkgfile",
            .installed = true,
            .semantic_fit = 0.8868,
            .ranking_utility = 0.9755,
            .score = 0.9755,
        },
    };

    const auto assessment = acclorite::ranking::assess_confidence(query, candidates);
    expect(assessment.confidence.ambiguity == acclorite::AmbiguityState::Clear,
           "process monitoring remains a clear interpretation despite a strong runner-up");
    expect(assessment.confidence.top_candidate >= 0.75,
           "btop-like process-monitoring winner earns high top-candidate confidence");
}

void test_confidence_marks_explicit_pdf_search_clear_and_high() {
    auto query = acclorite::Query::parse("search pdf text");
    query.frame = acclorite::query::recognize_frame(query);

    std::vector<acclorite::Candidate> candidates{
        acclorite::Candidate{
            .command = "pdfgrep",
            .summary = "search text in PDF files",
            .source = "expac+pkgfile",
            .semantic_fit = 0.8868,
            .ranking_utility = 0.9068,
            .score = 0.9068,
        },
        acclorite::Candidate{
            .command = "rg",
            .summary = "search lines recursively",
            .source = "path+man",
            .installed = true,
            .semantic_fit = 0.5513,
            .ranking_utility = 0.6623,
            .score = 0.6623,
        },
    };

    const auto assessment = acclorite::ranking::assess_confidence(query, candidates);
    expect(assessment.confidence.ambiguity == acclorite::AmbiguityState::Clear,
           "explicit PDF domain produces a clear interpretation");
    expect(assessment.confidence.top_candidate >= 0.80,
           "large semantic-tier lead yields high confidence in pdfgrep-like winner");
}

void test_confidence_marks_weak_best_match_low() {
    auto query = acclorite::Query::parse("mysterious widget frobnication");
    query.frame = acclorite::query::recognize_frame(query);
    std::vector<acclorite::Candidate> candidates{
        acclorite::Candidate{
            .command = "widget-helper",
            .summary = "generic helper",
            .source = "path",
            .semantic_fit = 0.42,
            .ranking_utility = 0.50,
            .score = 0.50,
        },
    };

    const auto assessment = acclorite::ranking::assess_confidence(query, candidates);
    expect(assessment.confidence.ambiguity == acclorite::AmbiguityState::LowConfidence,
           "weak best semantic fit is explicitly reported as low confidence");
    expect(assessment.confidence.top_candidate <= 0.58,
           "low-confidence state caps top candidate confidence");
}

class ConfidenceIntegrationSource final : public acclorite::KnowledgeSource {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::vector<acclorite::Candidate> search(const acclorite::Query&) const override {
        return {
            acclorite::Candidate{
                .command = "cpio",
                .summary = "copy files to and from archives",
                .source = "test",
                .semantic_fit = 0.8868,
                .ranking_utility = 0.94,
                .score = 0.94,
            },
            acclorite::Candidate{
                .command = "ark",
                .summary = "archive manager",
                .source = "test",
                .semantic_fit = 0.8868,
                .ranking_utility = 0.92,
                .score = 0.92,
            },
        };
    }
};

void test_search_engine_attaches_confidence_without_changing_ranking() {
    acclorite::SearchEngine engine;
    engine.add_source(std::make_unique<ConfidenceIntegrationSource>());

    const auto normal = engine.search(acclorite::Query::parse("archive files"));
    const auto explained = engine.search(acclorite::Query::parse("archive files"), 10, true);
    const auto profiled = engine.search(acclorite::Query::parse("archive files"), 10, false, true);
    expect(normal.candidates.size() == explained.candidates.size(),
           "confidence attachment does not change candidate count");
    if (normal.candidates.size() == explained.candidates.size()) {
        for (std::size_t i = 0; i < normal.candidates.size(); ++i) {
            expect(normal.candidates[i].command == explained.candidates[i].command,
                   "confidence/ranking diagnostics remain observational for ordering");
            expect(std::abs(normal.candidates[i].score - explained.candidates[i].score) < 0.000001,
                   "confidence/ranking diagnostics do not change candidate score");
        }
    }
    expect(profiled.candidates.size() == normal.candidates.size(),
           "performance profiling does not change candidate count");
    if (profiled.candidates.size() == normal.candidates.size()) {
        for (std::size_t i = 0; i < normal.candidates.size(); ++i) {
            expect(profiled.candidates[i].command == normal.candidates[i].command,
                   "performance profiling remains observational for ordering");
            expect(std::abs(profiled.candidates[i].score - normal.candidates[i].score) < 0.000001,
                   "performance profiling does not change candidate score");
        }
    }
    expect(!normal.timing.enabled, "normal search does not collect timing diagnostics");
    expect(profiled.timing.enabled && profiled.timing.total_ms >= 0.0,
           "profiled search exposes total internal search time");
    expect(!profiled.timing.stages.empty(),
           "profiled search exposes per-stage timing diagnostics");
    const auto has_profile_stage = [&](const std::string_view name) {
        return std::ranges::any_of(profiled.timing.stages, [&](const acclorite::TimingStage& stage) {
            return stage.name == name;
        });
    };
    expect(has_profile_stage("ranking-prefilter"),
           "performance profile exposes pre-enrichment ranking cost");
    expect(has_profile_stage("ranking-preferences"),
           "performance profile exposes final preference evaluation cost");
    expect(has_profile_stage("ranking-sort"),
           "performance profile exposes final numeric sort cost");
    for (const auto& candidate : profiled.candidates) {
        expect(candidate.ordering_semantic_fit >= 0.0,
               "SearchEngine returns candidates with a populated semantic ordering cache");
        expect(candidate.ordering_semantic_tier >= 0,
               "SearchEngine returns candidates with a populated semantic tier cache");
    }

    expect(normal.confidence.ambiguity == acclorite::AmbiguityState::Ambiguous,
           "SearchEngine attaches ambiguity assessment to ordinary results");
    expect(normal.clarifications.size() == 3,
           "SearchEngine carries clarification options in UI-independent result model");

    std::ostringstream profiled_json;
    acclorite::JsonRenderer{}.render(profiled, profiled_json);
    expect(profiled_json.str().find("\"timing\": {") != std::string::npos,
           "profiled JSON exposes timing diagnostics");
    expect(profiled_json.str().find("\"schema_version\": 14") != std::string::npos,
           "profiled JSON uses performance-aware schema version");

    std::ostringstream terminal;
    acclorite::TerminalRenderer{}.render(normal, terminal);
    expect(terminal.str().find("Confidence") != std::string::npos,
           "terminal output exposes confidence summary");
    expect(terminal.str().find("ambiguous") != std::string::npos,
           "terminal output exposes ambiguity state");
    expect(terminal.str().find("Clarify") != std::string::npos,
           "terminal output exposes clarification choices for ambiguous queries");

    std::ostringstream explained_terminal;
    acclorite::TerminalRenderer{}.render(explained, explained_terminal);
    expect(explained_terminal.str().find("Confidence diagnostics") != std::string::npos,
           "ranking diagnostics include detailed confidence decomposition");

    std::ostringstream json;
    acclorite::JsonRenderer{}.render(normal, json);
    expect(json.str().find("\"ambiguity\": \"ambiguous\"") != std::string::npos,
           "JSON exposes ambiguous confidence state");
    expect(json.str().find("archive-create") != std::string::npos,
           "JSON exposes deterministic clarification identifiers");
}

class GuidanceIntegrationSource final : public acclorite::KnowledgeSource {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::vector<acclorite::Candidate> search(const acclorite::Query&) const override {
        return {
            acclorite::Candidate{
                .command = "guide-tool",
                .summary = "search text in files",
                .source = "test+man",
                .installed = true,
                .cli_capable = true,
                .semantic_fit = 0.90,
                .ranking_utility = 0.90,
                .score = 0.90,
            },
            acclorite::Candidate{
                .command = "alternate-tool",
                .summary = "search text",
                .source = "test+man",
                .installed = true,
                .cli_capable = true,
                .semantic_fit = 0.86,
                .ranking_utility = 0.80,
                .score = 0.80,
            },
        };
    }
};

class FixedGuidanceProvider final : public acclorite::GuidanceProvider {
public:
    explicit FixedGuidanceProvider(int* calls) : calls_(calls) {}
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string_view diagnostic_name() const override { return "fixture"; }
    [[nodiscard]] acclorite::GuidanceBundle guide(const acclorite::Candidate& candidate) const override {
        if (calls_) {
            ++*calls_;
        }
        return acclorite::GuidanceBundle{
            .examples = {acclorite::UsageExample{
                .text = candidate.command + " --verified-example",
                .source_kind = acclorite::GuidanceSourceKind::Curated,
                .source_reference = "fixture:" + candidate.command,
                .verified = true,
            }},
            .learning_resources = {acclorite::LearningResource{
                .label = "Fixture docs",
                .target = "docs " + candidate.command,
                .source_kind = acclorite::GuidanceSourceKind::Curated,
                .source_reference = "fixture:" + candidate.command,
                .verified = true,
            }},
        };
    }
private:
    int* calls_{nullptr};
};

void test_man_guidance_extracts_only_source_backed_examples() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-man-guidance-test";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);
    write_man_page_fixture(
        temp,
        "TOOL(1)\n"
        "NAME\n"
        "    tool - fixture\n"
        "EXAMPLES\n"
        "    tool --scan ./src\n"
        "    This prose mentions tool but is not a command line.\n"
        "    $ tool --json ./src\n"
        "OPTIONS\n"
        "    --help\n"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::ManGuidanceProvider provider;
        expect(provider.available(), "man guidance provider detects local man executable");

        acclorite::Candidate candidate{
            .command = "tool",
            .summary = "fixture",
            .source = "path+man",
            .installed = true,
            .cli_capable = true,
        };
        const auto bundle = provider.guide(candidate);
        expect(bundle.learning_resources.size() == 1,
               "local man evidence creates one verified learning resource");
        expect(bundle.examples.size() == 2,
               "man guidance extracts command-looking lines from explicit examples section");
        if (bundle.examples.size() >= 2) {
            expect(bundle.examples[0].text == "tool --scan ./src",
                   "man guidance preserves first source-backed command verbatim");
            expect(bundle.examples[1].text == "tool --json ./src",
                   "man guidance strips shell prompt without inventing syntax");
            expect(bundle.examples[0].source_reference == "man:tool" && bundle.examples[0].verified,
                   "man example carries explicit verified provenance");
        }

        candidate.source = "path";
        const auto unproven = provider.guide(candidate);
        expect(unproven.examples.empty() && unproven.learning_resources.empty(),
               "man guidance refuses to claim documentation that ranking sources did not prove exists");
    }
    std::filesystem::remove_all(temp);
}

void test_guidance_is_post_ranking_bounded_and_machine_visible() {
    acclorite::SearchEngine baseline;
    baseline.add_source(std::make_unique<GuidanceIntegrationSource>());
    const auto without_guidance = baseline.search(acclorite::Query::parse("search text"));

    int calls = 0;
    acclorite::SearchEngine guided;
    guided.add_source(std::make_unique<GuidanceIntegrationSource>());
    guided.add_guidance_provider(std::make_unique<FixedGuidanceProvider>(&calls));
    const auto with_guidance = guided.search(acclorite::Query::parse("search text"), 10, false, true);

    expect(with_guidance.candidates.size() == without_guidance.candidates.size(),
           "post-ranking guidance does not change candidate count");
    if (with_guidance.candidates.size() == without_guidance.candidates.size()) {
        for (std::size_t i = 0; i < with_guidance.candidates.size(); ++i) {
            expect(with_guidance.candidates[i].command == without_guidance.candidates[i].command,
                   "guidance does not change ranking order");
            expect(std::abs(with_guidance.candidates[i].score - without_guidance.candidates[i].score) < 0.000001,
                   "guidance does not change scores");
        }
    }
    expect(calls == 1, "ordinary discovery enriches only the best match by default");
    expect(!with_guidance.candidates.empty() && with_guidance.candidates.front().examples.size() == 1,
           "best match receives verified example guidance");
    if (with_guidance.candidates.size() > 1) {
        expect(with_guidance.candidates[1].examples.empty(),
               "alternatives are not eagerly documentation-enriched on hot path");
    }
    expect(std::ranges::any_of(with_guidance.timing.stages, [](const acclorite::TimingStage& stage) {
        return stage.name == "guidance:fixture";
    }), "performance profile exposes bounded guidance cost");

    std::ostringstream terminal;
    acclorite::TerminalRenderer{}.render(with_guidance, terminal);
    expect(terminal.str().find("Verified example") != std::string::npos,
           "terminal result exposes verified example block");
    expect(terminal.str().find("docs guide-tool") != std::string::npos,
           "terminal result exposes learning resource target");

    std::ostringstream json;
    acclorite::JsonRenderer{}.render(with_guidance, json);
    expect(json.str().find("\"schema_version\": 14") != std::string::npos,
           "guidance fields advance search JSON schema to 14");
    expect(json.str().find("\"examples\"") != std::string::npos &&
           json.str().find("\"learning_resources\"") != std::string::npos,
           "JSON exposes UI-independent guidance collections");
    expect(json.str().find("fixture:guide-tool") != std::string::npos,
           "JSON preserves guidance provenance");
}

void test_process_plural_is_canonical_not_broken_s_stem() {
    auto query = acclorite::Query::parse("process monitoring");
    query.frame = acclorite::query::recognize_frame(query);
    query.explain_ranking = true;

    const auto match = acclorite::query::score_text(
        query,
        "btop",
        "Resource monitor that shows usage and stats for processor, memory, disks, network, and processes."
    );
    expect(match.breakdown.has_value(), "process morphology fixture emits semantic trace");
    if (!match.breakdown) {
        return;
    }

    const auto process = std::find_if(
        match.breakdown->concepts.begin(), match.breakdown->concepts.end(),
        [](const auto& concept_trace) { return concept_trace.term == "process"; }
    );
    expect(process != match.breakdown->concepts.end(), "process concept is traced");
    if (process != match.breakdown->concepts.end()) {
        expect(process->kind == acclorite::query::SemanticMatchKind::Canonical,
               "processes is a canonical morphological match for process");
        expect(process->quality >= 0.93,
               "process plural keeps canonical description quality");
    }
}

void test_json_renderer_escapes() {
    acclorite::SearchResult result;
    result.raw_query = "find \"thing\"";
    result.normalized_query = result.raw_query;
    result.frame = acclorite::query::recognize_frame(acclorite::Query::parse(result.raw_query));
    result.targets = {"tool"};
    result.locations.push_back(acclorite::LocationHit{
        .path = "/tmp/tool.conf",
        .kind = "config",
        .source = "test",
        .score = 0.9,
    });
    result.candidates.push_back(acclorite::Candidate{
        .command = "tool",
        .path = "/tmp/tool",
        .summary = "line1\nline2",
        .source = "test",
        .package = "tool-package",
        .repository = "extra",
        .package_version = "1.2.3-1",
        .installed = true,
        .repository_available = false,
        .matched_terms = {},
        .score = 1.0,
    });

    std::ostringstream out;
    acclorite::JsonRenderer{}.render(result, out);
    const auto json = out.str();

    expect(json.find("find \\\"thing\\\"") != std::string::npos, "JSON escapes quotes");
    expect(json.find("line1\\nline2") != std::string::npos, "JSON escapes newlines");
    expect(json.find("\"schema_version\": 14") != std::string::npos, "JSON schema advances for performance profiling output");
    expect(json.find("\"ranking\": null") != std::string::npos, "normal JSON emits null ranking diagnostics when not requested");
    expect(json.find("\"query_frame\"") != std::string::npos, "JSON includes query frame object");
    expect(json.find("\"confidence\"") != std::string::npos, "JSON includes confidence object");
    expect(json.find("\"clarifications\"") != std::string::npos, "JSON includes clarification array");
    expect(json.find("\"ambiguity\": \"no-result\"") != std::string::npos,
           "default result confidence serializes ambiguity state");
    expect(json.find("\"targets\": [\"tool\"]") != std::string::npos, "JSON includes resolved targets");
    expect(json.find("/tmp/tool.conf") != std::string::npos, "JSON includes location results");
    expect(json.find("\"type\": \"Discover\"") != std::string::npos, "JSON exposes recognized frame type");
    expect(json.find("\"package\": \"tool-package\"") != std::string::npos, "JSON includes package name");
    expect(json.find("\"repository\": \"extra\"") != std::string::npos, "JSON includes repository");
    expect(json.find("\"package_version\": \"1.2.3-1\"") != std::string::npos, "JSON includes package version");
}

} // namespace

int main() {
    // Unit tests that provide fake expac/pacman binaries must never consult a
    // developer machine's persistent Arch package snapshot. The dedicated
    // cache test explicitly opts back in with isolated sync/cache paths.
    ScopedEnv disable_arch_cache("ACCLORITE_DISABLE_ARCH_CACHE", "1");

    test_semantic_breakdown_exposes_weighted_concept_matches();
    test_base_merge_trace_exposes_preference_headroom_loss();
    test_same_source_variants_do_not_double_vote();
    test_arch_repo_variants_do_not_self_corrobate();
    test_pkgfile_provenance_is_not_semantic_corroboration();
    test_ranking_utility_preserves_order_beyond_display_clamp();
    test_semantic_fit_tier_prevents_convenience_from_overriding_precision();
    test_unrequested_pdf_specialization_does_not_beat_generic_text_search();
    test_explicit_specialized_tool_name_bypasses_domain_penalty();
    test_full_text_request_keeps_search_engine_specialization_relevant();
    test_front_door_role_constraints_prefer_user_facing_archive_tools();
    test_merged_descriptive_evidence_preserves_package_role_metadata();
    test_folder_compression_uses_positive_archive_front_door_role_evidence();
    test_filesystem_scope_role_does_not_require_backup_wording();
    test_archive_transform_intent_rejects_comparison_role();
    test_explicit_role_requests_remove_front_door_constraints();
    test_confidence_marks_archive_query_ambiguous_with_clarifications();
    test_confidence_distinguishes_competitive_tools_from_ambiguous_query();
    test_confidence_marks_process_monitoring_clear();
    test_confidence_marks_explicit_pdf_search_clear_and_high();
    test_confidence_marks_weak_best_match_low();
    test_search_engine_attaches_confidence_without_changing_ranking();
    test_man_guidance_extracts_only_source_backed_examples();
    test_guidance_is_post_ranking_bounded_and_machine_visible();
    test_process_plural_is_canonical_not_broken_s_stem();
    test_ranking_breakdown_tracks_actual_preference_steps();
    test_search_engine_ranking_diagnostics_are_opt_in_and_renderable();
    test_preference_prior_favors_documented_canonical_front_door();
    test_preference_prior_demotes_specialized_grep_for_generic_search();
    test_package_projection_penalizes_multi_binary_suite_metadata();
    test_generic_text_search_infers_file_content_context();
    test_installed_bonus_is_small_not_absolute();
    test_search_engine_prefers_documented_socket_front_door_over_path_only_helper();
    test_arch_suite_metadata_does_not_beat_direct_socket_docs();
    test_arch_full_text_package_does_not_beat_local_rg_for_generic_search();
    test_search_engine_prefers_general_text_search_over_compressed_wrapper();
    test_source_provenance_uses_exact_tokens();
    test_query_normalization();
    test_query_frame_recognizer_handles_normal_and_cursed_questions();
    test_question_words_do_not_override_actual_action();
    test_explain_frame_suppresses_noun_state_inspection_default();
    test_query_frame_targets_extract_named_entities();
    test_explain_frame_resolves_exact_entity_before_substring_noise();
    test_compare_frame_resolves_named_tools_instead_of_compare_command();
    test_locate_frame_finds_existing_bounded_config_paths();
    test_human_disk_space_query_preserves_consumption_relation();
    test_port_owner_human_relation_prefers_owner_lookup_over_monitor();
    test_tar_gz_format_intent_prefers_tar_family_over_generic_extractor();
    test_task_compare_is_discovery_not_entity_compare();
    test_benchmark_driven_subsystem_specializations_are_query_relative();
    test_inspect_relations_infer_process_for_port_owner_query();
    test_inspect_relations_infer_process_for_memory_hog_query();
    test_diagnose_frame_promotes_named_affected_tool();
    test_lexicon_expands_deterministically();
    test_fuzzy_typo_recovery();
    test_action_weight_beats_generic_object_match();
    test_specific_subject_beats_generic_usage_match();
    test_path_source_recovers_command_typo();
    test_path_source_exact_match();
    test_path_multiword_match_is_weak_evidence();
    test_man_source_discovers_by_concepts();
    test_multiword_semantics_beat_name_coincidence();
    test_man_source_filters_non_command_sections();
    test_network_connection_concepts_prefer_socket_tool();
    test_noun_heavy_state_query_infers_read_only_inspection();
    test_explicit_edit_overrides_read_only_default();
    test_duplicate_action_infers_low_weight_file_context();
    test_sources_merge_instead_of_replacing_evidence();
    test_desktop_metadata_marks_hybrid_tools();
    test_pacman_source_discovers_uninstalled_arch_tool();
    test_expac_sync_catalog_cache_reuses_local_snapshot_and_invalidates_on_sync_change();
    test_expac_cache_rejects_old_schema_and_changed_executable_identity();
    test_pacman_metadata_merges_with_installed_tool();
    test_expac_is_preferred_for_machine_formatted_arch_metadata();
    test_pkgfile_maps_package_name_to_actual_command();
    test_pkgfile_retries_unqualified_package_for_arch_derivative_repo();
    test_pkgfile_mapped_package_merges_with_local_binary();
#if ACCLORITE_HAS_SQLITE
    test_index_applies_implicit_inspection_intent();
    test_index_duplicate_default_context_beats_message_catalog();
    test_index_persists_semantic_catalog();
    test_index_keeps_hybrid_metadata();
    test_index_typo_query_uses_canonical_concepts();
    test_index_recovers_misspelled_command_name();
    test_index_fingerprints_detect_path_change_and_auto_refresh();
    test_fresh_index_does_not_rebuild_on_every_probe();
    test_index_fingerprints_detect_manual_and_desktop_changes();
#endif
    test_doctor_is_read_only_when_index_is_missing();
    test_doctor_reports_stale_index_without_refreshing_it();
    test_doctor_distinguishes_pkgfile_binary_from_metadata_readiness();
    test_doctor_json_is_machine_readable_and_declares_no_mutation();
    test_json_renderer_escapes();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "All Acclorite tests passed.\n";
    return 0;
}
