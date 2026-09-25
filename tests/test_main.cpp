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

#include "acclorite/answer/binder.hpp"
#include "acclorite/answer/composer.hpp"
#include "acclorite/answer/safety_classifier.hpp"
#include "acclorite/core/query.hpp"
#include "acclorite/core/search_engine.hpp"
#include "acclorite/diagnostics/doctor.hpp"
#include "acclorite/output/json_renderer.hpp"
#include "acclorite/output/terminal_renderer.hpp"
#include "acclorite/output/shell_renderer.hpp"
#include "acclorite/output/doctor_json_renderer.hpp"
#include "acclorite/output/doctor_terminal_renderer.hpp"
#include "acclorite/query/fuzzy.hpp"
#include "acclorite/query/frame.hpp"
#include "acclorite/query/action_target.hpp"
#include "acclorite/query/targets.hpp"
#include "acclorite/query/lexicon.hpp"
#include "acclorite/query/relevance.hpp"
#include "acclorite/ranking/preference.hpp"
#include "acclorite/ranking/confidence.hpp"
#include "acclorite/sources/apt_source.hpp"
#include "acclorite/sources/desktop_source.hpp"
#include "acclorite/sources/dnf_source.hpp"
#include "acclorite/sources/zypper_source.hpp"
#include "acclorite/sources/xbps_source.hpp"
#include "acclorite/sources/index_source.hpp"
#include "acclorite/sources/filesystem_location_source.hpp"
#include "acclorite/sources/man_source.hpp"
#include "acclorite/sources/pacman_source.hpp"
#include "acclorite/sources/path_source.hpp"
#include "acclorite/sources/pkgfile_enricher.hpp"
#include "acclorite/system/distro.hpp"
#include "acclorite/guidance/man_provider.hpp"
#include "acclorite/guidance/info_provider.hpp"
#include "acclorite/guidance/tldr_provider.hpp"
#include "acclorite/guidance/curated_provider.hpp"
#include "acclorite/guidance/provider.hpp"
#include "acclorite/syntax/man_provider.hpp"
#include "acclorite/syntax/fish_completion_provider.hpp"
#include "acclorite/syntax/grammar_merge.hpp"
#include "acclorite/syntax/provider.hpp"

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

void write_apt_cache_fixture(
    const std::filesystem::path& directory,
    const std::string& search_output,
    const bool stats_ready = true,
    const std::string& show_output = {}
) {
    std::string script =
        "#!/bin/sh\n"
        "if [ \"$1\" = \"stats\" ]; then\n"
        "  " + std::string(stats_ready ? "printf '%s\\n' 'Total package names: 123'\n  exit 0\n"
                                  : "exit 100\n") +
        "fi\n"
        "if [ \"$1\" = \"search\" ]; then\n";
    std::istringstream lines(search_output);
    std::string line;
    while (std::getline(lines, line)) {
        std::string escaped;
        for (const char ch : line) {
            if (ch == '\\' || ch == '\"' || ch == '$' || ch == '`') {
                escaped.push_back('\\');
            }
            escaped.push_back(ch);
        }
        script += "  printf '%s\\n' \"" + escaped + "\"\n";
    }
    script += "  exit 0\nfi\n";
    script += "if [ \"$1\" = \"show\" ]; then\n";
    std::istringstream show_lines(show_output);
    while (std::getline(show_lines, line)) {
        std::string escaped;
        for (const char ch : line) {
            if (ch == '\\' || ch == '\"' || ch == '$' || ch == '`') {
                escaped.push_back('\\');
            }
            escaped.push_back(ch);
        }
        script += "  printf '%s\\n' \"" + escaped + "\"\n";
    }
    script += "  exit 0\nfi\nexit 2\n";
    write_executable(directory / "apt-cache", script);
}

void write_dpkg_query_fixture(const std::filesystem::path& directory, const std::string& output) {
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
    write_executable(directory / "dpkg-query", script);
}


void write_dnf_fixture(
    const std::filesystem::path& directory,
    const std::string& frontend,
    const std::string& search_output,
    const std::string& repoquery_output,
    const std::filesystem::path& forbidden_marker = {}
) {
    std::string script =
        "#!/bin/sh\n"
        "if [ \"$1\" != \"--cacheonly\" ]; then\n";
    if (!forbidden_marker.empty()) {
        script += "  printf x > '" + forbidden_marker.string() + "'\n";
    }
    script +=
        "  exit 99\n"
        "fi\n"
        "shift\n"
        "if [ \"$1\" = \"search\" ]; then\n";

    std::istringstream search_lines(search_output);
    std::string line;
    while (std::getline(search_lines, line)) {
        std::string escaped;
        for (const char ch : line) {
            if (ch == '\\' || ch == '"' || ch == '$' || ch == '`') {
                escaped.push_back('\\');
            }
            escaped.push_back(ch);
        }
        script += "  printf '%s\\n' \"" + escaped + "\"\n";
    }
    script +=
        "  exit 0\n"
        "fi\n"
        "if [ \"$1\" = \"repoquery\" ]; then\n";
    std::istringstream repo_lines(repoquery_output);
    while (std::getline(repo_lines, line)) {
        std::string escaped;
        for (const char ch : line) {
            if (ch == '\\' || ch == '"' || ch == '$' || ch == '`') {
                escaped.push_back('\\');
            }
            escaped.push_back(ch);
        }
        script += "  printf '%s\\n' \"" + escaped + "\"\n";
    }
    script += "  exit 0\nfi\n";
    if (!forbidden_marker.empty()) {
        script += "printf x > '" + forbidden_marker.string() + "'\n";
    }
    script += "exit 99\n";
    write_executable(directory / frontend, script);
}


void write_zypper_fixture(
    const std::filesystem::path& directory,
    const std::string& search_xml,
    const std::string& details_xml,
    const std::filesystem::path& forbidden_marker = {}
) {
    auto shell_escape = [](const std::string& value) {
        std::string out;
        for (const char ch : value) {
            if (ch == '\\' || ch == '"' || ch == '$' || ch == '`') {
                out.push_back('\\');
            }
            out.push_back(ch);
        }
        return out;
    };

    std::string script =
        "#!/bin/sh\n"
        "fail() {\n";
    if (!forbidden_marker.empty()) {
        script += "  printf x > '" + forbidden_marker.string() + "'\n";
    }
    script +=
        "  exit 99\n"
        "}\n"
        "case \" $* \" in *\" --no-refresh \"*) ;; *) fail ;; esac\n"
        "case \" $* \" in *\" --non-interactive \"*) ;; *) fail ;; esac\n"
        "case \" $* \" in *\" --xmlout \"*) ;; *) fail ;; esac\n"
        "case \" $* \" in *\" --ignore-unknown \"*) ;; *) fail ;; esac\n"
        "cfg=''\nprev=''\n"
        "for arg in \"$@\"; do\n"
        "  if [ \"$prev\" = '--config' ]; then cfg=\"$arg\"; fi\n"
        "  prev=\"$arg\"\n"
        "done\n"
        "[ -n \"$cfg\" ] || fail\n"
        "policy=0\n"
        "while IFS= read -r line; do\n"
        "  case \"$line\" in *runSearchPackages*never*) policy=1 ;; esac\n"
        "done < \"$cfg\"\n"
        "[ \"$policy\" = 1 ] || fail\n"
        "case \" $* \" in *\" refresh \"*|*\" install \"*|*\" update \"*|*\" remove \"*|*\" dist-upgrade \"*) fail ;; esac\n"
        "case \" $* \" in *\" search \"*) ;; *) fail ;; esac\n"
        "case \" $* \" in\n"
        "  *\" --details \"*)\n";
    std::istringstream details(details_xml);
    std::string line;
    while (std::getline(details, line)) {
        script += "    printf '%s\\n' \"" + shell_escape(line) + "\"\n";
    }
    script += "    ;;\n  *)\n";
    std::istringstream search(search_xml);
    while (std::getline(search, line)) {
        script += "    printf '%s\\n' \"" + shell_escape(line) + "\"\n";
    }
    script += "    ;;\nesac\nexit 0\n";
    write_executable(directory / "zypper", script);
}


void write_xbps_fixture(
    const std::filesystem::path& directory,
    const std::string& search_output,
    const std::string& names_output,
    const std::string& versions_output,
    const std::filesystem::path& forbidden_marker = {},
    const bool repositories_ready = true,
    const std::string& installed_output = "[*] xbps-0.60.7_1"
) {
    auto shell_escape = [](const std::string& value) {
        std::string out;
        for (const char ch : value) {
            if (ch == '\\' || ch == '"' || ch == '$' || ch == '`') {
                out.push_back('\\');
            }
            out.push_back(ch);
        }
        return out;
    };
    const auto append_lines = [&](std::string& script, const std::string& output, const std::string& indent) {
        std::istringstream lines(output);
        std::string line;
        while (std::getline(lines, line)) {
            script += indent + "printf '%s\\n' \"" + shell_escape(line) + "\"\n";
        }
    };

    std::string query_script =
        "#!/bin/sh\n"
        "fail() {\n";
    if (!forbidden_marker.empty()) {
        query_script += "  printf x > '" + forbidden_marker.string() + "'\n";
    }
    query_script +=
        "  exit 99\n"
        "}\n"
        "for arg in \"$@\"; do\n"
        "  case \"$arg\" in -M|--memory-sync|--repository|--repository=*) fail ;; esac\n"
        "done\n"
        "if [ \"$1\" = '-L' ]; then\n";
    if (repositories_ready) {
        query_script += "  printf '%s\\n' '123 https://repo-default.voidlinux.org/current'\n";
    } else {
        query_script += "  printf '%s\\n' '-1 https://repo-default.voidlinux.org/current'\n";
    }
    query_script +=
        "  exit 0\n"
        "fi\n"
        "if [ \"$1\" = '-l' ]; then\n";
    append_lines(query_script, installed_output, "  ");
    query_script +=
        "  exit 0\n"
        "fi\n"
        "has_regex=0\nhas_search=0\n"
        "for arg in \"$@\"; do\n"
        "  [ \"$arg\" = '--regex' ] && has_regex=1\n"
        "  [ \"$arg\" = '-Rs' ] && has_search=1\n"
        "done\n"
        "[ \"$has_regex\" = 1 ] && [ \"$has_search\" = 1 ] || fail\n";
    append_lines(query_script, search_output, "");
    query_script += "exit 0\n";
    write_executable(directory / "xbps-query", query_script);

    std::string helper_script =
        "#!/bin/sh\n"
        "case \"$1\" in\n"
        "  getpkgname)\n";
    append_lines(helper_script, names_output, "    ");
    helper_script += "    exit 0 ;;\n  getpkgversion)\n";
    append_lines(helper_script, versions_output, "    ");
    helper_script += "    exit 0 ;;\n  *) exit 99 ;;\nesac\n";
    write_executable(directory / "xbps-uhelper", helper_script);
}

void write_rpm_fixture(const std::filesystem::path& directory, const std::string& output) {
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
    script += "exit 0\n";
    write_executable(directory / "rpm", script);
}

