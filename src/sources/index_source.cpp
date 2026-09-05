#include "acclorite/sources/index_source.hpp"

#include <algorithm>
#include <cstdint>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sqlite3.h>

#include "acclorite/query/fuzzy.hpp"
#include "acclorite/query/lexicon.hpp"
#include "acclorite/query/relevance.hpp"
#include "acclorite/ranking/evidence.hpp"
#include "acclorite/sources/desktop_source.hpp"
#include "acclorite/sources/man_source.hpp"
#include "acclorite/sources/path_source.hpp"
#include "acclorite/system/executable.hpp"

namespace acclorite {
namespace {

constexpr int kSchemaVersion = 1;
constexpr int kFingerprintVersion = 1;
constexpr int kQuickFingerprintVersion = 1;
constexpr std::int64_t kDeepVerificationIntervalSeconds = 6 * 60 * 60;

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

struct SourceFingerprints {
    std::string path;
    std::string manuals;
    std::string desktop;
};

struct QuickFingerprints {
    std::string path;
    std::string manuals;
    std::string desktop;
};

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void hash_text(std::uint64_t& hash, const std::string_view text) {
    for (const unsigned char ch : text) {
        hash ^= ch;
        hash *= kFnvPrime;
    }
    hash ^= 0xff;
    hash *= kFnvPrime;
}

std::string hash_hex(const std::uint64_t hash) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(16, '0');
    auto value = hash;
    for (int i = 15; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = digits[value & 0x0f];
        value >>= 4;
    }
    return out;
}

void hash_path_metadata(std::uint64_t& hash, const std::filesystem::path& path) {
    hash_text(hash, path.string());
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error) {
        hash_text(hash, "missing");
        return;
    }

    hash_text(hash, std::to_string(static_cast<unsigned>(status.type())));
    const auto modified = std::filesystem::last_write_time(path, error);
    if (!error) {
        hash_text(hash, std::to_string(modified.time_since_epoch().count()));
    }
    error.clear();
    if (std::filesystem::is_regular_file(status)) {
        const auto size = std::filesystem::file_size(path, error);
        if (!error) {
            hash_text(hash, std::to_string(size));
        }
    }
}

