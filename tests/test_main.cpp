#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <sys/stat.h>

#include "acclorite/core/query.hpp"
#include "acclorite/core/search_engine.hpp"
#include "acclorite/output/json_renderer.hpp"
#include "acclorite/sources/path_source.hpp"

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_query_normalization() {
    const auto query = acclorite::Query::parse("  Find   DUPLICATE  Files  ");
    expect(query.normalized == "find duplicate files", "query normalization");
    expect(query.tokens.size() == 3, "query tokenization");
}

void test_path_source_exact_match() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-path";
    std::filesystem::create_directories(temp);

    const auto executable = temp / "acclorite-test-tool";
    {
        std::ofstream file(executable);
        file << "#!/bin/sh\nexit 0\n";
    }
    ::chmod(executable.c_str(), 0755);

    const char* old_path_raw = std::getenv("PATH");
    const std::string old_path = old_path_raw ? old_path_raw : "";
    ::setenv("PATH", temp.c_str(), 1);

    acclorite::SearchEngine engine;
    engine.add_source(std::make_unique<acclorite::PathSource>());

    const auto result = engine.search(acclorite::Query::parse("acclorite-test-tool"));
    expect(!result.candidates.empty(), "PATH source finds executable");
    if (!result.candidates.empty()) {
        expect(result.candidates.front().command == "acclorite-test-tool", "exact PATH match ranks first");
        expect(result.candidates.front().score == 1.0, "exact PATH match has full score");
    }

    ::setenv("PATH", old_path.c_str(), 1);
    std::filesystem::remove_all(temp);
}

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
    test_path_source_exact_match();
    test_json_renderer_escapes();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "All Acclorite tests passed.\n";
    return 0;
}