void write_os_release_fixture(
    const std::filesystem::path& path,
    const std::string& id,
    const std::string& id_like = {}
) {
    std::ofstream file(path);
    file << "ID=" << id << '\n';
    if (!id_like.empty()) {
        file << "ID_LIKE=\"" << id_like << "\"\n";
    }
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

void write_info_page_fixture(const std::filesystem::path& directory, const std::string& output) {
    std::string script =
        "#!/bin/sh\n"
        "if [ \"$1\" = \"--where\" ]; then\n"
        "  printf '%s\\n' '/tmp/tool.info'\n"
        "  exit 0\n"
        "fi\n";
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
    write_executable(directory / "info", script);
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

void write_man_pages_fixture(
    const std::filesystem::path& directory,
    const std::vector<std::pair<std::string, std::string>>& pages
) {
    auto shell_escape = [](const std::string& value) {
        std::string out;
        for (const char ch : value) {
            if (ch == '\\' || ch == '"' || ch == '$' || ch == '`') {
                out.push_back('\\');
            }
            out.push_back(ch);
        }
        return out;
    };

    std::string script = "#!/bin/sh\npage=\"$2\"\ncase \"$page\" in\n";
    for (const auto& [name, output] : pages) {
        script += "  " + shell_escape(name) + ")\n";
        std::istringstream lines(output);
        std::string line;
        while (std::getline(lines, line)) {
            script += "    printf '%s\\n' \"" + shell_escape(line) + "\"\n";
        }
        script += "    exit 0\n    ;;\n";
    }
    script += "  *) exit 1 ;;\nesac\n";
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
    expect_frame("what flag makes curl follow redirects", acclorite::QueryFrame::Explain, true,
                 "what-flag capability question is explanation");
    expect_frame("which rg flag includes hidden files", acclorite::QueryFrame::Explain, true,
                 "which-flag capability question is explanation");
    expect_frame("which git subcommand creates a branch", acclorite::QueryFrame::Explain, true,
                 "which-subcommand capability question is explanation");
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
    expect_frame("What is that command that can extract tar.gz files in terminal",
                 acclorite::QueryFrame::Discover, true,
                 "half-remembered what-is-that-command phrasing remains discovery");
    expect_frame("what can compare these two directories and tell me what's different",
                 acclorite::QueryFrame::Discover, false,
                 "task-object compare phrasing is discovery rather than entity comparison");
    expect_frame("who tf owns port 3000", acclorite::QueryFrame::Inspect, true,
                 "slang ownership question still resolves to inspection");
    expect_frame("what terminal thing tells me which process stole port 8080",
                 acclorite::QueryFrame::Inspect, true,
                 "metaphorical port-ownership wording still resolves to inspection");
    expect_frame("git make a fresh branch called feature/foo and put me on it",
                 acclorite::QueryFrame::Modify, true,
                 "command-addressed branch creation sentence resolves to modification");
    expect_frame("fuzzy directory jumping thing", acclorite::QueryFrame::Discover, false,
                 "unframed human request safely defaults to discovery");
}

void test_action_target_parser_separates_command_syntax_from_language() {
    {
        const auto query = acclorite::Query::parse("what does curl -L do");
        const auto target = acclorite::query::detect_action_target(query);
        expect(target.has_value() && target->kind == acclorite::ActionTargetKind::Option,
               "explicit short option becomes a first-class option target");
        if (target) {
            expect(target->command == "curl" && target->literal == "-L" && target->explicit_syntax,
                   "explicit option target preserves parent command and case-sensitive literal syntax");
        }
    }

    {
        const auto query = acclorite::Query::parse("what does rg -. do");
        const auto target = acclorite::query::detect_action_target(query);
        expect(target.has_value() && target->kind == acclorite::ActionTargetKind::Option,
               "punctuation short option becomes a first-class option target");
        if (target) {
            expect(target->command == "rg" && target->literal == "-." && target->explicit_syntax,
                   "raw syntax tokenization preserves punctuation-valued short options exactly");
        }
    }

    {
        const auto query = acclorite::Query::parse("what flag makes curl follow redirects");
        const auto target = acclorite::query::detect_action_target(query);
        expect(target.has_value() && target->kind == acclorite::ActionTargetKind::Option,
               "natural-language flag request becomes a first-class option target");
        if (target) {
            expect(target->command == "curl" && !target->literal && !target->explicit_syntax,
                   "semantic option target resolves the parent command without inventing flag syntax");
            expect(target->terms.size() == 2 && target->terms[0] == "follow" && target->terms[1] == "redirects",
                   "semantic option target keeps capability terms separate from question scaffolding");
        }
    }

    {
        const auto query = acclorite::Query::parse("which rg flag includes hidden files");
        const auto target = acclorite::query::detect_action_target(query);
        expect(target.has_value() && target->command == "rg",
               "flag parser handles parent command immediately before the flag noun");
        if (target) {
            expect(target->terms.size() == 2 && target->terms[0] == "hidden" && target->terms[1] == "files",
                   "generic include/enabling language does not pollute capability matching terms");
        }
    }

    {
        const auto query = acclorite::Query::parse("which git subcommand creates a branch");
        const auto target = acclorite::query::detect_action_target(query);
        expect(target.has_value() && target->kind == acclorite::ActionTargetKind::Subcommand &&
                   target->command == "git",
               "subcommands are structurally recognizable before subcommand grammar parsing lands");
        if (target) {
            expect(target->terms.size() == 2 && target->terms[0] == "creates" && target->terms[1] == "branch",
                   "subcommand target noun stays structural instead of leaking into capability terms");
        }
    }

    {
        const auto query = acclorite::Query::parse("which git subcommands create branches");
        const auto target = acclorite::query::detect_action_target(query);
        expect(target.has_value() && target->kind == acclorite::ActionTargetKind::Subcommand &&
                   target->command == "git",
               "plural subcommand noun produces the same structured target kind");
    }

    {
        const auto query = acclorite::Query::parse("rg is skipping dotfiles, make it search them too");
        const auto target = acclorite::query::detect_action_target(query);
        expect(target.has_value() && target->kind == acclorite::ActionTargetKind::Operation &&
                   target->command == "rg",
               "command-addressed human sentence becomes an implicit operation target");
        if (target) {
            expect(target->terms.size() == 2 && target->terms[0] == "hidden" &&
                       target->terms[1] == "files",
                   "dotfile wording recovers compact hidden-files capability evidence");
        }
    }

    {
        const auto query = acclorite::Query::parse(
            "curl keeps getting 3xx responses and stopping, make it follow the redirect"
        );
        const auto target = acclorite::query::detect_action_target(query);
        expect(target.has_value() && target->kind == acclorite::ActionTargetKind::Operation &&
                   target->command == "curl",
               "named command is anchored without requiring the user to say flag");
        if (target) {
            expect(target->terms.size() == 2 && target->terms[0] == "follow" &&
                       target->terms[1] == "redirect",
                   "redirect complaint recovers grammar-facing operation terms");
        }
    }

    {
        const auto query = acclorite::Query::parse("ffmpeg start reading this video from 30 seconds in");
        const auto target = acclorite::query::detect_action_target(query);
        expect(target.has_value() && target->kind == acclorite::ActionTargetKind::Operation &&
                   target->command == "ffmpeg",
               "command-first seek request is preserved as an implicit operation");
        if (target) {
            expect(target->terms.size() == 2 && target->terms[0] == "seek" &&
                       target->terms[1] == "position",
                   "time-offset language recovers seek-position capability evidence");
        }
    }
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

void test_failed_index_rebuild_preserves_last_good_snapshot() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-index-rebuild-recovery";
    const auto bin = temp / "bin";
    const auto apps = temp / "applications";
    const auto db = temp / "cache/index.db";
    const auto staging = std::filesystem::path(db.string() + ".rebuild");
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(bin);
    std::filesystem::create_directories(apps);

    write_executable(bin / "ark", "#!/bin/sh\nexit 0\n");
    write_apropos_fixture(bin, "ark (1) - create and extract archive files\n");

    {
        ScopedPath scoped_path(bin);
        ScopedEnv desktop_dirs("ACCLORITE_DESKTOP_DIRS", apps.string());
        ScopedEnv index_path("ACCLORITE_INDEX_PATH", db.string());

        acclorite::IndexSource source;
        expect(source.rebuild(), "recovery fixture initial rebuild succeeds");
        const auto before = acclorite::IndexSource::probe();
        expect(before.ready, "recovery fixture starts with a valid index");
        std::ifstream before_file(db, std::ios::binary);
        const std::string before_bytes(
            (std::istreambuf_iterator<char>(before_file)), std::istreambuf_iterator<char>()
        );
        expect(!before_bytes.empty(), "recovery fixture captures the live database bytes");

        // Simulate a refresh environment that can no longer produce any catalog records.
        // The old implementation deleted the live database before discovering this failure.
        std::filesystem::remove(bin / "ark");
        std::filesystem::remove(bin / "apropos");
        {
            std::ofstream orphan(staging);
            orphan << "interrupted old staging file";
        }

        expect(!source.rebuild(), "empty-catalog rebuild reports failure");
        expect(std::filesystem::exists(db), "failed rebuild preserves the live database");
        expect(!std::filesystem::exists(staging), "failed rebuild cleans its staging database");
        expect(!std::filesystem::exists(staging.string() + "-wal"),
               "failed rebuild cleans staging WAL sidecar");
        expect(!std::filesystem::exists(staging.string() + "-shm"),
               "failed rebuild cleans staging shared-memory sidecar");

        const auto after = acclorite::IndexSource::probe();
        expect(after.ready, "failed rebuild leaves the previous snapshot structurally ready");
        expect(after.stale, "preserved snapshot is honestly reported as stale after source drift");

        std::ifstream after_file(db, std::ios::binary);
        const std::string after_bytes(
            (std::istreambuf_iterator<char>(after_file)), std::istreambuf_iterator<char>()
        );
        expect(after_bytes == before_bytes,
               "failed rebuild preserves the last known-good database byte-for-byte");
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

void test_distro_detection_and_package_family_selection() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-distro-detection";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    const auto cachyos = temp / "cachyos-release";
    write_os_release_fixture(cachyos, "cachyos", "arch");
    const auto arch = acclorite::system::detect_distro(cachyos);
    expect(arch.family == acclorite::PackageFamily::Arch,
           "CachyOS os-release resolves to the Arch package family");

    const auto ubuntu = temp / "ubuntu-release";
    write_os_release_fixture(ubuntu, "ubuntu", "debian");
    const auto debian = acclorite::system::detect_distro(ubuntu);
    expect(debian.family == acclorite::PackageFamily::Debian,
           "Ubuntu os-release resolves to the Debian package family");

    const auto fedora_release = temp / "fedora-release";
    write_os_release_fixture(fedora_release, "fedora", "rhel");
    const auto fedora = acclorite::system::detect_distro(fedora_release);
    expect(fedora.family == acclorite::PackageFamily::Fedora,
           "Fedora os-release resolves to the Fedora package family");

    const auto opensuse_release = temp / "opensuse-release";
    write_os_release_fixture(opensuse_release, "opensuse-tumbleweed", "suse opensuse");
    const auto suse = acclorite::system::detect_distro(opensuse_release);
    expect(suse.family == acclorite::PackageFamily::Suse,
           "openSUSE os-release resolves to the SUSE package family");

    const auto void_release = temp / "void-release";
    write_os_release_fixture(void_release, "void");
    const auto void_linux = acclorite::system::detect_distro(void_release);
    expect(void_linux.family == acclorite::PackageFamily::Void,
           "Void os-release resolves to the Void package family");

    const acclorite::system::PackageBackendAvailability all{
        .arch = true,
        .debian = true,
        .fedora = true,
        .suse = true,
        .void_linux = true,
    };
    expect(acclorite::system::choose_package_family(debian, all) == acclorite::PackageFamily::Debian,
           "Debian os-release breaks a three-backend tie in favor of APT");
    expect(acclorite::system::choose_package_family(fedora, all) == acclorite::PackageFamily::Fedora,
           "Fedora os-release breaks a three-backend tie in favor of DNF");
    expect(acclorite::system::choose_package_family(arch, all) == acclorite::PackageFamily::Arch,
           "Arch os-release breaks a four-backend tie in favor of pacman");
    expect(acclorite::system::choose_package_family(suse, all) == acclorite::PackageFamily::Suse,
           "openSUSE os-release breaks a five-backend tie in favor of Zypper");
    expect(acclorite::system::choose_package_family(void_linux, all) == acclorite::PackageFamily::Void,
           "Void os-release breaks a five-backend tie in favor of XBPS");

    acclorite::system::DistroInfo unknown;
    expect(acclorite::system::choose_package_family(
               unknown, {.arch = false, .debian = true, .fedora = false}) ==
               acclorite::PackageFamily::Debian,
           "a sole apt backend remains usable in a minimal chroot without os-release metadata");
    expect(acclorite::system::choose_package_family(
               unknown, {.arch = true, .debian = false, .fedora = false}) ==
               acclorite::PackageFamily::Arch,
           "a sole pacman backend remains usable in a minimal chroot without os-release metadata");
    expect(acclorite::system::choose_package_family(
               unknown, {.arch = false, .debian = false, .fedora = true, .suse = false}) ==
               acclorite::PackageFamily::Fedora,
           "a sole DNF backend remains usable in a minimal chroot without os-release metadata");
    expect(acclorite::system::choose_package_family(
               unknown, {.arch = false, .debian = false, .fedora = false, .suse = true}) ==
               acclorite::PackageFamily::Suse,
           "a sole Zypper backend remains usable in a minimal chroot without os-release metadata");
    expect(acclorite::system::choose_package_family(
               unknown, {.arch = false, .debian = false, .fedora = false, .suse = false, .void_linux = true}) ==
               acclorite::PackageFamily::Void,
           "a sole XBPS backend remains usable in a minimal chroot without os-release metadata");
    expect(acclorite::system::choose_package_family(
               unknown, {.arch = false, .debian = true, .fedora = true}) ==
               acclorite::PackageFamily::Unknown,
           "an unknown distro with multiple package backends remains conservative instead of guessing");

    std::filesystem::remove_all(temp);
}

void test_package_backends_share_common_interface() {
    std::unique_ptr<acclorite::PackageBackend> arch = std::make_unique<acclorite::PacmanSource>();
    std::unique_ptr<acclorite::PackageBackend> debian = std::make_unique<acclorite::AptSource>();
    std::unique_ptr<acclorite::PackageBackend> fedora = std::make_unique<acclorite::DnfSource>();
    std::unique_ptr<acclorite::PackageBackend> suse = std::make_unique<acclorite::ZypperSource>();
    std::unique_ptr<acclorite::PackageBackend> void_linux = std::make_unique<acclorite::XbpsSource>();
    expect(arch->package_family() == acclorite::PackageFamily::Arch,
           "PacmanSource satisfies the common Arch package backend contract");
    expect(debian->package_family() == acclorite::PackageFamily::Debian,
           "AptSource satisfies the common Debian package backend contract");
    expect(fedora->package_family() == acclorite::PackageFamily::Fedora,
           "DnfSource satisfies the common Fedora package backend contract");
    expect(suse->package_family() == acclorite::PackageFamily::Suse,
           "ZypperSource satisfies the common openSUSE package backend contract");
    expect(void_linux->package_family() == acclorite::PackageFamily::Void,
           "XbpsSource satisfies the common Void package backend contract");
    expect(arch->backend_id() == "pacman" && debian->backend_id() == "apt" &&
               fedora->backend_id() == "dnf" && suse->backend_id() == "zypper" &&
               void_linux->backend_id() == "xbps",
           "package backends expose stable backend identifiers");
}

void test_apt_source_discovers_uninstalled_debian_tool_offline() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-apt-uninstalled";
    const auto marker = temp / "apt-get-called";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_apt_cache_fixture(
        temp,
        "fdupes - Identifies or deletes duplicate files within specified directories\n"
        "gettext - GNU internationalization utilities\n",
        true,
        "Package: fdupes\nVersion: 2.3.0-1\n\n"
    );
    write_executable(
        temp / "apt-get",
        "#!/bin/sh\nprintf x > '" + marker.string() + "'\nexit 99\n"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::AptSource source;
        expect(source.available(), "fake apt-cache capability is detected");
        const auto candidates = source.search(acclorite::Query::parse("duplicate files"));
        expect(!candidates.empty(), "APT backend returns repository candidate from local metadata");
        if (!candidates.empty()) {
            const auto& best = candidates.front();
            expect(best.command == "fdupes", "APT description discovers fdupes");
            expect(!best.installed, "uninstalled APT package stays uninstalled");
            expect(best.repository_available, "APT package is marked repository-available");
            expect(best.package == "fdupes", "APT package identity is retained");
            expect(best.package_version == "2.3.0-1", "APT cached repository version is retained");
            expect(best.source == "apt", "APT provenance is retained");
        }
        expect(!std::filesystem::exists(marker),
               "APT repository discovery never invokes apt-get or performs a package update");
    }

    std::filesystem::remove_all(temp);
}

void test_apt_installed_metadata_merges_with_local_tool() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-apt-merge";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_executable(temp / "jq", "#!/bin/sh\nexit 0\n");
    write_apt_cache_fixture(
        temp,
        "jq - lightweight and flexible command-line JSON processor\n",
        true,
        "Package: jq\nVersion: 1.7.1-4\n\n"
    );
    write_dpkg_query_fixture(temp, "jq\t1.7.1-3\tinstall ok installed\n");

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PathSource>());
        engine.add_source(std::make_unique<acclorite::AptSource>());

        const auto result = engine.search(acclorite::Query::parse("jq"));
        expect(!result.candidates.empty(), "local jq and APT metadata merge");
        if (!result.candidates.empty()) {
            const auto& best = result.candidates.front();
            expect(best.command == "jq", "merged Debian package result remains jq");
            expect(best.installed, "dpkg installed state survives package merge");
            expect(best.repository_available, "APT repository availability survives package merge");
            expect(best.path == (temp / "jq").string(), "PATH survives APT package merge");
            expect(best.package == "jq", "APT package metadata survives merge");
            expect(best.package_version == "1.7.1-4", "APT repository version is retained while dpkg supplies installed state");
            expect(best.source.find("path") != std::string::npos, "merged result keeps PATH provenance");
            expect(best.source.find("apt") != std::string::npos, "merged result keeps APT provenance");
        }
    }

    std::filesystem::remove_all(temp);
}


void test_dnf_source_discovers_uninstalled_fedora_tool_offline() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-dnf-uninstalled";
    const auto marker = temp / "forbidden-dnf-call";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_dnf_fixture(
        temp,
        "dnf5",
        "================ Name & Summary Matched: duplicate files =================\n"
        "fdupes.x86_64: Identifies or deletes duplicate files within specified directories\n"
        "gettext.x86_64   GNU internationalization utilities\n",
        "fdupes\t2.3.0-6.fc42\tfedora\tIdentifies or deletes duplicate files within specified directories\n",
        marker
    );
    // If both frontends exist, DNF5 must win deterministically. Invoking this
    // dnf4 trap would prove frontend selection leaked or became order-dependent.
    write_executable(
        temp / "dnf",
        "#!/bin/sh\nprintf x > '" + marker.string() + "'\nexit 99\n"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::DnfSource source;
        expect(source.available(), "fake DNF5 capability is detected");
        expect(acclorite::DnfSource::frontend() == std::optional<std::string>{"dnf5"},
               "DNF5 is preferred when DNF4 and DNF5 coexist");
        const auto candidates = source.search(acclorite::Query::parse("duplicate files"));
        expect(!candidates.empty(), "DNF backend returns repository candidate from cached metadata");
        if (!candidates.empty()) {
            const auto& best = candidates.front();
            expect(best.command == "fdupes", "DNF summary discovers fdupes");
            expect(!best.installed, "uninstalled DNF package stays uninstalled");
            expect(best.repository_available, "DNF package is marked repository-available");
            expect(best.package == "fdupes", "DNF package identity is retained");
            expect(best.repository == "fedora", "DNF repository identity is retained");
            expect(best.package_version == "2.3.0-6.fc42", "DNF cached repository EVR is retained");
            expect(best.source == "dnf", "DNF provenance is retained");
        }
        expect(!std::filesystem::exists(marker),
               "DNF repository discovery always uses --cacheonly and never invokes mutation paths");
    }

    std::filesystem::remove_all(temp);
}

void test_dnf4_frontend_fallback_and_installed_metadata_merge() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-dnf4-merge";
    const auto marker = temp / "forbidden-dnf-call";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_executable(temp / "jq", "#!/bin/sh\nexit 0\n");
    write_dnf_fixture(
        temp,
        "dnf",
        "jq.x86_64 : Lightweight and flexible command-line JSON processor\n",
        "jq\t1.7.1-8.fc41\tupdates\tLightweight and flexible command-line JSON processor\n",
        marker
    );
    write_rpm_fixture(temp, "jq\t1.7.1-7.fc41\n");

    {
        ScopedPath scoped_path(temp);
        expect(acclorite::DnfSource::frontend() == std::optional<std::string>{"dnf"},
               "DNF4 is used when DNF5 is absent");

        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PathSource>());
        engine.add_source(std::make_unique<acclorite::DnfSource>());

        const auto result = engine.search(acclorite::Query::parse("jq"));
        expect(!result.candidates.empty(), "local jq and DNF metadata merge");
        if (!result.candidates.empty()) {
            const auto& best = result.candidates.front();
            expect(best.command == "jq", "merged Fedora package result remains jq");
            expect(best.installed, "RPM installed state survives DNF package merge");
            expect(best.repository_available, "DNF repository availability survives package merge");
            expect(best.path == (temp / "jq").string(), "PATH survives DNF package merge");
            expect(best.package == "jq", "DNF package metadata survives merge");
            expect(best.repository == "updates", "DNF repository metadata survives merge");
            expect(best.package_version == "1.7.1-8.fc41",
                   "DNF repository EVR is retained while RPM supplies installed state");
            expect(best.source.find("path") != std::string::npos,
                   "merged result keeps PATH provenance");
            expect(best.source.find("dnf") != std::string::npos,
                   "merged result keeps DNF provenance");
        }
        expect(!std::filesystem::exists(marker),
               "DNF4 fallback stays cache-only and read-only");
    }

    std::filesystem::remove_all(temp);
}


void test_zypper_source_discovers_uninstalled_opensuse_tool_offline() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-zypper-uninstalled";
    const auto marker = temp / "forbidden-zypper-call";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_zypper_fixture(
        temp,
        "<?xml version=\"1.0\"?>\n<stream><search-result><solvable-list>\n"
        "<solvable status=\"not-installed\" name=\"fdupes\" summary=\"Identifies &amp; deletes duplicate files\" kind=\"package\"/>\n"
        "<solvable status=\"not-installed\" name=\"gettext\" summary=\"GNU translations\" kind=\"package\"/>\n"
        "</solvable-list></search-result></stream>\n",
        "<?xml version=\"1.0\"?>\n<stream><search-result><solvable-list>\n"
        "<solvable status=\"not-installed\" name=\"fdupes\" kind=\"package\" edition=\"2.3.0-1.2\" arch=\"x86_64\" repository=\"repo-oss\"/>\n"
        "</solvable-list></search-result></stream>\n",
        marker
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::ZypperSource source;
        expect(source.available(), "fake zypper plus bundled read-only config are detected");
        const auto candidates = source.search(acclorite::Query::parse("duplicate files"));
        expect(!candidates.empty(), "Zypper backend returns repository candidate from local metadata");
        if (!candidates.empty()) {
            const auto& best = candidates.front();
            expect(best.command == "fdupes", "Zypper summary discovers fdupes");
            expect(!best.installed, "uninstalled Zypper package stays uninstalled");
            expect(best.repository_available, "Zypper package is marked repository-available");
            expect(best.package == "fdupes", "Zypper package identity is retained");
            expect(best.package_version == "2.3.0-1.2", "Zypper cached repository edition is retained");
            expect(best.repository == "repo-oss", "Zypper repository identity is retained");
            expect(best.summary.find("&") != std::string::npos,
                   "Zypper XML entities are decoded in package summaries");
            expect(best.source == "zypper", "Zypper provenance is retained");
        }
        expect(!std::filesystem::exists(marker),
               "Zypper discovery forces read-only config, --no-refresh, and query-only commands");
    }
    std::filesystem::remove_all(temp);
}

void test_zypper_installed_metadata_merges_with_path() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-zypper-merge";
    const auto marker = temp / "forbidden-zypper-call";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_executable(temp / "jq", "#!/bin/sh\nexit 0\n");
    write_zypper_fixture(
        temp,
        "<stream><search-result><solvable-list>\n"
        "<solvable status=\"installed\" name=\"jq\" summary=\"Command-line JSON processor\" kind=\"package\"/>\n"
        "</solvable-list></search-result></stream>\n",
        "<stream><search-result><solvable-list>\n"
        "<solvable status=\"other-version\" name=\"jq\" kind=\"package\" edition=\"1.6-1.1\" arch=\"x86_64\" repository=\"repo-oss\"/>\n"
        "<solvable status=\"not-installed\" name=\"jq\" kind=\"package\" edition=\"1.7.1-2.1\" arch=\"x86_64\" repository=\"repo-update\"/>\n"
        "<solvable status=\"installed\" name=\"jq\" kind=\"package\" edition=\"1.7-1.1\" arch=\"x86_64\" repository=\"@System\"/>\n"
        "</solvable-list></search-result></stream>\n",
        marker
    );
    write_rpm_fixture(temp, "jq\t1.7-1.1\n");

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PathSource>());
        engine.add_source(std::make_unique<acclorite::ZypperSource>());
        const auto result = engine.search(acclorite::Query::parse("jq json processor"));
        expect(!result.candidates.empty(), "PATH + Zypper search returns jq");
        if (!result.candidates.empty()) {
            const auto& best = result.candidates.front();
            expect(best.command == "jq", "merged Zypper/PATH candidate keeps jq identity");
            expect(best.installed, "RPM/PATH evidence marks Zypper candidate installed");
            expect(best.path == (temp / "jq").string(), "PATH survives Zypper package merge");
            expect(best.repository == "repo-update", "Zypper prefers repository-backed current candidate over @System");
            expect(best.repository_available, "repository-backed Zypper detail remains installable");
            expect(best.package_version == "1.7.1-2.1", "Zypper keeps current repository edition");
            expect(best.source.find("path") != std::string::npos &&
                       best.source.find("zypper") != std::string::npos,
                   "merged result keeps PATH and Zypper provenance");
        }
        expect(!std::filesystem::exists(marker), "Zypper merge stays query-only and no-refresh");
    }
    std::filesystem::remove_all(temp);
}


