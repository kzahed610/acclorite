#include "acclorite/sources/index_source.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sqlite3.h>

#include "acclorite/query/lexicon.hpp"
#include "acclorite/query/relevance.hpp"
#include "acclorite/sources/desktop_source.hpp"
#include "acclorite/sources/man_source.hpp"
#include "acclorite/sources/path_source.hpp"

namespace acclorite {
namespace {

constexpr int kSchemaVersion = 1;

struct SqliteCloser {
    void operator()(sqlite3* db) const {
        if (db) {
            sqlite3_close(db);
        }
    }
};
using Database = std::unique_ptr<sqlite3, SqliteCloser>;

struct StatementCloser {
    void operator()(sqlite3_stmt* statement) const {
        if (statement) {
            sqlite3_finalize(statement);
        }
    }
};
using Statement = std::unique_ptr<sqlite3_stmt, StatementCloser>;

struct IndexRecord {
    Candidate candidate;
    std::string search_text;
};

Database open_database(const std::filesystem::path& path) {
    sqlite3* raw = nullptr;
    const int rc = sqlite3_open_v2(
        path.c_str(),
        &raw,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr
    );
    Database db(raw);
    if (rc != SQLITE_OK || !db) {
        return {};
    }
    sqlite3_busy_timeout(db.get(), 1500);
    return db;
}

bool exec(sqlite3* db, const char* sql) {
    char* error = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
    if (error) {
        sqlite3_free(error);
    }
    return rc == SQLITE_OK;
}

Statement prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK) {
        return {};
    }
    return Statement(raw);
}

std::string text_column(sqlite3_stmt* statement, const int column) {
    const auto* raw = sqlite3_column_text(statement, column);
    return raw ? reinterpret_cast<const char*>(raw) : std::string{};
}

bool placeholder_summary(const std::string_view summary) {
    return summary.empty() || summary == "Executable available in PATH";
}

void append_unique_source(std::string& target, const std::string_view source) {
    if (source.empty()) {
        return;
    }
    if (target.empty()) {
        target = source;
        return;
    }
    if (target.find(source) == std::string::npos) {
        target += "+";
        target += source;
    }
}

void append_search_text(std::string& target, const std::string_view text) {
    if (text.empty() || text == "Executable available in PATH") {
        return;
    }
    if (!target.empty()) {
        target.push_back(' ');
    }
    target.append(text);
}

void merge_record(IndexRecord& target, const Candidate& incoming) {
    if (target.candidate.command.empty()) {
        target.candidate.command = incoming.command;
    }
    if (target.candidate.path.empty() && !incoming.path.empty()) {
        target.candidate.path = incoming.path;
    }

    // Official command descriptions are preferred for display. Desktop metadata is
    // still retained in search_text and can therefore improve retrieval/classification.
    const bool incoming_man = incoming.source.find("man") != std::string::npos;
    const bool target_man = target.candidate.source.find("man") != std::string::npos;
    if (!placeholder_summary(incoming.summary) &&
        (placeholder_summary(target.candidate.summary) || (incoming_man && !target_man))) {
        target.candidate.summary = incoming.summary;
    }

    append_unique_source(target.candidate.source, incoming.source);
    target.candidate.installed = target.candidate.installed || incoming.installed;
    target.candidate.repository_available =
        target.candidate.repository_available || incoming.repository_available;
    target.candidate.cli_capable = target.candidate.cli_capable || incoming.cli_capable;
    target.candidate.gui_capable = target.candidate.gui_capable || incoming.gui_capable;
    append_search_text(target.search_text, incoming.summary);
}

std::unordered_map<std::string, IndexRecord> collect_records() {
    std::unordered_map<std::string, IndexRecord> records;

    const auto collect = [&](const std::vector<Candidate>& incoming) {
        for (const auto& candidate : incoming) {
            if (candidate.command.empty()) {
                continue;
            }
            auto& record = records[candidate.command];
            merge_record(record, candidate);
        }
    };

    collect(PathSource::catalog());
    collect(ManSource::catalog());
    collect(DesktopSource::catalog());

    for (auto& [command, record] : records) {
        if (record.candidate.summary.empty()) {
            record.candidate.summary = "Executable available in PATH";
        }
        if (record.search_text.empty()) {
            record.search_text = record.candidate.summary;
        }
        // The command itself is intentionally present in search text in addition to
        // its heavily weighted dedicated FTS column.
        record.search_text += " ";
        record.search_text += command;
    }

    return records;
}

