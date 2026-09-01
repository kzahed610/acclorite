#include <algorithm>
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
#include "acclorite/output/json_renderer.hpp"
#include "acclorite/query/lexicon.hpp"
#include "acclorite/sources/desktop_source.hpp"
#include "acclorite/sources/index_source.hpp"
#include "acclorite/sources/man_source.hpp"
#include "acclorite/sources/path_source.hpp"

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

void test_query_normalization() {
    const auto query = acclorite::Query::parse("  Find   DUPLICATE  Files  ");
    expect(query.normalized == "find duplicate files", "query normalization");
    expect(query.tokens.size() == 3, "query tokenization");
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

#endif

void test_json_renderer_escapes() {
    acclorite::SearchResult result;
    result.raw_query = "find \"thing\"";
    result.normalized_query = result.raw_query;
    result.candidates.push_back(acclorite::Candidate{
        .command = "tool",
        .path = "/tmp/tool",
        .summary = "line1\nline2",
        .source = "test",
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
}

} // namespace

int main() {
    test_query_normalization();
    test_lexicon_expands_deterministically();
    test_path_source_exact_match();
    test_path_multiword_match_is_weak_evidence();
    test_man_source_discovers_by_concepts();
    test_multiword_semantics_beat_name_coincidence();
    test_man_source_filters_non_command_sections();
    test_network_connection_concepts_prefer_socket_tool();
    test_sources_merge_instead_of_replacing_evidence();
    test_desktop_metadata_marks_hybrid_tools();
#if ACCLORITE_HAS_SQLITE
    test_index_persists_semantic_catalog();
    test_index_keeps_hybrid_metadata();
#endif
    test_json_renderer_escapes();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "All Acclorite tests passed.\n";
    return 0;
}