void test_xbps_source_discovers_uninstalled_void_tool_offline() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-xbps-uninstalled";
    const auto marker = temp / "forbidden-xbps-call";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_xbps_fixture(
        temp,
        "[-] fdupes-2.3.0_1 Identifies or deletes duplicate files within specified directories\n"
        "[-] gettext-0.22_2 GNU internationalization utilities\n",
        "fdupes\ngettext\n",
        "2.3.0_1\n0.22_2\n",
        marker
    );
    write_executable(
        temp / "xbps-install",
        "#!/bin/sh\nprintf x > '" + marker.string() + "'\nexit 99\n"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::XbpsSource source;
        expect(source.available(), "fake xbps-query/xbps-uhelper capability is detected");
        const auto candidates = source.search(acclorite::Query::parse("duplicate files"));
        expect(!candidates.empty(), "XBPS backend returns repository candidate from synchronized metadata");
        if (!candidates.empty()) {
            const auto& best = candidates.front();
            expect(best.command == "fdupes", "XBPS short description discovers fdupes");
            expect(!best.installed, "uninstalled XBPS package stays uninstalled");
            expect(best.repository_available, "XBPS package is marked repository-available");
            expect(best.package == "fdupes", "XBPS package identity is retained");
            expect(best.package_version == "2.3.0_1", "XBPS repository version/revision is retained");
            expect(best.source == "xbps", "XBPS provenance is retained");
        }
        expect(!std::filesystem::exists(marker),
               "XBPS discovery never uses --memory-sync, explicit remote repositories, or xbps-install");
    }
    std::filesystem::remove_all(temp);
}

void test_xbps_installed_metadata_merges_with_path() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-xbps-merge";
    const auto marker = temp / "forbidden-xbps-call";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);

    write_executable(temp / "jq", "#!/bin/sh\nexit 0\n");
    write_xbps_fixture(
        temp,
        "[*] jq-1.7.1_2 Lightweight and flexible command-line JSON processor\n",
        "jq\n",
        "1.7.1_2\n",
        marker,
        true,
        "[*] jq-1.7.1_1"
    );

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<acclorite::PathSource>());
        engine.add_source(std::make_unique<acclorite::XbpsSource>());
        const auto result = engine.search(acclorite::Query::parse("jq json processor"));
        expect(!result.candidates.empty(), "PATH + XBPS search returns jq");
        if (!result.candidates.empty()) {
            const auto& best = result.candidates.front();
            expect(best.command == "jq", "merged XBPS/PATH candidate keeps jq identity");
            expect(best.installed, "XBPS installed marker/PATH evidence marks candidate installed");
            expect(best.path == (temp / "jq").string(), "PATH survives XBPS package merge");
            expect(best.repository_available, "XBPS repository availability survives package merge");
            expect(best.package_version == "1.7.1_2", "XBPS repository pkgver survives package merge");
            expect(best.source.find("path") != std::string::npos &&
                       best.source.find("xbps") != std::string::npos,
                   "merged result keeps PATH and XBPS provenance");
        }
        expect(!std::filesystem::exists(marker), "XBPS merge remains synchronized-cache-only and read-only");
    }
    std::filesystem::remove_all(temp);
}

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

void test_doctor_reports_debian_backend_readiness() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-doctor-apt";
    const auto bin = temp / "bin";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(bin);

    write_apt_cache_fixture(bin, "fdupes - identify duplicate files\n", true);
    write_dpkg_query_fixture(bin, "dpkg\t1.22.0\tinstall ok installed\n");
    const auto os_release = temp / "os-release";
    write_os_release_fixture(os_release, "ubuntu", "debian");

    {
        ScopedPath scoped_path(bin);
        ScopedEnv distro_file("ACCLORITE_OS_RELEASE", os_release.string());
        ScopedEnv index_path("ACCLORITE_INDEX_PATH", (temp / "index.db").string());
        ScopedEnv desktop_dirs("ACCLORITE_DESKTOP_DIRS", (temp / "no-applications").string());
        const auto report = acclorite::diagnostics::Doctor{}.run();
        const auto* apt = find_doctor_check(report, "apt-cache");
        const auto* dpkg = find_doctor_check(report, "dpkg-query");
        const auto* search = find_doctor_check(report, "apt-search");
        expect(apt != nullptr && apt->state == acclorite::diagnostics::DoctorState::Ready,
               "Doctor reports readable local APT package metadata");
        expect(dpkg != nullptr && dpkg->state == acclorite::diagnostics::DoctorState::Ready,
               "Doctor reports readable dpkg installed-package state");
        expect(search != nullptr && search->state == acclorite::diagnostics::DoctorState::Ready,
               "Doctor reports Debian repository package discovery ready");
        expect(find_doctor_check(report, "arch-search") == nullptr,
               "Debian Doctor output does not clutter the report with inactive Arch checks");
    }

    std::filesystem::remove_all(temp);
}


void test_doctor_reports_fedora_backend_readiness() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-doctor-dnf";
    const auto bin = temp / "bin";
    const auto marker = temp / "forbidden-dnf-call";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(bin);

    write_dnf_fixture(
        bin,
        "dnf5",
        "rpm.x86_64 : RPM package manager\n",
        "rpm\t4.19.1.1-3.fc42\tfedora\tRPM package manager\n",
        marker
    );
    write_rpm_fixture(bin, "rpm\t4.19.1.1-2.fc42\n");
    const auto os_release = temp / "os-release";
    write_os_release_fixture(os_release, "fedora", "rhel");

    {
        ScopedPath scoped_path(bin);
        ScopedEnv distro_file("ACCLORITE_OS_RELEASE", os_release.string());
        ScopedEnv index_path("ACCLORITE_INDEX_PATH", (temp / "index.db").string());
        ScopedEnv desktop_dirs("ACCLORITE_DESKTOP_DIRS", (temp / "no-applications").string());
        const auto report = acclorite::diagnostics::Doctor{}.run();
        const auto* cache = find_doctor_check(report, "dnf-cache");
        const auto* rpm = find_doctor_check(report, "rpm-query");
        const auto* search = find_doctor_check(report, "dnf-search");
        expect(cache != nullptr && cache->state == acclorite::diagnostics::DoctorState::Ready,
               "Doctor reports readable cache-only DNF metadata");
        expect(rpm != nullptr && rpm->state == acclorite::diagnostics::DoctorState::Ready,
               "Doctor reports readable RPM installed-package state");
        expect(search != nullptr && search->state == acclorite::diagnostics::DoctorState::Ready,
               "Doctor reports Fedora repository package discovery ready");
        expect(find_doctor_check(report, "arch-search") == nullptr,
               "Fedora Doctor output does not include inactive Arch checks");
        expect(find_doctor_check(report, "apt-search") == nullptr,
               "Fedora Doctor output does not include inactive Debian checks");
        expect(!std::filesystem::exists(marker),
               "Fedora Doctor probe uses --cacheonly and no mutation path");
    }

    std::filesystem::remove_all(temp);
}


void test_doctor_reports_opensuse_backend_readiness() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-doctor-zypper";
    const auto bin = temp / "bin";
    const auto marker = temp / "forbidden-zypper-call";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(bin);

    write_zypper_fixture(
        bin,
        "<stream><search-result><solvable-list><solvable status=\"installed\" name=\"rpm\" summary=\"RPM package manager\" kind=\"package\"/></solvable-list></search-result></stream>\n",
        "<stream><search-result><solvable-list><solvable status=\"installed\" name=\"rpm\" kind=\"package\" edition=\"4.20.1-1.1\" arch=\"x86_64\" repository=\"repo-oss\"/></solvable-list></search-result></stream>\n",
        marker
    );
    write_rpm_fixture(bin, "rpm\t4.20.1-1.1\n");
    const auto os_release = temp / "os-release";
    write_os_release_fixture(os_release, "opensuse-tumbleweed", "suse opensuse");

    {
        ScopedPath scoped_path(bin);
        ScopedEnv distro_file("ACCLORITE_OS_RELEASE", os_release.string());
        ScopedEnv index_path("ACCLORITE_INDEX_PATH", (temp / "index.db").string());
        ScopedEnv desktop_dirs("ACCLORITE_DESKTOP_DIRS", (temp / "no-applications").string());
        const auto report = acclorite::diagnostics::Doctor{}.run();
        const auto* cache = find_doctor_check(report, "zypper-cache");
        const auto* rpm = find_doctor_check(report, "rpm-query");
        const auto* search = find_doctor_check(report, "zypper-search");
        expect(cache != nullptr && cache->state == acclorite::diagnostics::DoctorState::Ready,
               "Doctor reports readable no-refresh Zypper metadata");
        expect(rpm != nullptr && rpm->state == acclorite::diagnostics::DoctorState::Ready,
               "Doctor reports readable RPM state on openSUSE");
        expect(search != nullptr && search->state == acclorite::diagnostics::DoctorState::Ready,
               "Doctor reports openSUSE repository discovery ready");
        expect(find_doctor_check(report, "arch-search") == nullptr &&
                   find_doctor_check(report, "apt-search") == nullptr &&
                   find_doctor_check(report, "dnf-search") == nullptr,
               "openSUSE Doctor output excludes inactive Arch/Debian/Fedora checks");
        expect(!std::filesystem::exists(marker),
               "openSUSE Doctor uses the read-only Zypper config and --no-refresh");
    }
    std::filesystem::remove_all(temp);
}


void test_doctor_reports_void_backend_readiness() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-test-doctor-xbps";
    const auto bin = temp / "bin";
    const auto marker = temp / "forbidden-xbps-call";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(bin);

    write_xbps_fixture(
        bin,
        "[*] xbps-0.60.7_1 XBPS package manager\n",
        "xbps\n",
        "0.60.7_1\n",
        marker,
        true,
        "[*] xbps-0.60.7_1"
    );
    const auto os_release = temp / "os-release";
    write_os_release_fixture(os_release, "void");

    {
        ScopedPath scoped_path(bin);
        ScopedEnv distro_file("ACCLORITE_OS_RELEASE", os_release.string());
        ScopedEnv index_path("ACCLORITE_INDEX_PATH", (temp / "index.db").string());
        ScopedEnv desktop_dirs("ACCLORITE_DESKTOP_DIRS", (temp / "no-applications").string());
        const auto report = acclorite::diagnostics::Doctor{}.run();
        const auto* cache = find_doctor_check(report, "xbps-cache");
        const auto* pkgdb = find_doctor_check(report, "xbps-pkgdb");
        const auto* search = find_doctor_check(report, "xbps-search");
        expect(cache != nullptr && cache->state == acclorite::diagnostics::DoctorState::Ready,
               "Doctor reports readable synchronized XBPS repository indexes");
        expect(pkgdb != nullptr && pkgdb->state == acclorite::diagnostics::DoctorState::Ready,
               "Doctor reports readable XBPS installed-package state");
        expect(search != nullptr && search->state == acclorite::diagnostics::DoctorState::Ready,
               "Doctor reports Void repository discovery ready");
        expect(find_doctor_check(report, "arch-search") == nullptr &&
                   find_doctor_check(report, "apt-search") == nullptr &&
                   find_doctor_check(report, "dnf-search") == nullptr &&
                   find_doctor_check(report, "zypper-search") == nullptr,
               "Void Doctor output excludes inactive Arch/Debian/Fedora/openSUSE checks");
        expect(!std::filesystem::exists(marker),
               "Void Doctor never enables XBPS memory-sync or mutation paths");
    }
    std::filesystem::remove_all(temp);
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



void test_human_discovery_modifiers_do_not_dilute_parent_tool_intent() {
    auto query = acclorite::Query::parse("need a command for folder size but readable not raw bytes");
    query.frame = acclorite::query::recognize_frame(query);
    const auto groups = acclorite::query::concept_groups(query);

    const auto has = [&](const std::string_view term) {
        return std::ranges::any_of(groups, [&](const auto& group) { return group.term == term; });
    };
    expect(has("usage"), "folder-size phrasing retains the filesystem usage operation");
    expect(has("folder") || has("folders") || has("file") || has("files"),
           "folder-size phrasing retains filesystem object context");
    expect(!has("readable") && !has("raw") && !has("bytes"),
           "human-readable output modifiers do not become full-weight discovery subjects");
}