bool schema_ready(sqlite3* db) {
    auto statement = prepare(
        db,
        "SELECT value FROM metadata WHERE key='schema_version' LIMIT 1"
    );
    if (!statement || sqlite3_step(statement.get()) != SQLITE_ROW) {
        return false;
    }

    const int version = std::atoi(text_column(statement.get(), 0).c_str());
    if (version != kSchemaVersion) {
        return false;
    }

    auto count = prepare(db, "SELECT count(*) FROM tools_fts");
    return count && sqlite3_step(count.get()) == SQLITE_ROW &&
           sqlite3_column_int64(count.get(), 0) > 0;
}

bool initialize_schema(sqlite3* db) {
    return exec(db, "PRAGMA journal_mode=WAL;") &&
           exec(db, "PRAGMA synchronous=NORMAL;") &&
           exec(db, "CREATE TABLE metadata(key TEXT PRIMARY KEY, value TEXT NOT NULL);") &&
           exec(
               db,
               "CREATE VIRTUAL TABLE tools_fts USING fts5("
               "command, summary, search_text, "
               "path UNINDEXED, source UNINDEXED, installed UNINDEXED, "
               "repository_available UNINDEXED, cli_capable UNINDEXED, gui_capable UNINDEXED, "
               "tokenize='unicode61 remove_diacritics 2'"
               ");"
           );
}

bool write_records(sqlite3* db, const std::unordered_map<std::string, IndexRecord>& records) {
    if (!exec(db, "BEGIN IMMEDIATE;")) {
        return false;
    }

    auto statement = prepare(
        db,
        "INSERT INTO tools_fts("
        "command, summary, search_text, path, source, installed, repository_available, "
        "cli_capable, gui_capable"
        ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)"
    );
    if (!statement) {
        exec(db, "ROLLBACK;");
        return false;
    }

    for (const auto& [_, record] : records) {
        const Candidate& candidate = record.candidate;
        sqlite3_reset(statement.get());
        sqlite3_clear_bindings(statement.get());

        sqlite3_bind_text(statement.get(), 1, candidate.command.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement.get(), 2, candidate.summary.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement.get(), 3, record.search_text.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement.get(), 4, candidate.path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement.get(), 5, candidate.source.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(statement.get(), 6, candidate.installed ? 1 : 0);
        sqlite3_bind_int(statement.get(), 7, candidate.repository_available ? 1 : 0);
        sqlite3_bind_int(statement.get(), 8, candidate.cli_capable ? 1 : 0);
        sqlite3_bind_int(statement.get(), 9, candidate.gui_capable ? 1 : 0);

        if (sqlite3_step(statement.get()) != SQLITE_DONE) {
            exec(db, "ROLLBACK;");
            return false;
        }
    }

    auto metadata = prepare(
        db,
        "INSERT INTO metadata(key, value) VALUES('schema_version', ?)"
    );
    if (!metadata) {
        exec(db, "ROLLBACK;");
        return false;
    }

    const std::string version = std::to_string(kSchemaVersion);
    sqlite3_bind_text(metadata.get(), 1, version.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(metadata.get()) != SQLITE_DONE) {
        exec(db, "ROLLBACK;");
        return false;
    }

    return exec(db, "COMMIT;");
}