std::vector<std::filesystem::path> split_path_list(const char* raw) {
    std::vector<std::filesystem::path> paths;
    if (!raw) {
        return paths;
    }
    const std::string text(raw);
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find(':', start);
        const auto item = text.substr(start, end - start);
        if (!item.empty()) {
            paths.emplace_back(item);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return paths;
}

std::string fingerprint_path_source() {
    std::uint64_t hash = kFnvOffset;
    hash_text(hash, "path-v1");
    hash_text(hash, std::getenv("PATH") ? std::getenv("PATH") : "");
    for (const auto& directory : system::path_directories()) {
        hash_path_metadata(hash, directory);
    }
    return hash_hex(hash);
}

std::string fingerprint_desktop_source() {
    std::uint64_t hash = kFnvOffset;
    hash_text(hash, "desktop-v1");

    for (const auto& directory : DesktopSource::application_directories()) {
        hash_path_metadata(hash, directory);
        std::error_code error;
        if (!std::filesystem::is_directory(directory, error)) {
            continue;
        }

        std::vector<std::filesystem::path> entries;
        for (std::filesystem::directory_iterator it(
                 directory,
                 std::filesystem::directory_options::skip_permission_denied,
                 error
             ); !error && it != std::filesystem::directory_iterator{}; it.increment(error)) {
            if (it->path().extension() == ".desktop") {
                entries.push_back(it->path());
            }
        }
        std::ranges::sort(entries);
        for (const auto& entry : entries) {
            hash_path_metadata(hash, entry);
        }
    }
    return hash_hex(hash);
}

std::vector<std::filesystem::path> man_roots() {
    if (const char* manpath = std::getenv("MANPATH")) {
        const auto configured = split_path_list(manpath);
        if (!configured.empty()) {
            return configured;
        }
    }

    std::vector<std::filesystem::path> roots{
        "/usr/local/share/man",
        "/usr/share/man",
        "/usr/local/man",
        "/usr/man",
    };
    if (const char* home = std::getenv("HOME")) {
        roots.emplace_back(std::filesystem::path(home) / ".local/share/man");
    }
    return roots;
}

void hash_directory_children(
    std::uint64_t& hash,
    const std::filesystem::path& root,
    const int depth
) {
    hash_path_metadata(hash, root);
    if (depth <= 0) {
        return;
    }

    std::error_code error;
    if (!std::filesystem::is_directory(root, error)) {
        return;
    }

    std::vector<std::filesystem::path> children;
    for (std::filesystem::directory_iterator it(
             root,
             std::filesystem::directory_options::skip_permission_denied,
             error
         ); !error && it != std::filesystem::directory_iterator{}; it.increment(error)) {
        const auto path = it->path();
        std::error_code item_error;
        if (std::filesystem::is_directory(path, item_error) || path.filename() == "index.db") {
            children.push_back(path);
        }
    }
    std::ranges::sort(children);
    for (const auto& child : children) {
        std::error_code child_error;
        if (std::filesystem::is_directory(child, child_error)) {
            hash_directory_children(hash, child, depth - 1);
        } else {
            hash_path_metadata(hash, child);
        }
    }
}

std::string fingerprint_manual_source() {
    std::uint64_t hash = kFnvOffset;
    hash_text(hash, "manual-v1");

    if (const auto apropos = system::find_executable("apropos")) {
        hash_path_metadata(hash, *apropos);
    } else if (const auto man = system::find_executable("man")) {
        hash_path_metadata(hash, *man);
    } else {
        hash_text(hash, "no-man-helper");
    }

    for (const auto& root : man_roots()) {
        // Directory mtimes catch normal package add/remove/replace operations without
        // stat'ing every compressed man page on every Acclorite invocation.
        hash_directory_children(hash, root, 2);
    }

    const std::vector<std::filesystem::path> cache_roots{
        "/var/cache/man",
        "/usr/local/var/cache/man",
        std::getenv("HOME")
            ? std::filesystem::path(std::getenv("HOME")) / ".cache/man"
            : std::filesystem::path{},
    };
    for (const auto& root : cache_roots) {
        if (!root.empty()) {
            hash_directory_children(hash, root, 2);
        }
    }
    return hash_hex(hash);
}

std::string fingerprint_desktop_source_quick() {
    std::uint64_t hash = kFnvOffset;
    hash_text(hash, "desktop-quick-v1");
    for (const auto& directory : DesktopSource::application_directories()) {
        // A package install/remove/replace changes the application directory mtime.
        // Avoid stat'ing every .desktop file on every CLI startup; the periodic deep
        // fingerprint and `doctor` still catch unusual in-place edits.
        hash_path_metadata(hash, directory);
    }
    return hash_hex(hash);
}

std::string fingerprint_manual_source_quick() {
    std::uint64_t hash = kFnvOffset;
    hash_text(hash, "manual-quick-v1");

    if (const auto apropos = system::find_executable("apropos")) {
        hash_path_metadata(hash, *apropos);
    } else if (const auto man = system::find_executable("man")) {
        hash_path_metadata(hash, *man);
    } else {
        hash_text(hash, "no-man-helper");
    }

    for (const auto& root : man_roots()) {
        // Hot-path freshness only walks the root and immediate man/language
        // directories. Package installs/removals normally change one of these
        // directory mtimes, while the deeper doctor probe remains available for
        // exhaustive verification. Crucially, this avoids iterating every man-page
        // directory entry on every CLI process startup.
        hash_directory_children(hash, root, 1);
    }

    const std::vector<std::filesystem::path> cache_roots{
        "/var/cache/man",
        "/usr/local/var/cache/man",
        std::getenv("HOME")
            ? std::filesystem::path(std::getenv("HOME")) / ".cache/man"
            : std::filesystem::path{},
    };
    for (const auto& root : cache_roots) {
        if (!root.empty()) {
            hash_directory_children(hash, root, 1);
        }
    }
    return hash_hex(hash);
}

SourceFingerprints current_fingerprints() {
    return SourceFingerprints{
        .path = fingerprint_path_source(),
        .manuals = fingerprint_manual_source(),
        .desktop = fingerprint_desktop_source(),
    };
}

QuickFingerprints current_quick_fingerprints() {
    return QuickFingerprints{
        .path = fingerprint_path_source(),
        .manuals = fingerprint_manual_source_quick(),
        // Desktop catalogs are usually small enough that retaining the exact
        // per-file metadata fingerprint is cheap and preserves in-place edit
        // detection. The manual tree is the expensive source that needs a fast tier.
        .desktop = fingerprint_desktop_source_quick(),
    };
}

std::int64_t current_epoch_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

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

std::optional<std::string> metadata_value(sqlite3* db, const std::string_view key) {
    auto statement = prepare(db, "SELECT value FROM metadata WHERE key=? LIMIT 1");
    if (!statement) {
        return std::nullopt;
    }
    sqlite3_bind_text(
        statement.get(), 1, key.data(), static_cast<int>(key.size()), SQLITE_TRANSIENT
    );
    if (sqlite3_step(statement.get()) != SQLITE_ROW) {
        return std::nullopt;
    }
    return text_column(statement.get(), 0);
}

bool write_metadata(sqlite3* db, const std::string_view key, const std::string_view value) {
    auto statement = prepare(db, "INSERT INTO metadata(key, value) VALUES(?, ?)");
    if (!statement) {
        return false;
    }
    sqlite3_bind_text(statement.get(), 1, key.data(), static_cast<int>(key.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(statement.get(), 2, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    return sqlite3_step(statement.get()) == SQLITE_DONE;
}

bool upsert_metadata(sqlite3* db, const std::string_view key, const std::string_view value) {
    auto statement = prepare(
        db,
        "INSERT INTO metadata(key, value) VALUES(?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value"
    );
    if (!statement) {
        return false;
    }
    sqlite3_bind_text(statement.get(), 1, key.data(), static_cast<int>(key.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(statement.get(), 2, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    return sqlite3_step(statement.get()) == SQLITE_DONE;
}

bool placeholder_summary(const std::string_view summary) {
    return summary.empty() || summary == "Executable available in PATH";
}

void append_unique_source(std::string& target, const std::string_view source) {
    merge_sources(target, source);
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
    const bool incoming_man = source_contains(incoming.source, "man");
    const bool target_man = source_contains(target.candidate.source, "man");
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

bool write_records(
    sqlite3* db,
    const std::unordered_map<std::string, IndexRecord>& records,
    const SourceFingerprints& fingerprints,
    const QuickFingerprints& quick_fingerprints
) {
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

    const bool metadata_ok =
        write_metadata(db, "schema_version", std::to_string(kSchemaVersion)) &&
        write_metadata(db, "fingerprint_version", std::to_string(kFingerprintVersion)) &&
        write_metadata(db, "fingerprint_path", fingerprints.path) &&
        write_metadata(db, "fingerprint_manuals", fingerprints.manuals) &&
        write_metadata(db, "fingerprint_desktop", fingerprints.desktop) &&
        write_metadata(db, "quick_fingerprint_version", std::to_string(kQuickFingerprintVersion)) &&
        write_metadata(db, "quick_fingerprint_path", quick_fingerprints.path) &&
        write_metadata(db, "quick_fingerprint_manuals", quick_fingerprints.manuals) &&
        write_metadata(db, "quick_fingerprint_desktop", quick_fingerprints.desktop) &&
        write_metadata(db, "deep_verified_epoch", std::to_string(current_epoch_seconds()));
    if (!metadata_ok) {
        exec(db, "ROLLBACK;");
        return false;
    }

    return exec(db, "COMMIT;");
}

void evaluate_freshness(sqlite3* db, IndexStatus& status) {
    if (!status.ready) {
        return;
    }

    const auto version = metadata_value(db, "fingerprint_version");
    const auto stored_path = metadata_value(db, "fingerprint_path");
    const auto stored_manuals = metadata_value(db, "fingerprint_manuals");
    const auto stored_desktop = metadata_value(db, "fingerprint_desktop");

    status.fingerprinted = version && std::atoi(version->c_str()) == kFingerprintVersion &&
                           stored_path && stored_manuals && stored_desktop;
    if (!status.fingerprinted) {
        status.stale = true;
        status.stale_sources.push_back("freshness metadata");
        return;
    }

    const auto current = current_fingerprints();
    if (*stored_path != current.path) {
        status.stale_sources.push_back("PATH");
    }
    if (*stored_manuals != current.manuals) {
        status.stale_sources.push_back("manuals");
    }
    if (*stored_desktop != current.desktop) {
        status.stale_sources.push_back("desktop metadata");
    }
    status.stale = !status.stale_sources.empty();
}

bool fingerprints_fresh(sqlite3* db) {
    IndexStatus status{
        .sqlite_supported = true,
        .exists = true,
        .ready = true,
        .fingerprinted = false,
        .stale = false,
        .path = {},
        .size_bytes = 0,
        .stale_sources = {},
    };
    evaluate_freshness(db, status);
    return status.fingerprinted && !status.stale;
}

bool quick_fingerprints_fresh(sqlite3* db) {
    const auto version = metadata_value(db, "quick_fingerprint_version");
    const auto stored_path = metadata_value(db, "quick_fingerprint_path");
    const auto stored_manuals = metadata_value(db, "quick_fingerprint_manuals");
    const auto stored_desktop = metadata_value(db, "quick_fingerprint_desktop");
    const auto last_deep = metadata_value(db, "deep_verified_epoch");
    if (!version || std::atoi(version->c_str()) != kQuickFingerprintVersion ||
        !stored_path || !stored_manuals || !stored_desktop || !last_deep) {
        // Upgrade a pre-0.1 index in place. Pay for the old deep verification once,
        // then seed the cheap fingerprint metadata without rebuilding the semantic
        // catalog or rerunning apropos/desktop discovery.
        if (!fingerprints_fresh(db)) {
            return false;
        }
        const auto seeded = current_quick_fingerprints();
        return upsert_metadata(db, "quick_fingerprint_version", std::to_string(kQuickFingerprintVersion)) &&
               upsert_metadata(db, "quick_fingerprint_path", seeded.path) &&
               upsert_metadata(db, "quick_fingerprint_manuals", seeded.manuals) &&
               upsert_metadata(db, "quick_fingerprint_desktop", seeded.desktop) &&
               upsert_metadata(db, "deep_verified_epoch", std::to_string(current_epoch_seconds()));
    }

    const auto current = current_quick_fingerprints();
    if (*stored_path != current.path || *stored_manuals != current.manuals ||
        *stored_desktop != current.desktop) {
        return false;
    }

    // The fast fingerprint is designed for every invocation. Periodically verify
    // the deeper tree fingerprint as a safety net for unusual nested/in-place
    // documentation changes that do not alter an immediate directory mtime.
    const std::int64_t verified = std::strtoll(last_deep->c_str(), nullptr, 10);
    const std::int64_t now = current_epoch_seconds();
    if (verified <= 0 || now - verified >= kDeepVerificationIntervalSeconds) {
        if (!fingerprints_fresh(db)) {
            return false;
        }
        if (!upsert_metadata(db, "deep_verified_epoch", std::to_string(now))) {
            return false;
        }
    }
    return true;
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

Database open_database_readonly(const std::filesystem::path& path) {
    sqlite3* raw = nullptr;
    const int rc = sqlite3_open_v2(
        path.c_str(),
        &raw,
        SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
        nullptr
    );
    Database db(raw);
    if (rc != SQLITE_OK || !db) {
        return {};
    }
    sqlite3_busy_timeout(db.get(), 1500);
    return db;
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

IndexStatus IndexSource::probe() {
    IndexStatus status{
        .sqlite_supported = true,
        .exists = false,
        .ready = false,
        .fingerprinted = false,
        .stale = false,
        .path = database_path(),
        .size_bytes = 0,
        .stale_sources = {},
    };

    std::error_code error;
    status.exists = std::filesystem::is_regular_file(status.path, error);
    if (!status.exists || error) {
        return status;
    }

    status.size_bytes = std::filesystem::file_size(status.path, error);
    if (error) {
        status.size_bytes = 0;
    }

    auto db = open_database_readonly(status.path);
    status.ready = db && schema_ready(db.get());
    if (status.ready) {
        evaluate_freshness(db.get(), status);
    }
    return status;
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

    // Capture fingerprints after collecting source data so the stored snapshot
    // describes the state that was actually indexed.
    const auto fingerprints = current_fingerprints();
    const auto quick_fingerprints = current_quick_fingerprints();
    return write_records(db.get(), records, fingerprints, quick_fingerprints);
}

bool IndexSource::ensure_ready() const {
    const auto path = database_path();
    if (std::filesystem::exists(path)) {
        auto db = open_database(path);
        if (db && schema_ready(db.get()) && quick_fingerprints_fresh(db.get())) {
            return true;
        }
    }
    return rebuild();
}

bool IndexSource::available() const {
    if (ready_hint_) {
        return true;
    }
    ready_hint_ = ensure_ready();
    return ready_hint_;
}

std::vector<Candidate> IndexSource::search(const Query& query) const {
    if (ready_hint_) {
        ready_hint_ = false;
    } else if (!ensure_ready()) {
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

        const std::string indexed_sources = text_column(statement.get(), 4);
        const double scored = std::min(1.0, relevance.score + bm25_bonus);
        Candidate candidate{
            .command = command,
            .path = text_column(statement.get(), 3),
            .summary = summary,
            .source = indexed_sources,
            .package = {},
            .repository = {},
            .package_version = {},
            .installed = sqlite3_column_int(statement.get(), 5) != 0,
            .repository_available = sqlite3_column_int(statement.get(), 6) != 0,
            .cli_capable = sqlite3_column_int(statement.get(), 7) != 0,
            .gui_capable = sqlite3_column_int(statement.get(), 8) != 0,
            .matched_terms = std::move(relevance.matched_terms),
            .provided_commands = {},
            .descriptive_evidence = {},
                .examples = {},
                .learning_resources = {},
            .evidence_trace = {},
            .base_merge_trace = {},
            .score = scored,
            .ranking = std::nullopt,
        };
        candidate.semantic_fit = relevance.semantic_fit;
        if (query.explain_ranking) {
            std::vector<ranking::RankingAdjustment> source_adjustments;
            if (scored != relevance.score) {
                source_adjustments.push_back(ranking::RankingAdjustment{
                    .id = "fts-bm25",
                    .label = "FTS/BM25 recall-position bonus",
                    .kind = ranking::AdjustmentKind::Add,
                    .value = scored - relevance.score,
                    .before = relevance.score,
                    .after = scored,
                });
            }
            candidate.evidence_trace.push_back(ranking::semantic_evidence(
                "index[" + indexed_sources + "]", "indexed catalog semantic match", relevance,
                std::move(source_adjustments), candidate.score
            ));
        }
        result.push_back(std::move(candidate));
        ++position;
    }

    // FTS cannot retrieve a misspelled command token that does not exist in the
    // index vocabulary (for example `grpe`). For a single command-like token, a
    // small O(N) pass over command names is cheap and gives the persistent-index
    // path the same half-remembered-name behavior as PathSource.
    const auto meaningful = query::meaningful_terms(query);
    if (meaningful.size() == 1) {
        auto fuzzy_statement = prepare(
            db.get(),
            "SELECT command, summary, path, source, installed, repository_available, "
            "cli_capable, gui_capable FROM tools_fts LIMIT 12000"
        );

        if (fuzzy_statement) {
            const std::string& term = meaningful.front();
            while (sqlite3_step(fuzzy_statement.get()) == SQLITE_ROW) {
                const std::string command = text_column(fuzzy_statement.get(), 0);
                const double similarity = query::fuzzy::similarity(command, term);
                if (similarity < 0.72) {
                    continue;
                }

                const bool already_present = std::ranges::any_of(
                    result, [&](const Candidate& candidate) { return candidate.command == command; }
                );
                if (already_present) {
                    continue;
                }

                const std::string fuzzy_sources = text_column(fuzzy_statement.get(), 3);
                const double fuzzy_score = std::min(
                    0.80,
                    0.32 + (0.58 * similarity) +
                    (fuzzy_sources.find("man") != std::string::npos ? 0.025 : 0.0)
                );
                Candidate candidate{
                    .command = command,
                    .path = text_column(fuzzy_statement.get(), 2),
                    .summary = text_column(fuzzy_statement.get(), 1),
                    .source = fuzzy_sources,
                    .package = {},
                    .repository = {},
                    .package_version = {},
                    .installed = sqlite3_column_int(fuzzy_statement.get(), 4) != 0,
                    .repository_available = sqlite3_column_int(fuzzy_statement.get(), 5) != 0,
                    .cli_capable = sqlite3_column_int(fuzzy_statement.get(), 6) != 0,
                    .gui_capable = sqlite3_column_int(fuzzy_statement.get(), 7) != 0,
                    .matched_terms = {term},
                    .provided_commands = {},
                    .descriptive_evidence = {},
                .examples = {},
                .learning_resources = {},
                    .evidence_trace = {},
                    .base_merge_trace = {},
                    .score = fuzzy_score,
                    .ranking = std::nullopt,
                };
                candidate.semantic_fit = fuzzy_score;
                if (query.explain_ranking) {
                    candidate.evidence_trace.push_back(ranking::opaque_evidence(
                        "index[" + fuzzy_sources + "]", "fuzzy command-name fallback", fuzzy_score
                    ));
                }
                result.push_back(std::move(candidate));
            }
        }
    }

    std::ranges::sort(result, [](const Candidate& left, const Candidate& right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        return left.command < right.command;
    });

    return result;
}

} // namespace acclorite