void test_query_relative_role_specificity_handles_hostile_human_collisions() {
    {
        auto query = acclorite::Query::parse(
            "What is that command that can extract tar.gz files in terminal"
        );
        query.frame = acclorite::query::recognize_frame(query);

        acclorite::Candidate observer{
            .command = "ptargrep",
            .summary = "Apply pattern matching to the contents of files in a tar archive",
            .source = "path+man",
            .installed = true,
            .cli_capable = true,
            .semantic_fit = 0.8634,
            .score = 0.9248,
        };
        acclorite::Candidate tar{
            .command = "tar",
            .summary = "an archiving utility",
            .source = "path+man",
            .installed = true,
            .cli_capable = true,
            .semantic_fit = 0.5913,
            .score = 0.6760,
        };

        const auto observer_fit = acclorite::ranking::assess_semantic_fit(query, observer);
        const auto tar_fit = acclorite::ranking::assess_semantic_fit(query, tar);
        expect(observer_fit.effective_fit < 0.60,
               "archive search/inspection role is demoted for explicit extraction intent");
        expect(tar_fit.effective_fit >= 0.84,
               "exact archive-format front door receives positive extraction-role evidence");
        expect(tar_fit.effective_fit > observer_fit.effective_fit,
               "tar extraction front door outranks archive-content search role semantically");
        expect(std::ranges::any_of(observer_fit.adjustments, [](const auto& adjustment) {
                   return adjustment.id == "archive-observer-role";
               }),
               "archive observer-role conflict remains traceable");
    }

    {
        auto query = acclorite::Query::parse(
            "what can compare these two directories and tell me what's different"
        );
        query.frame = acclorite::query::recognize_frame(query);

        acclorite::Candidate image_compare{
            .command = "compare",
            .summary = "compare images and annotate pixel differences",
            .source = "path+man",
            .package = "imagemagick",
            .installed = true,
            .cli_capable = true,
            .semantic_fit = 0.55,
            .score = 0.62,
        };
        acclorite::Candidate diff{
            .command = "diff",
            .summary = "compare files line by line and report differences",
            .source = "path+man",
            .installed = true,
            .cli_capable = true,
            .semantic_fit = 0.41,
            .score = 0.48,
        };

        const auto image_fit = acclorite::ranking::assess_semantic_fit(query, image_compare);
        const auto diff_fit = acclorite::ranking::assess_semantic_fit(query, diff);
        expect(image_fit.effective_fit < 0.40,
               "image comparison domain is demoted for directory comparison intent");
        expect(diff_fit.effective_fit >= 0.84,
               "file comparison front door receives positive directory-comparison evidence");
        expect(diff_fit.effective_fit > image_fit.effective_fit,
               "file comparison role beats same-spelled image comparison command");
    }

    {
        auto query = acclorite::Query::parse("turn a bunch of png files into jpg from terminal");
        query.frame = acclorite::query::recognize_frame(query);

        acclorite::Candidate text_renderer{
            .command = "img2txt",
            .summary = "convert images to ANSI and ASCII text for terminal display",
            .source = "path+man",
            .installed = true,
            .cli_capable = true,
            .semantic_fit = 0.58,
            .score = 0.67,
        };
        acclorite::Candidate converter{
            .command = "convert",
            .summary = "convert between image formats",
            .source = "path+man",
            .installed = true,
            .cli_capable = true,
            .semantic_fit = 0.44,
            .score = 0.52,
        };

        const auto text_fit = acclorite::ranking::assess_semantic_fit(query, text_renderer);
        const auto converter_fit = acclorite::ranking::assess_semantic_fit(query, converter);
        expect(text_fit.effective_fit < 0.55,
               "text/terminal renderer is demoted for image-to-image format conversion");
        expect(converter_fit.effective_fit >= 0.84,
               "image-format converter receives positive format-conversion evidence");
        expect(converter_fit.effective_fit > text_fit.effective_fit,
               "image-to-image front door beats image-to-text renderer");
    }

    {
        auto query = acclorite::Query::parse(
            "need a command for folder size but readable not raw bytes"
        );
        query.frame = acclorite::query::recognize_frame(query);

        acclorite::Candidate object_size{
            .command = "size",
            .summary = "display object file section sizes",
            .source = "path+man",
            .installed = true,
            .cli_capable = true,
            .semantic_fit = 0.48,
            .score = 0.55,
        };
        acclorite::Candidate duf{
            .command = "duf",
            .summary = "Disk Usage/Free Utility",
            .source = "path+man",
            .installed = true,
            .cli_capable = true,
            .semantic_fit = 0.30,
            .score = 0.39,
        };

        const auto size_fit = acclorite::ranking::assess_semantic_fit(query, object_size);
        const auto duf_fit = acclorite::ranking::assess_semantic_fit(query, duf);
        expect(duf_fit.effective_fit >= 0.84,
               "filesystem-usage front door receives positive folder-size evidence");
        expect(duf_fit.effective_fit > size_fit.effective_fit,
               "filesystem usage role beats unrelated executable named size");
    }

    {
        auto query = acclorite::Query::parse(
            "need a command for folder size but readable not raw bytes"
        );
        query.frame = acclorite::query::recognize_frame(query);

        acclorite::Candidate msdos_usage{
            .command = "mdu",
            .summary = "display the amount of space occupied by an MSDOS directory",
            .source = "path+man",
            .installed = true,
            .cli_capable = true,
            .semantic_fit = 0.86,
            .score = 0.92,
        };
        acclorite::Candidate f2fs_resize{
            .command = "resize.f2fs",
            .summary = "resize filesystem size for an F2FS file system",
            .source = "path+man",
            .installed = true,
            .cli_capable = true,
            .semantic_fit = 0.86,
            .score = 0.91,
        };
        acclorite::Candidate generic_usage{
            .command = "pdu",
            .summary = "parallel disk usage directory tree analyzer",
            .source = "path+man",
            .installed = true,
            .cli_capable = true,
            .semantic_fit = 0.84,
            .score = 0.89,
        };

        const auto msdos_fit = acclorite::ranking::assess_semantic_fit(query, msdos_usage);
        const auto f2fs_fit = acclorite::ranking::assess_semantic_fit(query, f2fs_resize);
        const auto generic_fit = acclorite::ranking::assess_semantic_fit(query, generic_usage);
        expect(msdos_fit.effective_fit < 0.70,
               "generic folder-size intent demotes MS-DOS-specific usage tools");
        expect(f2fs_fit.effective_fit < 0.70,
               "generic folder-size intent demotes filesystem-specific resize tools");
        expect(generic_fit.effective_fit >= 0.84,
               "generic directory disk-usage analyzer remains a valid front door");
        expect(generic_fit.effective_fit > msdos_fit.effective_fit &&
                   generic_fit.effective_fit > f2fs_fit.effective_fit,
               "generic disk-usage role beats filesystem-format-specific lookalikes");
    }
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
    expect(profiled_json.str().find("\"schema_version\": 15") != std::string::npos,
           "profiled JSON uses performance-aware schema version");

    std::ostringstream terminal;
    acclorite::TerminalRenderer{}.render(normal, terminal);
    expect(terminal.str().find("⚠ needs clarification") != std::string::npos,
           "terminal output exposes ambiguity as a compact warning");
    expect(terminal.str().find("needs clarification") != std::string::npos,
           "normal terminal output translates the internal ambiguity state into human wording");
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

class CountingSyntaxProvider final : public acclorite::CommandSyntaxProvider {
public:
    explicit CountingSyntaxProvider(int* availability_calls, int* grammar_calls)
        : availability_calls_(availability_calls), grammar_calls_(grammar_calls) {}

    [[nodiscard]] bool available() const override {
        if (availability_calls_) {
            ++*availability_calls_;
        }
        return true;
    }
    [[nodiscard]] std::string_view diagnostic_name() const override { return "counting"; }
    [[nodiscard]] std::optional<acclorite::CommandGrammar> grammar(
        const acclorite::Candidate& candidate
    ) const override {
        if (grammar_calls_) {
            ++*grammar_calls_;
        }
        acclorite::CommandGrammar grammar;
        grammar.command = candidate.command;
        return grammar;
    }

private:
    int* availability_calls_{nullptr};
    int* grammar_calls_{nullptr};
};

void test_man_syntax_provider_extracts_verified_grammar_without_executing_target() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-man-syntax-test";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);
    const auto executed_marker = temp / "target-executed";

    write_man_page_fixture(
        temp,
        "TOOL(1)\n"
        "NAME\n"
        "    tool - fixture\n"
        "SYNOPSIS\n"
        "    tool [OPTIONS] <input>\n"
        "OPTIONS\n"
        "    -q, --quiet\n"
        "        Suppress ordinary output.\n"
        "    -o, --output <path>\n"
        "        Write the result to path.\n"
        "    --color[=WHEN]\n"
        "        Control color output.\n"
        "    -L, --location\n"
        "        Follow redi‐\n"
        "        rects to a new location.\n"
        "        --proxy-user may still be mentioned here as prose.\n"
        "COMMANDS\n"
        "    status [UNIT...|PID...]]\n"
        "        Show status for one or more units.\n"
        "    inspect [PATTERN…|PID…]\n"
        "        Show runtime status information.\n"
        "    mode start|stop\n"
        "        Select a literal mode.\n"
        "    tool-branch(1)\n"
        "        List, create, or delete branches.\n"
        "    This is category prose and must not become a subcommand.\n"
        "EXAMPLES\n"
        "    tool --quiet input\n"
    );
    write_executable(temp / "tool", "#!/bin/sh\ntouch '" + executed_marker.string() + "'\nexit 91\n");

    {
        ScopedPath scoped_path(temp);
        acclorite::ManCommandSyntaxProvider provider;
        expect(provider.available(), "man syntax provider detects local man executable");

        const acclorite::Candidate candidate{
            .command = "tool",
            .summary = "fixture",
            .source = "path+man",
            .installed = true,
            .cli_capable = true,
        };
        const auto grammar = provider.grammar(candidate);
        expect(grammar.has_value(), "man syntax provider accepts proven local manual syntax");
        if (grammar) {
            expect(grammar->command == "tool", "command grammar preserves exact candidate identity");
            expect(grammar->synopsis.size() == 1 && grammar->synopsis.front().text == "tool [OPTIONS] <input>",
                   "man syntax parser preserves conservative SYNOPSIS text");
            expect(grammar->global_options.size() == 4,
                   "man syntax parser extracts option declarations without dumping prose");

            const auto output = std::ranges::find_if(grammar->global_options, [](const acclorite::CommandOption& option) {
                return std::ranges::find(option.names, "--output") != option.names.end();
            });
            expect(output != grammar->global_options.end(), "man syntax parser preserves option aliases");
            if (output != grammar->global_options.end()) {
                expect(output->takes_value && output->value_required && output->value_name == "path",
                       "man syntax parser records directly proven required option values");
                expect(output->description == "Write the result to path.",
                       "man syntax parser associates indented option descriptions");
                expect(output->provenance.source_kind == acclorite::SyntaxSourceKind::Man &&
                           output->provenance.source_reference == "man:tool" &&
                           output->provenance.section == "OPTIONS",
                       "every accepted man option carries exact syntax provenance");
            }

            const auto color = std::ranges::find_if(grammar->global_options, [](const acclorite::CommandOption& option) {
                return std::ranges::find(option.names, "--color") != option.names.end();
            });
            expect(color != grammar->global_options.end() && color->takes_value && !color->value_required &&
                       color->value_name == "WHEN",
                   "man syntax parser distinguishes attached optional values from required ones");

            const auto location = std::ranges::find_if(grammar->global_options, [](const acclorite::CommandOption& option) {
                return std::ranges::find(option.names, "--location") != option.names.end();
            });
            expect(location != grammar->global_options.end() &&
                       location->description.find("Follow redirects to a new location.") != std::string::npos &&
                       location->description.find("--proxy-user may still be mentioned here as prose") != std::string::npos,
                   "man description joining removes groff wrap hyphens and keeps option-looking prose attached");
            expect(std::ranges::none_of(grammar->global_options, [](const acclorite::CommandOption& option) {
                return std::ranges::find(option.names, "--proxy-user") != option.names.end();
            }), "wrapped option-looking prose is not promoted into fabricated grammar");
            expect(grammar->subcommands.size() == 4,
                   "man command sections conservatively produce verified subcommand grammar");
            const auto unicode_signature = std::ranges::find_if(grammar->subcommands, [](const acclorite::SubcommandSpec& subcommand) {
                return subcommand.name == "inspect";
            });
            expect(unicode_signature != grammar->subcommands.end(),
                   "man subcommand parser accepts Unicode ellipsis in compact signatures");
            const auto status = std::ranges::find_if(grammar->subcommands, [](const acclorite::SubcommandSpec& subcommand) {
                return subcommand.name == "status";
            });
            expect(status != grammar->subcommands.end() && status->positionals.size() == 1 &&
                       status->positionals.front().name == "UNIT|PID" &&
                       !status->positionals.front().required &&
                       status->positionals.front().variadic,
                   "man subcommand parser tolerates one redundant trailing optional bracket while collapsing same-shape union slots");
            expect(unicode_signature != grammar->subcommands.end() &&
                       unicode_signature->positionals.size() == 1 &&
                       unicode_signature->positionals.front().name == "PATTERN|PID" &&
                       !unicode_signature->positionals.front().required &&
                       unicode_signature->positionals.front().variadic,
                   "Unicode-ellipsis alternative slots preserve bindable optional variadic grammar");
            const auto literal_alternatives = std::ranges::find_if(grammar->subcommands, [](const acclorite::SubcommandSpec& subcommand) {
                return subcommand.name == "mode";
            });
            expect(literal_alternatives != grammar->subcommands.end() && literal_alternatives->positionals.empty(),
                   "literal command alternatives stay unbound instead of being flattened into a positional slot");
            const auto branch = std::ranges::find_if(grammar->subcommands, [](const acclorite::SubcommandSpec& subcommand) {
                return subcommand.name == "branch";
            });
            expect(branch != grammar->subcommands.end(),
                   "man command references strip parent prefix and section suffix into subcommand identity");
            if (branch != grammar->subcommands.end()) {
                expect(branch->description.find("create") != std::string::npos &&
                           branch->provenance.source_reference == "man:tool" &&
                           branch->provenance.section == "COMMANDS",
                       "verified subcommand preserves description and exact man provenance");
            }
            expect(std::ranges::none_of(grammar->subcommands, [](const acclorite::SubcommandSpec& subcommand) {
                return subcommand.name == "This" || subcommand.name == "this";
            }), "command-section prose is never promoted into fabricated subcommand grammar");
        }

        acclorite::Candidate unproven = candidate;
        unproven.source = "path";
        expect(!provider.grammar(unproven).has_value(),
               "man syntax provider refuses grammar when existing sources did not prove a manual");
    }

    expect(!std::filesystem::exists(executed_marker),
           "syntax extraction never executes the discovered target command");
    std::filesystem::remove_all(temp);
}


void test_fish_completion_syntax_provider_parses_static_options_without_executing_shell() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-fish-syntax-test";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);
    const auto executed_marker = temp / "executed";

    {
        std::ofstream completion(temp / "guide-tool.fish");
        completion
            << "function __dangerous_fixture\n"
            << "    touch '" << executed_marker.string() << "'\n"
            << "end\n"
            << "complete -c guide-tool -s q -l quiet -d 'Suppress normal output.'\n"
            << "complete --command guide-tool --short-option o --long-option output "
               "--description 'Write exported output to a path.' --require-parameter --no-files\n"
            << "complete -c guide-tool -l color -d 'Choose color mode.' -r -a 'auto always never'\n"
            << "complete -c guide-tool -n '__fish_use_subcommand' -a 'run' -d 'Run a job.'\n"
            << "complete -c guide-tool -n '__fish_use_subcommand' -a 'sync' -d 'Synchronize state.'\n"
            << "complete -c guide-tool -n '__fish_seen_subcommand_from run' -l export -r "
               "-d 'Export run output to a file.'\n"
            << "complete -c guide-tool -n '__fish_seen_subcommand_from run sync' -xs p "
               "-d 'Select a profile for this operation.'\n"
            << "complete -c guide-tool -l ghost-context -d 'Unproven scope.' "
               "-n '__fish_seen_subcommand_from ghost'\n"
            << "complete -c guide-tool -l variable-context -d 'Variable scope.' "
               "-n '__fish_seen_subcommand_from $run'\n"
            << "complete -c guide-tool -l negated-context -d 'Negated scope.' "
               "-n 'not __fish_seen_subcommand_from run'\n"
            << "complete -c guide-tool -l chained-context -d 'Chained scope.' "
               "-n '__fish_seen_subcommand_from run; and true'\n"
            << "complete -c guide-tool -l generated -d 'Dynamic argument candidates.' "
               "-a '(__fish_complete_command)'\n"
            << "complete -c other-tool -l wrong-command -d 'Must not leak across command identity.'\n"
            << "complete -c guide-tool -l unsafe; touch '" << executed_marker.string() << "'\n";
    }
    write_executable(temp / "fish", "#!/bin/sh\ntouch '" + executed_marker.string() + "'\nexit 97\n");
    write_executable(temp / "guide-tool", "#!/bin/sh\ntouch '" + executed_marker.string() + "'\nexit 98\n");

    {
        ScopedPath scoped_path(temp);
        acclorite::FishCompletionSyntaxProvider provider({temp});
        expect(provider.available(), "Fish completion syntax provider detects configured completion roots");

        const acclorite::Candidate candidate{
            .command = "guide-tool",
            .summary = "fixture",
            .source = "path",
            .installed = true,
            .cli_capable = true,
        };
        const auto grammar = provider.grammar(candidate);
        expect(grammar.has_value(), "Fish completion provider accepts exact static completion metadata");
        if (grammar) {
            expect(grammar->command == "guide-tool",
                   "Fish completion grammar preserves exact candidate identity");
            expect(grammar->global_options.size() == 3,
                   "Fish parser keeps only understood unconditional static option declarations");

            const auto output = std::ranges::find_if(grammar->global_options, [](const acclorite::CommandOption& option) {
                return std::ranges::find(option.names, "--output") != option.names.end();
            });
            expect(output != grammar->global_options.end(),
                   "Fish parser preserves static short/long option aliases");
            if (output != grammar->global_options.end()) {
                expect(output->names.size() == 2 && output->names.front() == "-o" && output->names.back() == "--output",
                       "Fish parser renders canonical short and long option spellings");
                expect(output->value_shape_known && output->takes_value && output->value_required && output->value_name == "VALUE",
                       "Fish require-parameter metadata becomes a conservative required value slot");
                expect(output->description == "Write exported output to a path.",
                       "Fish static description text is retained as semantic evidence");
                expect(output->provenance.source_kind == acclorite::SyntaxSourceKind::Completion &&
                           output->provenance.source_reference.find("guide-tool.fish") != std::string::npos &&
                           output->provenance.section == "complete",
                       "Fish-derived syntax carries completion-file provenance");
            }

            const auto quiet = std::ranges::find_if(grammar->global_options, [](const acclorite::CommandOption& option) {
                return std::ranges::find(option.names, "--quiet") != option.names.end();
            });
            expect(quiet != grammar->global_options.end() && !quiet->value_shape_known,
                   "Fish declarations without parameter metadata preserve option identity but keep value shape unknown");

            expect(std::ranges::none_of(grammar->global_options, [](const acclorite::CommandOption& option) {
                return std::ranges::find(option.names, "--ghost-context") != option.names.end() ||
                       std::ranges::find(option.names, "--variable-context") != option.names.end() ||
                       std::ranges::find(option.names, "--negated-context") != option.names.end() ||
                       std::ranges::find(option.names, "--chained-context") != option.names.end() ||
                       std::ranges::find(option.names, "--generated") != option.names.end() ||
                       std::ranges::find(option.names, "--unsafe") != option.names.end() ||
                       std::ranges::find(option.names, "--wrong-command") != option.names.end();
            }), "unsupported conditions, dynamic arguments, shell syntax, and foreign-command declarations never leak into global grammar");

            expect(grammar->subcommands.size() == 2,
                   "Fish literal __fish_use_subcommand declarations prove exactly two subcommands");
            const auto run = std::ranges::find_if(grammar->subcommands, [](const acclorite::SubcommandSpec& subcommand) {
                return subcommand.name == "run";
            });
            const auto sync = std::ranges::find_if(grammar->subcommands, [](const acclorite::SubcommandSpec& subcommand) {
                return subcommand.name == "sync";
            });
            expect(run != grammar->subcommands.end() && sync != grammar->subcommands.end(),
                   "Fish static argument candidates preserve literal subcommand identities");
            if (run != grammar->subcommands.end()) {
                expect(run->provenance.source_kind == acclorite::SyntaxSourceKind::Completion &&
                           run->provenance.section == "complete / __fish_use_subcommand",
                       "Fish-derived subcommands carry explicit condition provenance");
                const auto export_option = std::ranges::find_if(run->options, [](const acclorite::CommandOption& option) {
                    return std::ranges::find(option.names, "--export") != option.names.end();
                });
                const auto profile_option = std::ranges::find_if(run->options, [](const acclorite::CommandOption& option) {
                    return std::ranges::find(option.names, "-p") != option.names.end();
                });
                expect(export_option != run->options.end() && export_option->value_shape_known &&
                           export_option->takes_value && export_option->value_required,
                       "literal seen-subcommand condition scopes a required-value option to the proven child");
                expect(profile_option != run->options.end() && profile_option->value_shape_known &&
                           profile_option->takes_value && profile_option->value_required,
                       "compact Fish -xs syntax is parsed as exclusive required-value scoped option metadata");
                expect(std::ranges::none_of(run->options, [](const acclorite::CommandOption& option) {
                    return std::ranges::find(option.names, "--ghost-context") != option.names.end() ||
                           std::ranges::find(option.names, "--variable-context") != option.names.end() ||
                           std::ranges::find(option.names, "--negated-context") != option.names.end() ||
                           std::ranges::find(option.names, "--chained-context") != option.names.end();
                }), "only the allowlisted positive literal condition shape may scope child options");
            }
            if (sync != grammar->subcommands.end()) {
                expect(std::ranges::any_of(sync->options, [](const acclorite::CommandOption& option) {
                    return std::ranges::find(option.names, "-p") != option.names.end();
                }), "one positive literal condition may safely scope the same option to multiple proven subcommands");
                expect(std::ranges::none_of(sync->options, [](const acclorite::CommandOption& option) {
                    return std::ranges::find(option.names, "--export") != option.names.end();
                }), "subcommand-scoped Fish options never bleed into sibling subcommands");
            }

            if (run != grammar->subcommands.end()) {
                const auto child = provider.subcommand_grammar(candidate, *run);
                expect(child.has_value() && child->name == "run" && !child->options.empty(),
                       "Fish provider exposes only parent-proven child grammar through the deep resolver contract");

                acclorite::Query scoped_query = acclorite::Query::parse(
                    "guide-tool run export output report.txt"
                );
                scoped_query.frame = acclorite::QueryFrameResult{
                    .frame = acclorite::QueryFrame::Modify,
                    .confidence = 0.99,
                    .explicit_frame = true,
                    .signals = {"fixture"},
                };
                scoped_query.action_target = acclorite::ActionTarget{
                    .kind = acclorite::ActionTargetKind::Operation,
                    .command = "guide-tool",
                    .literal = std::nullopt,
                    .terms = {"run", "export", "output"},
                    .explicit_syntax = false,
                };
                const auto answer = acclorite::AnswerComposer::compose(
                    scoped_query,
                    candidate,
                    *grammar,
                    [&](const acclorite::SubcommandSpec& subcommand) {
                        return provider.subcommand_grammar(candidate, subcommand);
                    }
                );
                expect(answer.has_value() && answer->relevant_subcommands.size() == 1 &&
                           answer->relevant_subcommands.front().name == "run" &&
                           answer->relevant_options.size() == 1 &&
                           std::ranges::find(answer->relevant_options.front().names, "--export") !=
                               answer->relevant_options.front().names.end(),
                       "scoped Fish child grammar participates in compound-operation composition");
                expect(answer && answer->invocation &&
                           answer->invocation->arguments.size() == 3 &&
                           answer->invocation->arguments[0].value == "run" &&
                           answer->invocation->arguments[1].value == "--export" &&
                           answer->invocation->arguments[2].value == "report.txt" &&
                           !answer->invocation->complete,
                       "Fish condition scope can bind a verified child option template but never claims full command completeness without synopsis proof");
            }
        }

        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<GuidanceIntegrationSource>());
        engine.add_syntax_provider(std::make_unique<acclorite::FishCompletionSyntaxProvider>(
            std::vector<std::filesystem::path>{temp}
        ));
        const auto result = engine.search(acclorite::Query::parse("what does guide-tool --output do"), 10, false, true);
        expect(result.actionable_answer.has_value() &&
                   result.actionable_answer->relevant_options.size() == 1,
               "Fish completion grammar participates in normal post-ranking actionable composition");
        if (result.actionable_answer && !result.actionable_answer->relevant_options.empty()) {
            expect(result.actionable_answer->relevant_options.front().provenance.source_kind ==
                       acclorite::SyntaxSourceKind::Completion,
                   "machine answer retains Fish completion authority instead of pretending it came from man");
        }
        expect(std::ranges::any_of(result.timing.stages, [](const acclorite::TimingStage& stage) {
            return stage.name == "syntax:fish-completion";
        }), "profiling exposes static Fish completion syntax cost separately");
    }

    expect(!std::filesystem::exists(executed_marker),
           "static Fish completion parsing never executes fish, completion code, or the target command");
    std::filesystem::remove_all(temp);
}