std::string fts_query(const Query& query) {
    const auto terms = query::retrieval_terms(query, 28);
    std::vector<std::string> clauses;
    clauses.reserve(terms.size() * 2);

    const auto quote = [](std::string_view input) {
        std::string escaped;
        escaped.reserve(input.size() + 2);
        escaped.push_back('"');
        for (const char ch : input) {
            if (ch == '"') {
                escaped += "\"\"";
            } else {
                escaped.push_back(ch);
            }
        }
        escaped.push_back('"');
        return escaped;
    };

    for (const auto& term : terms) {
        if (term.empty()) {
            continue;
        }
        clauses.push_back(quote(term));

        // Retrieval-only morphology. Strict semantic scoring happens after FTS,
        // so a five-character prefix can safely increase recall here.
        if (term.size() >= 6) {
            clauses.push_back(quote(std::string_view(term).substr(0, 5)) + "*");
        }
    }

    std::string result;
    for (const auto& clause : clauses) {
        if (!result.empty()) {
            result += " OR ";
        }
        result += clause;
    }
    return result;
}

} // namespace

std::filesystem::path IndexSource::database_path() {
    if (const char* override_path = std::getenv("ACCLORITE_INDEX_PATH")) {
        return override_path;
    }

    if (const char* cache_home = std::getenv("XDG_CACHE_HOME")) {
        return std::filesystem::path(cache_home) / "acclorite/index.db";
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".cache/acclorite/index.db";
    }
    return std::filesystem::temp_directory_path() / "acclorite-index.db";
}

bool IndexSource::rebuild() const {
    const auto path = database_path();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }

    // A cache database is disposable. Rebuilding into a clean file also makes schema
    // migration failure recovery trivial during development.
    std::filesystem::remove(path, error);
    std::filesystem::remove(path.string() + "-wal", error);
    std::filesystem::remove(path.string() + "-shm", error);

    auto db = open_database(path);
    if (!db || !initialize_schema(db.get())) {
        return false;
    }

    const auto records = collect_records();
    if (records.empty()) {
        return false;
    }

    return write_records(db.get(), records);
}

bool IndexSource::ensure_ready() const {
    const auto path = database_path();
    if (std::filesystem::exists(path)) {
        auto db = open_database(path);
        if (db && schema_ready(db.get())) {
            return true;
        }
    }
    return rebuild();
}

bool IndexSource::available() const {
    return ensure_ready();
}

std::vector<Candidate> IndexSource::search(const Query& query) const {
    if (!ensure_ready()) {
        return {};
    }

    const std::string match_query = fts_query(query);
    if (match_query.empty()) {
        return {};
    }

    auto db = open_database(database_path());
    if (!db) {
        return {};
    }

    auto statement = prepare(
        db.get(),
        "SELECT command, summary, search_text, path, source, installed, "
        "repository_available, cli_capable, gui_capable, "
        "bm25(tools_fts, 5.0, 2.0, 1.0) AS rank "
        "FROM tools_fts WHERE tools_fts MATCH ? ORDER BY rank LIMIT 160"
    );
    if (!statement) {
        return {};
    }

    sqlite3_bind_text(statement.get(), 1, match_query.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<Candidate> result;
    std::size_t position = 0;

    while (sqlite3_step(statement.get()) == SQLITE_ROW) {
        const std::string command = text_column(statement.get(), 0);
        const std::string summary = text_column(statement.get(), 1);
        const std::string search_text = text_column(statement.get(), 2);

        auto relevance = query::score_text(query, command, search_text, 0.99, 0.96);
        if (relevance.score <= 0.0) {
            ++position;
            continue;
        }

        // FTS/BM25 is the recall/ranking hint; concept coverage remains authoritative.
        // This small position bonus mainly breaks ties between equally semantic hits.
        const double bm25_bonus = std::max(0.0, 0.055 - (static_cast<double>(position) * 0.00035));

        result.push_back(Candidate{
            .command = command,
            .path = text_column(statement.get(), 3),
            .summary = summary,
            .source = text_column(statement.get(), 4),
            .installed = sqlite3_column_int(statement.get(), 5) != 0,
            .repository_available = sqlite3_column_int(statement.get(), 6) != 0,
            .cli_capable = sqlite3_column_int(statement.get(), 7) != 0,
            .gui_capable = sqlite3_column_int(statement.get(), 8) != 0,
            .matched_terms = std::move(relevance.matched_terms),
            .score = std::min(1.0, relevance.score + bm25_bonus),
        });
        ++position;
    }

    return result;
}

} // namespace acclorite