void test_syntax_evidence_merging_preserves_authority_and_combines_parent_child_facts() {
    {
        acclorite::CommandGrammar primary{
            .command = "tool",
            .global_options = {acclorite::CommandOption{
                .names = {"-o", "--output"},
                .description = "Write to FILE.",
                .value_name = "FILE",
                .value_shape_known = true,
                .takes_value = true,
                .value_required = true,
                .provenance = acclorite::SyntaxProvenance{
                    .source_kind = acclorite::SyntaxSourceKind::Man,
                    .source_reference = "man:tool",
                    .section = "OPTIONS",
                },
            }},
            .subcommands = {acclorite::SubcommandSpec{
                .name = "run",
                .description = "Run a job.",
                .options = {},
                .positionals = {},
                .provenance = acclorite::SyntaxProvenance{
                    .source_kind = acclorite::SyntaxSourceKind::Man,
                    .source_reference = "man:tool",
                    .section = "COMMANDS",
                },
                .synopsis = {},
            }},
        };
        const acclorite::CommandGrammar secondary{
            .command = "tool",
            .global_options = {
                acclorite::CommandOption{
                    .names = {"--output"},
                    .description = "Completion description.",
                    .value_name = std::nullopt,
                    .value_shape_known = false,
                    .takes_value = false,
                    .value_required = false,
                    .provenance = acclorite::SyntaxProvenance{
                        .source_kind = acclorite::SyntaxSourceKind::Completion,
                        .source_reference = "fish:/tmp/tool.fish",
                        .section = "complete",
                    },
                },
                acclorite::CommandOption{
                    .names = {"--color"},
                    .description = "Enable color.",
                    .value_name = std::nullopt,
                    .value_shape_known = false,
                    .takes_value = false,
                    .value_required = false,
                    .provenance = acclorite::SyntaxProvenance{
                        .source_kind = acclorite::SyntaxSourceKind::Completion,
                        .source_reference = "fish:/tmp/tool.fish",
                        .section = "complete",
                    },
                },
            },
            .subcommands = {acclorite::SubcommandSpec{
                .name = "run",
                .description = "Completion run description.",
                .options = {acclorite::CommandOption{
                    .names = {"--export"},
                    .description = "Export output.",
                    .value_name = "VALUE",
                    .value_shape_known = true,
                    .takes_value = true,
                    .value_required = true,
                    .provenance = acclorite::SyntaxProvenance{
                        .source_kind = acclorite::SyntaxSourceKind::Completion,
                        .source_reference = "fish:/tmp/tool.fish",
                        .section = "complete / __fish_seen_subcommand_from",
                    },
                }},
                .positionals = {},
                .provenance = acclorite::SyntaxProvenance{
                    .source_kind = acclorite::SyntaxSourceKind::Completion,
                    .source_reference = "fish:/tmp/tool.fish",
                    .section = "complete / __fish_use_subcommand",
                },
                .synopsis = {},
            }},
        };

        acclorite::merge_command_grammar(primary, secondary);
        expect(primary.global_options.size() == 2,
               "grammar merge adds non-overlapping lower-priority global syntax");
        const auto output = std::ranges::find_if(primary.global_options, [](const acclorite::CommandOption& option) {
            return std::ranges::find(option.names, "--output") != option.names.end();
        });
        expect(output != primary.global_options.end() && output->value_shape_known && output->takes_value &&
                   output->provenance.source_kind == acclorite::SyntaxSourceKind::Man,
               "grammar merge never lets weaker duplicate completion evidence degrade authoritative man shape");
        expect(primary.subcommands.size() == 1 &&
                   primary.subcommands.front().provenance.source_kind == acclorite::SyntaxSourceKind::Man &&
                   primary.subcommands.front().options.size() == 1 &&
                   primary.subcommands.front().options.front().provenance.source_kind ==
                       acclorite::SyntaxSourceKind::Completion,
               "merged subcommand keeps parent authority while retaining per-option completion provenance");
    }

    const auto temp = std::filesystem::temp_directory_path() / "acclorite-syntax-merge-test";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);
    const auto executed_marker = temp / "target-executed";

    write_man_pages_fixture(temp, {
        {
            "guide-tool",
            "GUIDE-TOOL(1)\n"
            "NAME\n"
            "    guide-tool - fixture\n"
            "COMMANDS\n"
            "    guide-tool-switch(1)\n"
            "        Switch branches in the working tree.\n"
        },
    });
    {
        std::ofstream fish(temp / "guide-tool.fish");
        fish << "complete -c guide-tool -n '__fish_seen_subcommand_from switch' "
                "-l create -r -d 'Create a new branch before switching to it.'\n";
    }
    write_executable(temp / "guide-tool", "#!/bin/sh\ntouch '" + executed_marker.string() + "'\nexit 91\n");

    {
        ScopedPath scoped_path(temp);
        const acclorite::Candidate candidate{
            .command = "guide-tool",
            .summary = "fixture",
            .source = "test+man",
            .installed = true,
            .cli_capable = true,
        };
        acclorite::ManCommandSyntaxProvider man_provider;
        acclorite::FishCompletionSyntaxProvider fish_provider({temp});
        const auto man_root = man_provider.grammar(candidate);
        expect(man_root.has_value() && man_root->subcommands.size() == 1 &&
                   man_root->subcommands.front().name == "switch",
               "man root independently proves the child identity used by merged syntax");
        expect(!fish_provider.grammar(candidate).has_value(),
               "a scoped Fish condition still cannot invent a root subcommand by itself");
        if (man_root && !man_root->subcommands.empty()) {
            const auto fish_child = fish_provider.subcommand_grammar(candidate, man_root->subcommands.front());
            expect(fish_child.has_value() && fish_child->options.size() == 1 &&
                       std::ranges::find(fish_child->options.front().names, "--create") !=
                           fish_child->options.front().names.end(),
                   "Fish may enrich a child identity already proven by another trusted provider");
        }

        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<GuidanceIntegrationSource>());
        engine.add_syntax_provider(std::make_unique<acclorite::ManCommandSyntaxProvider>());
        engine.add_syntax_provider(std::make_unique<acclorite::FishCompletionSyntaxProvider>(
            std::vector<std::filesystem::path>{temp}
        ));

        const auto result = engine.search(
            acclorite::Query::parse(
                "guide-tool make a fresh branch called feature/foo and put me on it"
            ),
            10,
            false,
            true
        );
        expect(result.actionable_answer.has_value(),
               "SearchEngine composes one answer from complementary syntax providers");
        if (result.actionable_answer) {
            const auto& answer = *result.actionable_answer;
            expect(answer.relevant_subcommands.size() == 1 &&
                       answer.relevant_subcommands.front().name == "switch" &&
                       answer.relevant_subcommands.front().provenance.source_kind ==
                           acclorite::SyntaxSourceKind::Man,
                   "merged answer preserves the man-proven parent subcommand provenance");
            expect(answer.relevant_options.size() == 1 &&
                       answer.relevant_options.front().provenance.source_kind ==
                           acclorite::SyntaxSourceKind::Completion,
                   "merged answer preserves completion provenance on the scoped option fact");
            expect(answer.invocation.has_value() && answer.invocation->arguments.size() == 3 &&
                       answer.invocation->arguments[0].value == "switch" &&
                       answer.invocation->arguments[1].value == "--create" &&
                       answer.invocation->arguments[2].value == "feature/foo" &&
                       !answer.invocation->complete,
                   "cross-provider child grammar binds a verified scoped template without overstating completeness");
        }
        expect(std::ranges::any_of(result.timing.stages, [](const acclorite::TimingStage& stage) {
            return stage.name == "syntax:man";
        }) && std::ranges::any_of(result.timing.stages, [](const acclorite::TimingStage& stage) {
            return stage.name == "syntax:fish-completion-child";
        }), "profiling keeps each provider's root/child syntax cost independently visible after merging");
    }

    expect(!std::filesystem::exists(executed_marker),
           "cross-provider grammar merging never executes the target command");
    std::filesystem::remove_all(temp);
}

void test_parent_proven_child_man_grammar_resolves_compound_operation_without_executing_targets() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-child-man-syntax-test";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);
    const auto executed_marker = temp / "target-executed";

    write_man_pages_fixture(temp, {
        {
            "guide-tool",
            "GUIDE-TOOL(1)\n"
            "NAME\n"
            "    guide-tool - fixture\n"
            "COMMANDS\n"
            "    guide-tool-branch(1)\n"
            "        List, create, or delete branches.\n"
            "    guide-tool-switch(1)\n"
            "        Switch branches in the working tree.\n"
        },
        {
            "guide-tool-branch",
            "GUIDE-TOOL-BRANCH(1)\n"
            "NAME\n"
            "    guide-tool-branch - manage branches\n"
            "OPTIONS\n"
            "    -c, --copy <new-branch>\n"
            "        Copy a branch and its reflog.\n"
        },
        {
            "guide-tool-switch",
            "GUIDE-TOOL-SWITCH(1)\n"
            "NAME\n"
            "    guide-tool-switch - switch branches\n"
            "SYNOPSIS\n"
            "    guide-tool switch [<options>] [<branch>]\n"
            "    guide-tool switch [<options>] (-c|-C) <new-branch> [<start-point>]\n"
            "    guide-tool switch [<options>] --detach [<start-point>]\n"
            "OPTIONS\n"
            "    -c <new-branch>, --create <new-branch>\n"
            "        Create a new branch before switching to the branch.\n"
            "    -C <new-branch>, --force-create <new-branch>\n"
            "        Similar to --create except that an existing branch is reset before switching.\n"
        },
    });
    write_executable(temp / "guide-tool", "#!/bin/sh\ntouch '" + executed_marker.string() + "'\nexit 91\n");
    write_executable(temp / "guide-tool-branch", "#!/bin/sh\ntouch '" + executed_marker.string() + "'\nexit 92\n");
    write_executable(temp / "guide-tool-switch", "#!/bin/sh\ntouch '" + executed_marker.string() + "'\nexit 93\n");

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine engine;
        engine.add_source(std::make_unique<GuidanceIntegrationSource>());
        engine.add_syntax_provider(std::make_unique<acclorite::ManCommandSyntaxProvider>());

        const auto query = acclorite::Query::parse(
            "guide-tool make a fresh branch called feature/foo and put me on it"
        );
        const auto result = engine.search(query, 10, false, true);
        expect(result.action_target.has_value() &&
                   result.action_target->kind == acclorite::ActionTargetKind::Operation &&
                   std::ranges::find(result.action_target->terms, "create") != result.action_target->terms.end() &&
                   std::ranges::find(result.action_target->terms, "branch") != result.action_target->terms.end() &&
                   std::ranges::find(result.action_target->terms, "switch") != result.action_target->terms.end(),
               "compound branch request preserves create + branch + switch operation evidence");
        expect(result.actionable_answer.has_value(),
               "parent-proven child man grammar can answer a compound operation");
        if (result.actionable_answer) {
            const auto& answer = *result.actionable_answer;
            expect(answer.relevant_subcommands.size() == 1 &&
                       answer.relevant_subcommands.front().name == "switch",
                   "deeper grammar selects switch rather than shallow branch management");
            expect(answer.relevant_options.size() == 1 &&
                       std::ranges::find(answer.relevant_options.front().names, "--create") !=
                           answer.relevant_options.front().names.end(),
                   "child manual proves the create option inside the selected subcommand");
            expect(answer.relevant_options.front().provenance.source_reference == "man:guide-tool-switch",
                   "nested option provenance points at the child manual that proved it");
            expect(answer.invocation.has_value(),
                   "child option value can bind the user branch name into structured argv");
            if (answer.invocation) {
                const auto& invocation = *answer.invocation;
                expect(invocation.complete && invocation.command == "guide-tool" &&
                           invocation.arguments.size() == 3 &&
                           invocation.arguments[0].value == "switch" &&
                           invocation.arguments[1].value == "--create" &&
                           invocation.arguments[2].value == "feature/foo",
                       "selected child synopsis alternative proves the complete create-and-switch invocation");
            }
        }

        const auto alternate_phrase = engine.search(acclorite::Query::parse(
            "guide-tool make a new branch called release/test and go there"
        ));
        expect(alternate_phrase.actionable_answer.has_value() &&
                   alternate_phrase.actionable_answer->invocation.has_value(),
               "alternate create-and-go-there phrasing still binds exactly one branch literal");
        if (alternate_phrase.actionable_answer && alternate_phrase.actionable_answer->invocation) {
            const auto& invocation = *alternate_phrase.actionable_answer->invocation;
            expect(invocation.complete && invocation.arguments.size() == 3 &&
                       invocation.arguments[0].value == "switch" &&
                       invocation.arguments[1].value == "--create" &&
                       invocation.arguments[2].value == "release/test",
                   "operation paraphrases are consumed as structure instead of extra bindable literals");
        }

        expect(std::ranges::any_of(result.timing.stages, [](const acclorite::TimingStage& stage) {
            return stage.name == "syntax:man-child";
        }), "profiling exposes child-man grammar acquisition separately");

        std::ostringstream terminal;
        acclorite::TerminalRenderer{}.render(result, terminal);
        expect(terminal.str().find("Verified command") != std::string::npos &&
                   terminal.str().find("guide-tool switch --create feature/foo") != std::string::npos &&
                   terminal.str().find("man:guide-tool-switch") != std::string::npos,
               "terminal renders nested verified syntax and child-man provenance");

        const auto create_only = engine.search(acclorite::Query::parse(
            "guide-tool make a fresh branch called feature/only"
        ));
        expect(create_only.actionable_answer.has_value() &&
                   create_only.actionable_answer->relevant_subcommands.size() == 1 &&
                   create_only.actionable_answer->relevant_subcommands.front().name == "branch" &&
                   create_only.actionable_answer->relevant_options.empty(),
               "child grammar never adds switch semantics when root branch grammar already satisfies create-only intent");
    }

    expect(!std::filesystem::exists(executed_marker),
           "child-man enrichment never executes parent or child target commands");
    std::filesystem::remove_all(temp);
}

void test_explicit_option_answer_is_verified_post_ranking_and_machine_visible() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-actionable-option-test";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);
    const auto executed_marker = temp / "guide-tool-executed";

    write_man_page_fixture(
        temp,
        "GUIDE-TOOL(1)\n"
        "NAME\n"
        "    guide-tool - fixture\n"
        "SYNOPSIS\n"
        "    guide-tool [OPTIONS] <input>\n"
        "OPTIONS\n"
        "    -o, --output <path>\n"
        "        Write the result to the selected path.\n"
        "    --quiet\n"
        "        Suppress normal output.\n"
        "    -l, --lower-case\n"
        "        Lowercase fixture option.\n"
        "    -L, --upper-case\n"
        "        Uppercase fixture option.\n"
        "    -R, --location\n"
        "        Follow HTTP redirects to the new location.\n"
        "        --user may appear here as referenced prose without becoming a declaration.\n"
        "    --max-redirs <count>\n"
        "        Limit the number of redirects that may be followed.\n"
        "    -., --hidden\n"
        "        Search hidden files and directories.\n"
        "    --seek <position>\n"
        "        Seek input to the requested position.\n"
        "COMMANDS\n"
        "    status [UNIT...|PID...]]\n"
        "        Show runtime status information for a unit.\n"
        "    guide-tool-branch(1)\n"
        "        List, create, or delete branches.\n"
        "    guide-tool-switch(1)\n"
        "        Switch branches in the working tree.\n"
    );
    write_executable(temp / "guide-tool", "#!/bin/sh\ntouch '" + executed_marker.string() + "'\nexit 92\n");

    const auto query = acclorite::Query::parse("what does guide-tool --output do");

    acclorite::SearchEngine baseline;
    baseline.add_source(std::make_unique<GuidanceIntegrationSource>());
    const auto without_syntax = baseline.search(query, 10, false, true);

    {
        ScopedPath scoped_path(temp);
        acclorite::SearchEngine actionable;
        actionable.add_source(std::make_unique<GuidanceIntegrationSource>());
        actionable.add_syntax_provider(std::make_unique<acclorite::ManCommandSyntaxProvider>());
        const auto result = actionable.search(query, 10, false, true);

        expect(result.candidates.size() == without_syntax.candidates.size(),
               "syntax enrichment does not change candidate count");
        if (result.candidates.size() == without_syntax.candidates.size()) {
            for (std::size_t i = 0; i < result.candidates.size(); ++i) {
                expect(result.candidates[i].command == without_syntax.candidates[i].command,
                       "syntax enrichment does not change ranking order");
                expect(std::abs(result.candidates[i].score - without_syntax.candidates[i].score) < 0.000001,
                       "syntax enrichment does not change ranking score");
            }
        }

        expect(result.actionable_answer.has_value(),
               "explicit locally verified option produces an actionable answer");
        if (result.actionable_answer) {
            expect(result.actionable_answer->command == "guide-tool",
                   "actionable answer is attached to the selected candidate, not ranking data");
            expect(!result.actionable_answer->invocation.has_value(),
                   "Explain-only option inspection does not manufacture an execution-shaped invocation");
            expect(result.actionable_answer->relevant_options.size() == 1,
                   "option question returns the relevant verified option instead of dumping all flags");
            if (!result.actionable_answer->relevant_options.empty()) {
                const auto& option = result.actionable_answer->relevant_options.front();
                expect(!option.names.empty() && option.names.front() == "--output",
                       "answer composer preserves the exact option spelling requested by the user");
                expect(option.value_name == "path" && option.value_required,
                       "actionable answer preserves verified argument shape");
            }
            expect(result.actionable_answer->safety == acclorite::ActionSafety::Unknown,
                   "initial syntax slice exposes honest Unknown safety rather than guessing");
        }

        expect(std::ranges::any_of(result.timing.stages, [](const acclorite::TimingStage& stage) {
            return stage.name == "syntax:man";
        }), "performance profile exposes syntax-provider cost independently of ranking");
        expect(std::ranges::any_of(result.timing.stages, [](const acclorite::TimingStage& stage) {
            return stage.name == "answer-compose";
        }), "performance profile exposes deterministic answer composition cost");

        std::ostringstream terminal;
        acclorite::TerminalRenderer{}.render(result, terminal);
        expect(terminal.str().find("Verified syntax") != std::string::npos &&
                   terminal.str().find("guide-tool --output <path>") != std::string::npos,
               "terminal renders a source-backed syntax template for explicit option questions");
        expect(terminal.str().find("man:guide-tool") != std::string::npos &&
                   terminal.str().find("OPTIONS") != std::string::npos,
               "terminal renders option provenance next to the verified syntax");

        std::ostringstream json;
        acclorite::JsonRenderer{}.render(result, json);
        expect(json.str().find("\"actionable_answer\": {") != std::string::npos &&
                   json.str().find("\"command\": \"guide-tool\"") != std::string::npos,
               "search JSON exposes additive actionable-answer structure");
        expect(json.str().find("\"source_type\": \"man\"") != std::string::npos &&
                   json.str().find("\"value_name\": \"path\"") != std::string::npos,
               "machine output exposes syntax provenance and argument metadata without scraping prose");

        const auto uppercase_option = actionable.search(
            acclorite::Query::parse("what does guide-tool -L do")
        );
        expect(uppercase_option.actionable_answer.has_value(),
               "explicit option matching preserves case-sensitive short-option identity");
        if (uppercase_option.actionable_answer &&
            !uppercase_option.actionable_answer->relevant_options.empty()) {
            const auto& option = uppercase_option.actionable_answer->relevant_options.front();
            expect(!option.names.empty() && option.names.front() == "-L",
                   "uppercase short option is not normalized into the distinct lowercase option");
            expect(option.description.find("Uppercase fixture option") != std::string::npos,
                   "case-sensitive short option selects the matching documented semantics");
        }

        const auto lowercase_option = actionable.search(
            acclorite::Query::parse("what does guide-tool -l do")
        );
        expect(lowercase_option.actionable_answer.has_value(),
               "lowercase short option remains independently addressable");
        if (lowercase_option.actionable_answer &&
            !lowercase_option.actionable_answer->relevant_options.empty()) {
            expect(lowercase_option.actionable_answer->relevant_options.front().names.front() == "-l",
                   "lowercase short option does not alias to uppercase spelling");
        }

        const auto punctuation_option = actionable.search(
            acclorite::Query::parse("what does guide-tool -. do")
        );
        expect(punctuation_option.actionable_answer.has_value(),
               "punctuation-valued short option remains explicitly addressable end to end");
        if (punctuation_option.actionable_answer &&
            !punctuation_option.actionable_answer->relevant_options.empty()) {
            expect(punctuation_option.actionable_answer->relevant_options.front().names.front() == "-.",
                   "explicit punctuation short option preserves exact requested spelling");
        }

        const auto semantic_redirect = actionable.search(
            acclorite::Query::parse("what flag makes guide-tool follow redirects"), 10, false, true
        );
        expect(semantic_redirect.actionable_answer.has_value(),
               "natural-language capability question selects a locally verified option");
        expect(semantic_redirect.targets.size() == 1 && semantic_redirect.targets.front() == "guide-tool",
               "syntax-target parent command is exposed as the resolved entity target");
        expect(semantic_redirect.action_target.has_value() &&
                   semantic_redirect.action_target->kind == acclorite::ActionTargetKind::Option &&
                   !semantic_redirect.action_target->explicit_syntax,
               "search result carries structured semantic option intent separately from ranking targets");
        if (semantic_redirect.actionable_answer &&
            !semantic_redirect.actionable_answer->relevant_options.empty()) {
            const auto& option = semantic_redirect.actionable_answer->relevant_options.front();
            expect(std::ranges::find(option.names, "--location") != option.names.end(),
                   "capability matching selects redirect-following option rather than another redirect-related flag");
            expect(std::ranges::find(option.names, "--max-redirs") == option.names.end(),
                   "semantic option matching does not confuse capability with an adjacent related option");
        }

        const auto semantic_hidden = actionable.search(
            acclorite::Query::parse("which guide-tool flag includes hidden files")
        );
        expect(semantic_hidden.actionable_answer.has_value(),
               "semantic flag query can resolve a capability from option name plus documented description");
        if (semantic_hidden.actionable_answer && !semantic_hidden.actionable_answer->relevant_options.empty()) {
            const auto& names = semantic_hidden.actionable_answer->relevant_options.front().names;
            expect(std::ranges::find(names, "--hidden") != names.end(),
                   "hidden-files capability resolves to the verified hidden option");
            expect(!names.empty() && names.front() == "--hidden",
                   "semantic option answers prefer a readable long alias over an obscure short spelling");
        }

        const auto semantic_subcommand = actionable.search(
            acclorite::Query::parse("which guide-tool subcommand creates a branch")
        );
        expect(semantic_subcommand.actionable_answer.has_value(),
               "semantic subcommand query selects locally verified man subcommand grammar");
        if (semantic_subcommand.actionable_answer &&
            !semantic_subcommand.actionable_answer->relevant_subcommands.empty()) {
            const auto& subcommand = semantic_subcommand.actionable_answer->relevant_subcommands.front();
            expect(subcommand.name == "branch",
                   "subcommand capability matching selects branch from verified description and identity");
            expect(subcommand.provenance.source_kind == acclorite::SyntaxSourceKind::Man &&
                       subcommand.provenance.section == "COMMANDS",
                   "semantic subcommand answer retains exact syntax provenance");
        }
        std::ostringstream subcommand_terminal;
        acclorite::TerminalRenderer{}.render(semantic_subcommand, subcommand_terminal);
        expect(subcommand_terminal.str().find("guide-tool branch") != std::string::npos &&
                   subcommand_terminal.str().find("man:guide-tool") != std::string::npos,
               "terminal renders verified subcommand identity and provenance without pretending to bind arguments");

        const auto implicit_redirect = actionable.search(acclorite::Query::parse(
            "guide-tool keeps getting 3xx responses and stopping, make it follow the redirect"
        ));
        expect(implicit_redirect.actionable_answer.has_value() &&
                   implicit_redirect.action_target.has_value() &&
                   implicit_redirect.action_target->kind == acclorite::ActionTargetKind::Option,
               "verified grammar resolves an implicit operation into an option target");
        if (implicit_redirect.actionable_answer &&
            !implicit_redirect.actionable_answer->relevant_options.empty()) {
            expect(std::ranges::find(
                       implicit_redirect.actionable_answer->relevant_options.front().names,
                       "--location"
                   ) != implicit_redirect.actionable_answer->relevant_options.front().names.end(),
                   "implicit redirect operation resolves to verified redirect-following option");
        }

        const auto implicit_hidden = actionable.search(acclorite::Query::parse(
            "guide-tool is skipping dotfiles, make it search them too"
        ));
        expect(implicit_hidden.actionable_answer.has_value() &&
                   implicit_hidden.action_target.has_value() &&
                   implicit_hidden.action_target->kind == acclorite::ActionTargetKind::Option,
               "hidden-file complaint resolves operation into verified option syntax");

        const auto implicit_status = actionable.search(acclorite::Query::parse(
            "guide-tool show me what nginx is doing"
        ));
        expect(implicit_status.actionable_answer.has_value() &&
                   implicit_status.action_target.has_value() &&
                   implicit_status.action_target->kind == acclorite::ActionTargetKind::Subcommand,
               "human status request resolves operation into verified subcommand syntax");
        if (implicit_status.actionable_answer &&
            !implicit_status.actionable_answer->relevant_subcommands.empty()) {
            expect(implicit_status.actionable_answer->relevant_subcommands.front().name == "status",
                   "implicit status request selects verified status subcommand");
            expect(implicit_status.actionable_answer->invocation.has_value(),
                   "verified optional subcommand positional can bind a user-provided unit name");
            if (implicit_status.actionable_answer->invocation) {
                const auto& invocation = *implicit_status.actionable_answer->invocation;
                expect(invocation.complete && invocation.arguments.size() == 2 &&
                           invocation.arguments[0].value == "status" &&
                           invocation.arguments[1].value == "nginx" &&
                           !invocation.arguments[1].placeholder,
                       "argument binder constructs a complete structured system-status-shaped invocation");
            }
        }

        std::ostringstream implicit_status_terminal;
        acclorite::TerminalRenderer{}.render(implicit_status, implicit_status_terminal);
        expect(implicit_status_terminal.str().find("guide-tool status nginx") != std::string::npos,
               "terminal renders the complete bound invocation through the shell renderer");

        const auto implicit_seek = actionable.search(acclorite::Query::parse(
            "guide-tool start reading this video from 30 seconds in"
        ));
        expect(implicit_seek.actionable_answer.has_value() &&
                   implicit_seek.actionable_answer->invocation.has_value(),
               "required option value binding produces a structured partial invocation");
        if (implicit_seek.actionable_answer && implicit_seek.actionable_answer->invocation) {
            const auto& invocation = *implicit_seek.actionable_answer->invocation;
            expect(!invocation.complete && invocation.arguments.size() == 2 &&
                       invocation.arguments[0].value == "--seek" &&
                       invocation.arguments[1].value == "30" &&
                       !invocation.arguments[1].placeholder,
                   "binder preserves the user-supplied option value without claiming root-command completeness");
        }

        std::ostringstream semantic_json;
        acclorite::JsonRenderer{}.render(semantic_redirect, semantic_json);
        expect(semantic_json.str().find("\"action_target\": {") != std::string::npos &&
                   semantic_json.str().find("\"kind\": \"option\"") != std::string::npos &&
                   semantic_json.str().find("\"follow\"") != std::string::npos,
               "machine output exposes structured action target and capability terms additively");

        const auto unsupported = actionable.search(
            acclorite::Query::parse("what does guide-tool --absolutely-made-up do")
        );
        expect(!unsupported.actionable_answer.has_value(),
               "unsupported explicit flags are rejected instead of hallucinated from nearby documentation");
    }

    expect(!std::filesystem::exists(executed_marker),
           "end-to-end actionable answer path never executes the selected command");
    std::filesystem::remove_all(temp);
}

void test_child_synopsis_completeness_requires_explicit_compatible_path() {
    auto query = acclorite::Query::parse("tool make a new branch called feature/foo and switch");
    query.action_target = acclorite::ActionTarget{
        .kind = acclorite::ActionTargetKind::Operation,
        .command = std::string("tool"),
        .literal = std::nullopt,
        .terms = {"create", "branch", "switch"},
        .explicit_syntax = false,
    };

    const acclorite::CommandGrammar grammar{.command = "tool"};
    const acclorite::CommandOption create{
        .names = {"--create", "-c"},
        .description = "Create a new branch before switching.",
        .value_name = std::string("new-branch"),
        .takes_value = true,
        .value_required = true,
    };

    acclorite::SubcommandSpec generic_only{
        .name = "switch",
        .synopsis = {
            acclorite::SynopsisAlternative{.text = "tool switch [<options>] [<branch>]"},
        },
    };
    const auto generic = acclorite::ArgumentBinder::bind_subcommand_option(
        query, grammar, generic_only, create
    );
    expect(generic.has_value() && !generic->complete,
           "generic <options> synopsis allowance does not prove one selected option path complete");

    acclorite::SubcommandSpec unmet_required{
        .name = "switch",
        .synopsis = {
            acclorite::SynopsisAlternative{
                .text = "tool switch [<options>] (-c|-C) <new-branch> <required-extra>"
            },
        },
    };
    const auto unmet = acclorite::ArgumentBinder::bind_subcommand_option(
        query, grammar, unmet_required, create
    );
    expect(unmet.has_value() && !unmet->complete,
           "child synopsis with an additional unmet required slot remains an incomplete template");
}

void test_binder_only_consumes_operation_paraphrases_when_operation_proves_them() {
    auto query = acclorite::Query::parse("tool write output to new");
    query.action_target = acclorite::ActionTarget{
        .kind = acclorite::ActionTargetKind::Operation,
        .command = std::string("tool"),
        .literal = std::nullopt,
        .terms = {"write", "output"},
        .explicit_syntax = false,
    };

    const acclorite::CommandGrammar grammar{.command = "tool"};
    const acclorite::SubcommandSpec subcommand{.name = "emit"};
    const acclorite::CommandOption option{
        .names = {"--output"},
        .description = "Write output to a selected name.",
        .value_name = std::string("name"),
        .takes_value = true,
        .value_required = true,
    };

    const auto invocation = acclorite::ArgumentBinder::bind_subcommand_option(
        query, grammar, subcommand, option
    );
    expect(invocation.has_value() && invocation->arguments.size() == 3 &&
               invocation->arguments[2].value == "new",
           "generic literal 'new' remains bindable outside create-branch operation recovery");
}

void test_hidden_files_paraphrase_is_not_rebound_as_root_search_data() {
    auto query = acclorite::Query::parse("rg is skipping dotfiles, make it search them too");
    query.frame = acclorite::query::recognize_frame(query);
    query.action_target = acclorite::query::detect_action_target(query);

    expect(query.action_target.has_value() &&
               query.action_target->kind == acclorite::ActionTargetKind::Operation &&
               std::ranges::find(query.action_target->terms, "hidden") != query.action_target->terms.end() &&
               std::ranges::find(query.action_target->terms, "files") != query.action_target->terms.end(),
           "dotfiles wording still recovers the hidden-files option target");

    const acclorite::CommandOption hidden{
        .names = {"--hidden", "-."},
        .description = "Search hidden files and directories.",
        .value_shape_known = true,
        .takes_value = false,
        .value_required = false,
        .provenance = acclorite::SyntaxProvenance{
            .source_kind = acclorite::SyntaxSourceKind::Man,
            .source_reference = "man:rg",
            .section = "OPTIONS",
        },
    };
    const acclorite::CommandGrammar grammar{
        .command = "rg",
        .global_options = {hidden},
        .synopsis = {acclorite::SynopsisAlternative{
            .text = "rg [OPTIONS] PATTERN [PATH...]",
            .provenance = acclorite::SyntaxProvenance{
                .source_kind = acclorite::SyntaxSourceKind::Man,
                .source_reference = "man:rg",
                .section = "SYNOPSIS",
            },
        }},
    };

    const auto invocation = acclorite::ArgumentBinder::bind_root_option(query, grammar, hidden);
    expect(!invocation.has_value(),
           "hidden-files paraphrase words are never rebound as rg PATTERN/PATH literals");

    auto literal_query = acclorite::Query::parse("tool compare dotfiles to skipping");
    literal_query.action_target = acclorite::ActionTarget{
        .kind = acclorite::ActionTargetKind::Operation,
        .command = std::string("tool"),
        .literal = std::nullopt,
        .terms = {"compare"},
        .explicit_syntax = false,
    };
    const acclorite::CommandGrammar literal_grammar{
        .command = "tool",
        .synopsis = {acclorite::SynopsisAlternative{
            .text = "tool LEFT RIGHT",
            .provenance = acclorite::SyntaxProvenance{
                .source_kind = acclorite::SyntaxSourceKind::Man,
                .source_reference = "man:tool",
                .section = "SYNOPSIS",
            },
        }},
    };
    const auto literal_invocation = acclorite::ArgumentBinder::bind_root(literal_query, literal_grammar);
    expect(literal_invocation.has_value() && literal_invocation->arguments.size() == 2 &&
               literal_invocation->arguments[0].value == "dotfiles" &&
               literal_invocation->arguments[1].value == "skipping",
           "dotfiles/skipping remain ordinary bindable data outside hidden-files intent");
}

void test_safety_classifier_requires_complete_source_backed_alignment() {
    const auto classify = [](
        std::vector<std::string> terms,
        std::string summary,
        std::vector<std::string> arguments,
        const bool complete = true,
        std::vector<acclorite::CommandOption> options = {},
        std::vector<acclorite::SubcommandSpec> subcommands = {}
    ) {
        acclorite::Query query = acclorite::Query::parse("fixture");
        query.action_target = acclorite::ActionTarget{
            .kind = acclorite::ActionTargetKind::Operation,
            .command = std::string("tool"),
            .literal = std::nullopt,
            .terms = std::move(terms),
            .explicit_syntax = false,
        };
        acclorite::Candidate candidate;
        candidate.command = "tool";
        candidate.summary = std::move(summary);

        acclorite::ActionableAnswer answer;
        answer.command = "tool";
        answer.invocation = acclorite::CommandInvocation{
            .command = "tool",
            .complete = complete,
        };
        for (auto& argument : arguments) {
            answer.invocation->arguments.push_back(acclorite::InvocationArgument{
                .value = std::move(argument),
                .placeholder = false,
            });
        }
        answer.relevant_options = std::move(options);
        answer.relevant_subcommands = std::move(subcommands);
        return acclorite::SafetyClassifier::classify(query, candidate, answer);
    };

    expect(classify({"remove", "delete"}, "remove files and directories", {"victim"}) ==
               acclorite::ActionSafety::Destructive,
           "safety classifier labels destructive actions only when requested intent and source semantics agree");
    expect(classify({"copy"}, "copy files and directories", {"source", "dest"}) ==
               acclorite::ActionSafety::Mutating,
           "copy operation is source-backed mutating behavior");
    expect(classify({"search"}, "print lines that match patterns", {"TODO", "notes.txt"}) ==
               acclorite::ActionSafety::ReadOnly,
           "search operation is source-backed read-only behavior");
    expect(classify({"download"}, "transfer data from or to a server", {"https://example.com"}) ==
               acclorite::ActionSafety::Network,
           "network classification requires both network semantics and a concrete network request signal");
    expect(classify({"change"}, "change system state; requires root privileges", {"setting"}) ==
               acclorite::ActionSafety::Privileged,
           "explicit source wording can classify a complete action as privileged");
    expect(classify({"remove", "delete"}, "remove files and directories", {"victim"}, false) ==
               acclorite::ActionSafety::Unknown,
           "incomplete command templates stay Unknown even when their partial semantics look destructive");
    expect(classify({"search"}, "write matching records to the database", {"needle"}) ==
               acclorite::ActionSafety::Unknown,
           "conflicting requested and documented behavior stays Unknown instead of forcing a reassuring label");

    const acclorite::CommandOption create_option{
        .names = {"--create"},
        .description = "Create a new branch before switching to it.",
        .provenance = acclorite::SyntaxProvenance{
            .source_kind = acclorite::SyntaxSourceKind::Man,
            .source_reference = "man:tool-switch",
            .section = "OPTIONS",
        },
    };
    expect(classify({"create", "branch", "switch"}, "generic tool frontend", {"switch", "--create", "feature/x"},
                    true, {create_option}) == acclorite::ActionSafety::Mutating,
           "specific verified option semantics outrank an uninformative parent summary for safety classification");
}

void test_root_synopsis_binding_handles_practical_multiargument_commands() {
    const auto provenance = acclorite::SyntaxProvenance{
        .source_kind = acclorite::SyntaxSourceKind::Man,
        .source_reference = "man:tool",
        .section = "SYNOPSIS",
    };

    {
        auto query = acclorite::Query::parse("tool rename ~/uncool-shit to ~/cool-shit");
        query.action_target = acclorite::ActionTarget{
            .kind = acclorite::ActionTargetKind::Operation,
            .command = std::string("tool"),
            .literal = std::nullopt,
            .terms = {"rename", "move"},
            .explicit_syntax = false,
        };
        const acclorite::CommandGrammar grammar{
            .command = "tool",
            .synopsis = {acclorite::SynopsisAlternative{
                .text = "tool [OPTION]... SOURCE DEST",
                .provenance = provenance,
            }},
        };
        const auto invocation = acclorite::ArgumentBinder::bind_root(query, grammar);
        expect(invocation.has_value() && invocation->complete && invocation->arguments.size() == 2 &&
                   invocation->arguments[0].value == "~/uncool-shit" &&
                   invocation->arguments[1].value == "~/cool-shit",
               "root SYNOPSIS binder maps ordered SOURCE/DEST literals without inventing options");
    }

    {
        auto query = acclorite::Query::parse("tool search TODO in ~/Realmheart");
        query.action_target = acclorite::ActionTarget{
            .kind = acclorite::ActionTargetKind::Operation,
            .command = std::string("tool"),
            .literal = std::nullopt,
            .terms = {"search"},
            .explicit_syntax = false,
        };
        const acclorite::CommandGrammar grammar{
            .command = "tool",
            .synopsis = {acclorite::SynopsisAlternative{
                .text = "tool [OPTIONS] PATTERN [PATH...]",
                .provenance = provenance,
            }},
        };
        const auto invocation = acclorite::ArgumentBinder::bind_root(query, grammar);
        expect(invocation.has_value() && invocation->complete && invocation->arguments.size() == 2 &&
                   invocation->arguments[0].value == "TODO" &&
                   invocation->arguments[1].value == "~/Realmheart",
               "root binder preserves pattern/path order and consumes optional variadic path data");
    }

    {
        auto query = acclorite::Query::parse("tool save https://example.com to 'My Report.html'");
        query.action_target = acclorite::ActionTarget{
            .kind = acclorite::ActionTargetKind::Operation,
            .command = std::string("tool"),
            .literal = std::nullopt,
            .terms = {"output"},
            .explicit_syntax = false,
        };
        const acclorite::CommandOption output{
            .names = {"--output", "-o"},
            .description = "Write output to a file.",
            .value_name = std::string("file"),
            .value_shape_known = true,
            .takes_value = true,
            .value_required = true,
            .provenance = provenance,
        };
        const acclorite::CommandGrammar grammar{
            .command = "tool",
            .global_options = {output},
            .synopsis = {acclorite::SynopsisAlternative{
                .text = "tool [OPTIONS] <url>",
                .provenance = provenance,
            }},
        };
        const auto invocation = acclorite::ArgumentBinder::bind_root_option(query, grammar, output);
        expect(invocation.has_value() && invocation->complete && invocation->arguments.size() == 3 &&
                   invocation->arguments[0].value == "--output" &&
                   invocation->arguments[1].value == "My Report.html" &&
                   invocation->arguments[2].value == "https://example.com",
               "global option value role selection composes destination output with independent root URL grammar");
        if (invocation) {
            expect(acclorite::output::render_shell_invocation(*invocation) ==
                       "tool --output 'My Report.html' https://example.com",
                   "multi-role root binding remains shell-safe for quoted destination values");
        }
    }

    {
        auto query = acclorite::Query::parse("rename ~/old to ~/new");
        query.frame = acclorite::query::recognize_frame(query);
        query.action_target = acclorite::query::detect_action_target(query);
        expect(query.action_target.has_value() &&
                   query.action_target->kind == acclorite::ActionTargetKind::Operation &&
                   !query.action_target->command.has_value() &&
                   std::ranges::find(query.action_target->terms, "rename") != query.action_target->terms.end(),
               "strong modify intent with concrete values can remain command-agnostic until ranking selects the parent tool");

        const acclorite::Candidate candidate{
            .command = "mv",
            .summary = "move or rename files",
            .installed = true,
            .cli_capable = true,
        };
        const acclorite::CommandGrammar grammar{
            .command = "mv",
            .synopsis = {acclorite::SynopsisAlternative{
                .text = "mv [OPTION]... SOURCE DEST",
                .provenance = acclorite::SyntaxProvenance{
                    .source_kind = acclorite::SyntaxSourceKind::Man,
                    .source_reference = "man:mv",
                    .section = "SYNOPSIS",
                },
            }},
        };
        const auto answer = acclorite::AnswerComposer::compose(query, candidate, grammar, {});
        expect(answer.has_value() && answer->invocation.has_value() && answer->invocation->complete &&
                   answer->invocation->command == "mv" && answer->invocation->arguments.size() == 2,
               "post-ranking composition can turn command-agnostic human intent into a proven root invocation");
        expect(answer.has_value() && answer->safety == acclorite::ActionSafety::Mutating,
               "composer attaches evidence-driven safety after constructing a complete verified invocation");
    }
}

void test_shell_renderer_quotes_structured_invocations() {
    const acclorite::CommandInvocation invocation{
        .command = "tool",
        .arguments = {
            acclorite::InvocationArgument{.value = "status", .placeholder = false},
            acclorite::InvocationArgument{.value = "unit with spaces", .placeholder = false},
            acclorite::InvocationArgument{.value = "<VALUE>", .placeholder = true},
        },
        .complete = false,
    };
    expect(acclorite::output::render_shell_invocation(invocation) ==
               "tool status 'unit with spaces' <VALUE>",
           "shell renderer quotes literal whitespace while leaving verified placeholders visibly structural");
    expect(acclorite::output::shell_quote("O'Brien") == "'O'\\''Brien'",
           "shell renderer safely escapes embedded single quotes without evaluating input");
    expect(acclorite::output::shell_quote("simple/path-1") == "simple/path-1",
           "shell renderer leaves conservative shell-safe literals readable");
    expect(acclorite::output::shell_quote("~/My Files/input.txt") == "~/'My Files/input.txt'",
           "shell renderer quotes a spaced home-relative path without disabling tilde expansion");
}

void test_syntax_provider_is_not_touched_for_ordinary_discovery() {
    int availability_calls = 0;
    int grammar_calls = 0;

    acclorite::SearchEngine engine;
    engine.add_source(std::make_unique<GuidanceIntegrationSource>());
    engine.add_syntax_provider(std::make_unique<CountingSyntaxProvider>(&availability_calls, &grammar_calls));
    const auto result = engine.search(acclorite::Query::parse("search text"), 10, false, true);

    expect(!result.candidates.empty(), "ordinary discovery fixture still returns candidates");
    expect(!result.actionable_answer.has_value(), "ordinary discovery does not manufacture an actionable option answer");
    expect(availability_calls == 0 && grammar_calls == 0,
           "ordinary discovery bypasses syntax providers entirely on the benchmark hot path");
    expect(std::ranges::none_of(result.timing.stages, [](const acclorite::TimingStage& stage) {
        return stage.name.starts_with("syntax:") || stage.name == "answer-compose";
    }), "ordinary discovery profiling has no hidden syntax-provider work");
}

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


void test_info_guidance_is_verified_and_falls_back_after_man() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-info-guidance-test";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);
    const auto info_root = temp / "share/info";
    std::filesystem::create_directories(info_root);
    {
        std::ofstream menu(info_root / "dir");
        menu << "* tool: (tool)Top. Fixture tool manual.\n";
    }
    write_info_page_fixture(
        temp,
        "File: tool.info,  Node: Top\n"
        "1 Examples\n"
        "**********\n"
        "    tool --from-info ./src\n"
        "    prose mentioning tool is not accepted\n"
        "    $ tool --json ./src\n"
        "2 Options\n"
        "*********\n"
        "    --help\n"
    );

    {
        ScopedPath scoped_path(temp);
        ScopedEnv scoped_infopath("INFOPATH", info_root.string());
        acclorite::InfoGuidanceProvider provider;
        expect(provider.available(), "info guidance provider detects local info executable");

        acclorite::Candidate candidate{
            .command = "tool",
            .summary = "fixture",
            .source = "path+man",
            .installed = true,
            .cli_capable = true,
        };
        const auto bundle = provider.guide(candidate);
        expect(bundle.learning_resources.size() == 1,
               "verified local Info topic creates one learning resource");
        expect(bundle.examples.size() == 2,
               "Info fallback extracts only command-looking lines from explicit examples section");
        if (bundle.examples.size() >= 2) {
            expect(bundle.examples[0].text == "tool --from-info ./src",
                   "Info guidance preserves source-backed command text");
            expect(bundle.examples[1].text == "tool --json ./src",
                   "Info guidance strips a shell prompt without inventing syntax");
            expect(bundle.examples[0].source_reference == "info:tool" && bundle.examples[0].verified,
                   "Info example carries explicit verified provenance");
        }

        candidate.examples.push_back(acclorite::UsageExample{
            .text = "tool --from-man",
            .source_kind = acclorite::GuidanceSourceKind::Man,
            .source_reference = "man:tool",
            .verified = true,
        });
        const auto lower_priority = provider.guide(candidate);
        expect(lower_priority.learning_resources.size() == 1,
               "Info remains available as a learning resource after man examples exist");
        expect(lower_priority.examples.empty(),
               "Info does not duplicate lower-priority example extraction when man already succeeded");

        candidate.command = "missing-tool";
        candidate.examples.clear();
        const auto cheap_miss = provider.guide(candidate);
        expect(cheap_miss.examples.empty() && cheap_miss.learning_resources.empty(),
               "Info guidance rejects an absent local topic before invoking the expensive exact probe");

        {
            std::ofstream dedicated(info_root / "solo.info.gz");
            dedicated << "fixture";
        }
        candidate.command = "solo";
        const auto dedicated_manual = provider.guide(candidate);
        expect(dedicated_manual.learning_resources.size() == 1,
               "Info prefilter accepts an exact dedicated .info manual even without a dir menu entry");

        candidate.command = "tool";
        candidate.installed = false;
        candidate.examples.clear();
        const auto not_local = provider.guide(candidate);
        expect(not_local.examples.empty() && not_local.learning_resources.empty(),
               "Info guidance refuses local documentation for a non-installed candidate");

        candidate.installed = true;
        candidate.learning_resources = {acclorite::LearningResource{
            .label = "Local Info manual",
            .target = "info tool",
            .source_kind = acclorite::GuidanceSourceKind::Info,
            .source_reference = "info:tool",
            .verified = true,
        }};
        acclorite::SearchResult info_only_result;
        info_only_result.candidates.push_back(candidate);
        std::ostringstream info_only_terminal;
        acclorite::TerminalRenderer{}.render(info_only_result, info_only_terminal);
        expect(info_only_terminal.str().find("No verified example found") == std::string::npos &&
               info_only_terminal.str().find("Learn") != std::string::npos,
               "terminal omits empty example sections while preserving verified learning resources");

        write_executable(temp / "info", "#!/bin/sh\nexit 0\n");
        candidate.learning_resources.clear();
        const auto unverified_topic = provider.guide(candidate);
        expect(unverified_topic.examples.empty() && unverified_topic.learning_resources.empty(),
               "Info guidance requires a non-empty exact topic location before claiming provenance");
    }
    std::filesystem::remove_all(temp);
}

void test_tldr_guidance_reads_exact_local_cache_without_spawning_client() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-tldr-guidance-test";
    std::filesystem::remove_all(temp);
    const auto bin = temp / "bin";
    const auto cache = temp / "cache";
    const auto pages = cache / "tldr-pages/pages.en/common";
    const auto marker = temp / "tldr-invoked";
    std::filesystem::create_directories(bin);
    std::filesystem::create_directories(pages);

    write_executable(
        bin / "tldr",
        "#!/bin/sh\n"
        "printf '%s\\n' invoked >> \"$TLDR_TEST_MARKER\"\n"
        "exit 99\n"
    );
    {
        std::ofstream page(pages / "tool.md");
        page << "# tool\n\n"
                "> Fixture community page.\n\n"
                "- Scan a path:\n\n"
                "`tool --scan {{path/to/files}}`\n\n"
                "- This code line is not the documented command:\n\n"
                "`printf 'tool'`\n\n"
                "- Run with privileges:\n\n"
                "`sudo tool --json {{path/to/files}}`\n";
    }

    {
        ScopedPath scoped_path(bin);
        ScopedEnv home("HOME", (temp / "home").string());
        ScopedEnv xdg_cache("XDG_CACHE_HOME", (temp / "xdg-cache").string());
        ScopedEnv xdg_data("XDG_DATA_DIRS", (temp / "data").string());
        ScopedEnv language("LANGUAGE", "");
        ScopedEnv lang("LANG", "en_US.UTF-8");
        ScopedEnv tealdeer_cache("TEALDEER_CACHE_DIR", cache.string());
        ScopedEnv tealdeer_config("TEALDEER_CONFIG_DIR", (temp / "config").string());
        ScopedEnv invoked_marker("TLDR_TEST_MARKER", marker.string());

        acclorite::TldrGuidanceProvider provider;
        expect(provider.available(), "TLDR guidance detects a local cache without invoking the client");

        acclorite::Candidate candidate{
            .command = "tool",
            .summary = "fixture",
            .source = "package",
            .installed = false,
            .repository_available = true,
            .cli_capable = true,
        };
        const auto bundle = provider.guide(candidate);
        expect(bundle.learning_resources.size() == 1,
               "exact local TLDR page creates one source-backed learning resource");
        expect(bundle.examples.size() == 2,
               "TLDR fallback extracts at most two command code lines from the exact cached page");
        if (bundle.examples.size() >= 2) {
            expect(bundle.examples[0].text == "tool --scan {{path/to/files}}",
                   "TLDR guidance preserves upstream placeholder syntax verbatim");
            expect(bundle.examples[1].text == "sudo tool --json {{path/to/files}}",
                   "TLDR guidance accepts conservative sudo-prefixed examples");
            expect(bundle.examples[0].source_kind == acclorite::GuidanceSourceKind::Tldr &&
                       bundle.examples[0].verified &&
                       bundle.examples[0].source_reference.find("tool.md") != std::string::npos,
                   "TLDR example records exact local-page provenance");
        }
        if (!bundle.learning_resources.empty()) {
            expect(bundle.learning_resources[0].target == "tldr tool",
                   "installed TLDR client is exposed only as a learning target, not executed");
            expect(bundle.learning_resources[0].source_kind == acclorite::GuidanceSourceKind::Tldr,
                   "TLDR learning resource preserves community-source type");
        }
        expect(!std::filesystem::exists(marker),
               "TLDR guidance never executes the client while reading the local cache");

        candidate.examples.push_back(acclorite::UsageExample{
            .text = "tool --from-man",
            .source_kind = acclorite::GuidanceSourceKind::Man,
            .source_reference = "man:tool",
            .verified = true,
        });
        const auto lower_priority = provider.guide(candidate);
        expect(lower_priority.learning_resources.size() == 1,
               "TLDR remains a learning resource after a higher-priority example exists");
        expect(lower_priority.examples.empty(),
               "TLDR does not duplicate examples after man or Info guidance already succeeded");

        candidate.examples.clear();
        candidate.command = "missing-tool";
        const auto missing = provider.guide(candidate);
        expect(missing.examples.empty() && missing.learning_resources.empty(),
               "TLDR missing-page path is exact local filesystem lookup only");
        expect(!std::filesystem::exists(marker),
               "TLDR cache miss does not spawn the client or trigger network-capable behavior");

        {
            std::ofstream wrong(pages / "wrong.md");
            wrong << "# different-command\n\n`wrong --unsafe`\n";
        }
        candidate.command = "wrong";
        const auto wrong_heading = provider.guide(candidate);
        expect(wrong_heading.examples.empty() && wrong_heading.learning_resources.empty(),
               "TLDR filename alone is insufficient; page heading must prove exact command identity");

        {
            std::ofstream empty_examples(pages / "noexample.md");
            empty_examples << "# noexample\n\n> Fixture.\n\n`printf nope`\n";
        }
        candidate.command = "noexample";
        const auto no_examples = provider.guide(candidate);
        expect(no_examples.learning_resources.size() == 1 && no_examples.examples.empty(),
               "TLDR may verify a local page while conservatively finding no usable example");
        acclorite::SearchResult tldr_only_result;
        candidate.learning_resources = no_examples.learning_resources;
        tldr_only_result.candidates.push_back(candidate);
        std::ostringstream tldr_only_terminal;
        acclorite::TerminalRenderer{}.render(tldr_only_result, tldr_only_terminal);
        expect(tldr_only_terminal.str().find("No verified example found") == std::string::npos &&
               tldr_only_terminal.str().find("Learn") != std::string::npos,
               "terminal omits empty example sections for verified TLDR-only guidance");
    }

    std::filesystem::remove_all(temp);
}

void test_tldr_guidance_honors_tealdeer_configured_cache() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-tldr-config-test";
    std::filesystem::remove_all(temp);
    const auto config_dir = temp / "config";
    const auto cache_dir = temp / "custom-cache";
    const auto pages = cache_dir / "tldr-pages/pages.en/linux";
    const auto empty_bin = temp / "empty-bin";
    std::filesystem::create_directories(config_dir);
    std::filesystem::create_directories(pages);
    std::filesystem::create_directories(empty_bin);

    {
        std::ofstream config(config_dir / "config.toml");
        config << "[directories]\n"
                  "cache_dir = \"../custom-cache\" # relative to this config file\n";
    }
    {
        std::ofstream page(pages / "configtool.md");
        page << "# configtool\n\n> Fixture.\n\n`configtool --local-cache`\n";
    }

    {
        ScopedPath scoped_path(empty_bin);
        ScopedEnv home("HOME", (temp / "home").string());
        ScopedEnv xdg_cache("XDG_CACHE_HOME", (temp / "empty-cache").string());
        ScopedEnv xdg_data("XDG_DATA_DIRS", (temp / "empty-data").string());
        ScopedEnv language("LANGUAGE", "");
        ScopedEnv lang("LANG", "en_US.UTF-8");
        ScopedEnv tealdeer_cache("TEALDEER_CACHE_DIR", "");
        ScopedEnv tealdeer_config("TEALDEER_CONFIG_DIR", config_dir.string());

        acclorite::TldrGuidanceProvider provider;
        expect(provider.available(), "TLDR guidance honors tealdeer directories.cache_dir without subprocess discovery");
        const auto bundle = provider.guide(acclorite::Candidate{
            .command = "configtool",
            .summary = "fixture",
            .source = "path",
            .installed = true,
            .cli_capable = true,
        });
        expect(bundle.examples.size() == 1 && bundle.examples[0].text == "configtool --local-cache",
               "tealdeer configured cache is parsed and used for exact local guidance");
        expect(!bundle.learning_resources.empty() &&
                   bundle.learning_resources[0].target.find("configtool.md") != std::string::npos,
               "when no TLDR client is present the verified local page path remains the learning target");
    }

    std::filesystem::remove_all(temp);
}

void test_curated_guidance_is_exact_dated_and_lower_priority() {
    const auto temp = std::filesystem::temp_directory_path() / "acclorite-curated-guidance-test.tsv";
    {
        std::ofstream metadata(temp);
        metadata << "# schema: 1\n"
                    "tool\texample\tstable-example\t\ttool --stable\thttps://example.invalid/tool-docs\t2026-09-06\n"
                    "tool\tresource\tofficial-docs\tOfficial tool documentation\thttps://example.invalid/tool-docs\thttps://example.invalid/tool-docs\t2026-09-06\n"
                    "toolbox\texample\tother-tool\t\ttoolbox --different\thttps://example.invalid/toolbox\t2026-09-06\n"
                    "broken\texample\tmissing-date\t\tbroken --nope\thttps://example.invalid/broken\tnot-a-date\n";
    }

    acclorite::CuratedGuidanceProvider provider(temp, "fixture-curator");
    expect(provider.available(), "curated guidance loads a bounded valid bundled-style metadata file");

    acclorite::Candidate candidate{
        .command = "tool",
        .summary = "fixture",
        .source = "path",
        .installed = true,
        .cli_capable = true,
    };
    const auto bundle = provider.guide(candidate);
    expect(bundle.examples.size() == 1 && bundle.examples[0].text == "tool --stable",
           "curated guidance matches exact command identity and emits verified fallback syntax");
    expect(bundle.learning_resources.size() == 1 &&
               bundle.learning_resources[0].target == "https://example.invalid/tool-docs",
           "curated guidance exposes the verified upstream learning target");
    if (!bundle.examples.empty()) {
        expect(bundle.examples[0].source_kind == acclorite::GuidanceSourceKind::Curated &&
                   bundle.examples[0].verified &&
                   bundle.examples[0].verified_by == "fixture-curator" &&
                   bundle.examples[0].verified_on == "2026-09-06",
               "curated example carries explicit curator and verification date provenance");
    }

    candidate.examples.push_back(acclorite::UsageExample{
        .text = "tool --from-tldr",
        .source_kind = acclorite::GuidanceSourceKind::Tldr,
        .source_reference = "tldr:tool",
        .verified = true,
    });
    const auto lower_priority = provider.guide(candidate);
    expect(lower_priority.examples.empty(),
           "curated examples remain below man, Info, and TLDR source priority");
    expect(lower_priority.learning_resources.size() == 1,
           "curated upstream learning resources remain useful even when a higher-priority example exists");

    candidate.examples.clear();
    candidate.command = "toolbox-extra";
    const auto substring = provider.guide(candidate);
    expect(substring.examples.empty() && substring.learning_resources.empty(),
           "curated metadata never leaks through substring command matching");

    candidate.command = "broken";
    const auto malformed = provider.guide(candidate);
    expect(malformed.examples.empty() && malformed.learning_resources.empty(),
           "malformed undated curated rows are ignored instead of being treated as verified");

    acclorite::SearchResult result;
    candidate.command = "tool";
    candidate.examples = bundle.examples;
    candidate.learning_resources = bundle.learning_resources;
    result.candidates.push_back(candidate);
    std::ostringstream json;
    acclorite::JsonRenderer{}.render(result, json);
    expect(json.str().find("\"schema_version\": 15") != std::string::npos,
           "curated verification metadata advances search JSON to schema 15");
    expect(json.str().find("\"verified_by\": \"fixture-curator\"") != std::string::npos &&
               json.str().find("\"verified_on\": \"2026-09-06\"") != std::string::npos,
           "schema 15 exposes curator identity and verification date machine-readably");

    {
        ScopedEnv no_override("ACCLORITE_CURATED_GUIDANCE", "");
        acclorite::CuratedGuidanceProvider bundled;
        const auto bundled_rg = bundled.guide(acclorite::Candidate{
            .command = "rg",
            .summary = "fixture",
            .source = "path",
            .installed = true,
            .cli_capable = true,
        });
        expect(!bundled_rg.learning_resources.empty() &&
                   bundled_rg.learning_resources.front().verified_by == "acclorite-project",
               "only the bundled Acclorite corpus claims acclorite-project curator provenance");
    }

    {
        ScopedEnv override_path("ACCLORITE_CURATED_GUIDANCE", temp.string());
        acclorite::CuratedGuidanceProvider overridden;
        const auto overridden_tool = overridden.guide(candidate);
        expect(!overridden_tool.learning_resources.empty() &&
                   overridden_tool.learning_resources.front().verified_by == "local-override",
               "environment-supplied curated corpora cannot impersonate Acclorite project provenance");
    }

    std::filesystem::remove(temp);
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
    expect(terminal.str().find("Example") != std::string::npos,
           "terminal result exposes verified example block");
    expect(terminal.str().find("docs guide-tool") != std::string::npos,
           "terminal result exposes learning resource target");

    std::ostringstream json;
    acclorite::JsonRenderer{}.render(with_guidance, json);
    expect(json.str().find("\"schema_version\": 15") != std::string::npos,
           "guidance fields remain visible in search JSON schema 15");
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


void test_terminal_ux_is_hierarchical_and_color_is_opt_in() {
    acclorite::SearchResult result;
    result.raw_query = "what is rg";
    result.normalized_query = result.raw_query;
    result.frame = acclorite::query::recognize_frame(acclorite::Query::parse(result.raw_query));
    result.targets = {"rg"};
    result.confidence = acclorite::Confidence{
        .top_candidate = 0.98,
        .interpretation = 0.95,
        .separation = 1.0,
        .semantic_gap = 1.0,
        .utility_gap = 1.0,
        .ambiguity = acclorite::AmbiguityState::Clear,
        .signals = {},
    };
    acclorite::Candidate candidate{
        .command = "rg",
        .path = "/usr/bin/rg",
        .summary = "search text recursively",
        .source = "path+man",
        .installed = true,
        .cli_capable = true,
        .matched_terms = {"search", "text"},
        .score = 1.0,
    };
    candidate.examples.push_back(acclorite::UsageExample{
        .text = "rg {{pattern}}",
        .source_kind = acclorite::GuidanceSourceKind::Tldr,
        .source_reference = "tldr:/cache/rg.md",
        .verified = true,
    });
    candidate.learning_resources.push_back(acclorite::LearningResource{
        .label = "Local manual",
        .target = "man rg",
        .source_kind = acclorite::GuidanceSourceKind::Man,
        .source_reference = "man:rg",
        .verified = true,
    });
    result.candidates.push_back(std::move(candidate));

    std::ostringstream plain;
    acclorite::TerminalRenderer{}.render(result, plain);
    expect(plain.str().find("Explain · rg") != std::string::npos,
           "terminal UX combines frame and target into a visible context heading");
    expect(plain.str().find("✓ installed · /usr/bin/rg · CLI") != std::string::npos,
           "terminal UX keeps installation, path, and interface on one compact row");
    expect(plain.str().find("Package") == std::string::npos &&
           plain.str().find("Matched") == std::string::npos &&
           plain.str().find("Sources") == std::string::npos,
           "normal terminal UX hides diagnostic metadata");
    expect(plain.str().find("Template  rg {{pattern}} · tldr") != std::string::npos &&
           plain.str().find("Learn") != std::string::npos &&
           plain.str().find("Confidence") == std::string::npos,
           "terminal UX keeps guidance compact and omits redundant clear confidence");
    expect(plain.str().find("tldr:/cache/rg.md") == std::string::npos,
           "normal terminal UX hides internal tldr cache paths");
    expect(plain.str().find("\x1b[") == std::string::npos,
           "default renderer output remains ANSI-free for tests and redirected output");

    std::ostringstream colored;
    acclorite::TerminalRenderer{true}.render(result, colored);
    expect(colored.str().find("\x1b[") != std::string::npos,
           "interactive renderer can add restrained ANSI styling");

    acclorite::diagnostics::DoctorReport doctor;
    doctor.checks.push_back(acclorite::diagnostics::DoctorCheck{
        .id = "index",
        .section = "Core",
        .label = "Persistent index",
        .state = acclorite::diagnostics::DoctorState::Ready,
        .detail = "/tmp/index.db; fresh",
        .hint = "",
    });
    std::ostringstream doctor_plain;
    acclorite::DoctorTerminalRenderer{}.render(doctor, doctor_plain);
    expect(doctor_plain.str().find("✓ healthy") != std::string::npos,
           "Doctor UX leads with overall health");
    expect(doctor_plain.str().find("1 checks · 1 ready") != std::string::npos,
           "Doctor UX summarizes check counts before details");
    expect(doctor_plain.str().find("Read-only diagnostic · no changes were made") != std::string::npos,
           "Doctor UX makes read-only behavior obvious");
    expect(doctor_plain.str().find("\x1b[") == std::string::npos,
           "plain Doctor renderer stays ANSI-free");
}


class HintInspectionSource final : public acclorite::KnowledgeSource {
public:
    bool available() const override { return true; }
    std::string_view diagnostic_name() const override { return "hint-test"; }

    std::vector<acclorite::Candidate> search(const acclorite::Query&) const override {
        return {acclorite::Candidate{
            .command = "decoy",
            .path = "/usr/bin/decoy",
            .summary = "generic unrelated helper",
            .source = "man",
            .installed = true,
            .cli_capable = true,
            .semantic_fit = 0.74,
            .score = 0.74,
        }};
    }

    std::vector<acclorite::Candidate> inspect_commands(
        const acclorite::Query&,
        std::span<const std::string> commands
    ) const override {
        std::vector<acclorite::Candidate> result;
        if (std::find(commands.begin(), commands.end(), "right-tool") != commands.end()) {
            result.push_back(acclorite::Candidate{
                .command = "right-tool",
                .path = "/usr/bin/right-tool",
                .summary = "inspect process ancestry as a tree",
                .source = "man",
                .installed = true,
                .cli_capable = true,
                .semantic_fit = 0.90,
                .score = 0.90,
            });
            // SearchEngine must reject inspection spillover that was never hinted.
            result.push_back(acclorite::Candidate{
                .command = "smuggled-tool",
                .path = "/usr/bin/smuggled-tool",
                .summary = "must not enter the candidate pool",
                .source = "man",
                .installed = true,
                .cli_capable = true,
                .semantic_fit = 0.99,
                .score = 0.99,
            });
        }
        return result;
    }
};

void test_candidate_hints_only_expand_recall_through_source_inspection() {
    acclorite::SearchEngine engine;
    engine.add_source(std::make_unique<HintInspectionSource>());
    const auto query = acclorite::Query::parse("show process ancestry as a tree");

    const auto baseline = engine.search(query, 10);
    expect(!baseline.candidates.empty() && baseline.candidates.front().command == "decoy",
           "baseline search does not manufacture an unreturned hinted candidate");

    const std::vector<acclorite::CandidateHint> hints{
        {.command = "right-tool", .relevance_floor = 0.0},
        {.command = "right-tool", .relevance_floor = 0.0},
        {.command = "bad/hint", .relevance_floor = 0.0},
    };
    const auto hinted = engine.search(query, 10, false, false, hints);
    expect(!hinted.candidates.empty() && hinted.candidates.front().command == "right-tool",
           "source-substantiated candidate hint can expand recall before normal ranking");
    const auto right = std::find_if(hinted.candidates.begin(), hinted.candidates.end(), [](const auto& candidate) {
        return candidate.command == "right-tool";
    });
    expect(right != hinted.candidates.end() && right->source == "man",
           "hint identity itself never becomes provenance or a semantic evidence source");
    expect(std::none_of(hinted.candidates.begin(), hinted.candidates.end(), [](const auto& candidate) {
        return candidate.command == "smuggled-tool";
    }), "source inspection cannot inject candidates outside the bounded hint set");
}


class RankedHintInspectionSource final : public acclorite::KnowledgeSource {
public:
    bool available() const override { return true; }
    std::string_view diagnostic_name() const override { return "ranked-hint-test"; }

    std::vector<acclorite::Candidate> search(const acclorite::Query&) const override {
        return {acclorite::Candidate{
            .command = "lexical-decoy",
            .path = "/usr/bin/lexical-decoy",
            .summary = "process helper with lexical overlap",
            .source = "man",
            .installed = true,
            .cli_capable = true,
            .semantic_fit = 0.76,
            .score = 0.76,
        }};
    }

    std::vector<acclorite::Candidate> inspect_commands(
        const acclorite::Query&,
        std::span<const std::string> commands
    ) const override {
        if (std::find(commands.begin(), commands.end(), "semantic-tool") == commands.end()) {
            return {};
        }
        return {acclorite::Candidate{
            .command = "semantic-tool",
            .path = "/usr/bin/semantic-tool",
            .summary = "source-backed but lexically distant process ancestry tool",
            .source = "man",
            .installed = true,
            .cli_capable = true,
            .semantic_fit = 0.40,
            .score = 0.40,
        }};
    }
};

void test_ranked_candidate_hint_adds_only_bounded_relevance_support() {
    acclorite::SearchEngine engine;
    engine.add_source(std::make_unique<RankedHintInspectionSource>());
    const auto query = acclorite::Query::parse("show process ancestry as a tree");

    const std::vector<acclorite::CandidateHint> identity_only{{
        .command = "semantic-tool", .relevance_floor = 0.0
    }};
    const auto no_floor = engine.search(query, 10, false, false, identity_only);
    expect(!no_floor.candidates.empty() && no_floor.candidates.front().command == "lexical-decoy",
           "identity-only hint does not override ordinary semantic ranking");

    const std::vector<acclorite::CandidateHint> ranked{{
        .command = "semantic-tool", .relevance_floor = 0.84
    }};
    const auto promoted = engine.search(query, 10, true, false, ranked);
    expect(!promoted.candidates.empty() && promoted.candidates.front().command == "semantic-tool",
           "source-substantiated ranked hint may contribute bounded relevance support");
    const auto semantic = std::find_if(promoted.candidates.begin(), promoted.candidates.end(), [](const auto& candidate) {
        return candidate.command == "semantic-tool";
    });
    expect(semantic != promoted.candidates.end() && semantic->source == "man",
           "ranked relevance support never becomes provenance");
    expect(semantic != promoted.candidates.end() && semantic->ranking.has_value() &&
           std::any_of(semantic->ranking->semantic_adjustments.begin(), semantic->ranking->semantic_adjustments.end(),
                       [](const auto& adjustment) { return adjustment.id == "retrieval-relevance-floor"; }),
           "ranking diagnostics expose the bounded retrieval relevance floor");

    const std::vector<acclorite::CandidateHint> excessive{{
        .command = "semantic-tool", .relevance_floor = 0.97
    }};
    const auto rejected = engine.search(query, 10, false, false, excessive);
    expect(std::none_of(rejected.candidates.begin(), rejected.candidates.end(), [](const auto& candidate) {
        return candidate.command == "semantic-tool";
    }), "hint relevance above the bounded ceiling is rejected rather than creating exact authority");
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
    expect(json.find("\"schema_version\": 15") != std::string::npos, "JSON schema advances for performance profiling output");
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
    test_human_discovery_modifiers_do_not_dilute_parent_tool_intent();
    test_query_relative_role_specificity_handles_hostile_human_collisions();
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
    test_candidate_hints_only_expand_recall_through_source_inspection();
    test_ranked_candidate_hint_adds_only_bounded_relevance_support();
    test_man_syntax_provider_extracts_verified_grammar_without_executing_target();
    test_fish_completion_syntax_provider_parses_static_options_without_executing_shell();
    test_syntax_evidence_merging_preserves_authority_and_combines_parent_child_facts();
    test_parent_proven_child_man_grammar_resolves_compound_operation_without_executing_targets();
    test_explicit_option_answer_is_verified_post_ranking_and_machine_visible();
    test_child_synopsis_completeness_requires_explicit_compatible_path();
    test_binder_only_consumes_operation_paraphrases_when_operation_proves_them();
    test_hidden_files_paraphrase_is_not_rebound_as_root_search_data();
    test_safety_classifier_requires_complete_source_backed_alignment();
    test_root_synopsis_binding_handles_practical_multiargument_commands();
    test_shell_renderer_quotes_structured_invocations();
    test_syntax_provider_is_not_touched_for_ordinary_discovery();
    test_man_guidance_extracts_only_source_backed_examples();
    test_info_guidance_is_verified_and_falls_back_after_man();
    test_tldr_guidance_reads_exact_local_cache_without_spawning_client();
    test_tldr_guidance_honors_tealdeer_configured_cache();
    test_curated_guidance_is_exact_dated_and_lower_priority();
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
    test_action_target_parser_separates_command_syntax_from_language();
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
    test_distro_detection_and_package_family_selection();
    test_package_backends_share_common_interface();
    test_apt_source_discovers_uninstalled_debian_tool_offline();
    test_apt_installed_metadata_merges_with_local_tool();
    test_dnf_source_discovers_uninstalled_fedora_tool_offline();
    test_dnf4_frontend_fallback_and_installed_metadata_merge();
    test_zypper_source_discovers_uninstalled_opensuse_tool_offline();
    test_zypper_installed_metadata_merges_with_path();
    test_xbps_source_discovers_uninstalled_void_tool_offline();
    test_xbps_installed_metadata_merges_with_path();
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
    test_failed_index_rebuild_preserves_last_good_snapshot();
    test_index_keeps_hybrid_metadata();
    test_index_typo_query_uses_canonical_concepts();
    test_index_recovers_misspelled_command_name();
    test_index_fingerprints_detect_path_change_and_auto_refresh();
    test_fresh_index_does_not_rebuild_on_every_probe();
    test_index_fingerprints_detect_manual_and_desktop_changes();
#endif
    test_doctor_reports_debian_backend_readiness();
    test_doctor_reports_fedora_backend_readiness();
    test_doctor_reports_opensuse_backend_readiness();
    test_doctor_reports_void_backend_readiness();
    test_doctor_is_read_only_when_index_is_missing();
    test_doctor_reports_stale_index_without_refreshing_it();
    test_doctor_distinguishes_pkgfile_binary_from_metadata_readiness();
    test_doctor_json_is_machine_readable_and_declares_no_mutation();
    test_terminal_ux_is_hierarchical_and_color_is_opt_in();
    test_json_renderer_escapes();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "All Acclorite tests passed.\n";
    return 0;
}
