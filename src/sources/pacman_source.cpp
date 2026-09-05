#include "acclorite/sources/pacman_source.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <ranges>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

#include <unistd.h>

#if ACCLORITE_HAS_SQLITE
#include <sqlite3.h>
#endif

#include "acclorite/query/lexicon.hpp"
#include "acclorite/query/relevance.hpp"
#include "acclorite/ranking/evidence.hpp"
#include "acclorite/system/executable.hpp"
#include "acclorite/system/process.hpp"

namespace acclorite {
namespace {

constexpr std::string_view kArchCacheMagic = "acclorite-arch-cache-v3";
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

std::string trim(std::string_view input) {
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = input.find_last_not_of(" \t\r\n");
    return std::string(input.substr(first, last - first + 1));
}

std::string lower_ascii(std::string_view input) {
    std::string result;
    result.reserve(input.size());
    for (const unsigned char ch : input) {
        result.push_back(static_cast<char>(std::tolower(ch)));
    }
    return result;
}

void hash_text(std::uint64_t& hash, std::string_view text) {
    for (const unsigned char ch : text) {
        hash ^= ch;
        hash *= kFnvPrime;
    }
    hash ^= 0xff;
    hash *= kFnvPrime;
}

std::string hash_hex(std::uint64_t value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = digits[value & 0x0f];
        value >>= 4;
    }
    return out;
}

std::filesystem::path configured_pacman_db_path() {
    if (const char* db_path = std::getenv("ACCLORITE_PACMAN_DBPATH")) {
        return db_path;
    }

    const std::filesystem::path config = std::getenv("ACCLORITE_PACMAN_CONFIG")
        ? std::filesystem::path(std::getenv("ACCLORITE_PACMAN_CONFIG"))
        : std::filesystem::path("/etc/pacman.conf");
    std::ifstream file(config);
    std::string line;
    while (std::getline(file, line)) {
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line.resize(comment);
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = trim(std::string_view(line).substr(0, equals));
        if (key != "DBPath") {
            continue;
        }
        const std::string value = trim(std::string_view(line).substr(equals + 1));
        if (!value.empty()) {
            return value;
        }
    }
    return "/var/lib/pacman";
}

std::filesystem::path sync_database_directory() {
    if (const char* override_path = std::getenv("ACCLORITE_PACMAN_SYNC_DIR")) {
        return override_path;
    }
    return configured_pacman_db_path() / "sync";
}

bool cache_disabled() {
    const char* raw = std::getenv("ACCLORITE_DISABLE_ARCH_CACHE");
    return raw && std::string_view(raw) != "" && std::string_view(raw) != "0";
}

std::string sanitize_cache_field(std::string_view input) {
    std::string result;
    result.reserve(input.size());
    for (const char ch : input) {
        if (ch == '\t' || ch == '\r' || ch == '\n') {
            result.push_back(' ');
        } else {
            result.push_back(ch);
        }
    }
    return result;
}

std::vector<query::ConceptGroup> selected_search_groups(const Query& query) {
    auto groups = query::concept_groups(query);
    if (groups.empty()) {
        return {};
    }

    std::ranges::sort(groups, [](const auto& left, const auto& right) {
        if (left.implicit != right.implicit) {
            return !left.implicit;
        }
        return left.weight > right.weight;
    });

    std::vector<query::ConceptGroup> selected;
    for (const auto& group : groups) {
        if (group.implicit && !selected.empty()) {
            continue;
        }
        selected.push_back(group);
        if (selected.size() == 2) {
            break;
        }
    }

    if (selected.size() == 1 && !selected.front().implicit) {
        for (const auto& group : groups) {
            if (group.implicit) {
                selected.push_back(group);
                break;
            }
        }
    }
    return selected;
}

bool cached_group_matches(const std::string& searchable, const query::ConceptGroup& group) {
    std::unordered_set<std::string> seen;
    std::size_t considered = 0;
    for (const auto& alternative : group.alternatives) {
        if (alternative.size() < 2 || !seen.insert(alternative).second) {
            continue;
        }
        ++considered;
        if (searchable.find(lower_ascii(alternative)) != std::string::npos) {
            return true;
        }
        if (considered == 4) {
            break;
        }
    }
    return false;
}

std::string ere_escape(std::string_view input) {
    static constexpr std::string_view special = R"(.^$[](){}*+?|\)";
    std::string escaped;
    escaped.reserve(input.size() * 2);
    for (const char ch : input) {
        if (special.find(ch) != std::string_view::npos) {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::string group_pattern(const query::ConceptGroup& group) {
    std::vector<std::string> alternatives;
    alternatives.reserve(4);
    std::unordered_set<std::string> seen;

    for (const auto& term : group.alternatives) {
        if (term.size() < 2 || !seen.insert(term).second) {
            continue;
        }
        alternatives.push_back(ere_escape(term));
        if (alternatives.size() == 4) {
            break;
        }
    }

    if (alternatives.empty()) {
        return {};
    }
    if (alternatives.size() == 1) {
        return alternatives.front();
    }

    std::string pattern = "(";
    for (std::size_t i = 0; i < alternatives.size(); ++i) {
        if (i != 0) {
            pattern.push_back('|');
        }
        pattern += alternatives[i];
    }
    pattern.push_back(')');
    return pattern;
}

#if ACCLORITE_HAS_SQLITE

struct ArchSqliteCloser {
    void operator()(sqlite3* db) const {
        if (db) {
            sqlite3_close(db);
        }
    }
};
using ArchDatabase = std::unique_ptr<sqlite3, ArchSqliteCloser>;

struct ArchStatementCloser {
    void operator()(sqlite3_stmt* statement) const {
        if (statement) {
            sqlite3_finalize(statement);
        }
    }
};
using ArchStatement = std::unique_ptr<sqlite3_stmt, ArchStatementCloser>;

ArchDatabase open_arch_database(
    const std::filesystem::path& path,
    const int flags
) {
    sqlite3* raw = nullptr;
    const int rc = sqlite3_open_v2(path.c_str(), &raw, flags, nullptr);
    ArchDatabase db(raw);
    if (rc != SQLITE_OK || !db) {
        return {};
    }
    sqlite3_busy_timeout(db.get(), 1000);
    return db;
}

bool arch_exec(sqlite3* db, const char* sql) {
    char* error = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
    if (error) {
        sqlite3_free(error);
    }
    return rc == SQLITE_OK;
}

ArchStatement arch_prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK) {
        return {};
    }
    return ArchStatement(raw);
}

std::string arch_text_column(sqlite3_stmt* statement, const int column) {
    const auto* raw = sqlite3_column_text(statement, column);
    return raw ? reinterpret_cast<const char*>(raw) : std::string{};
}

std::vector<std::string> fts_words(std::string_view input) {
    std::vector<std::string> words;
    std::string current;
    for (const unsigned char ch : input) {
        if (std::isalnum(ch)) {
            current.push_back(static_cast<char>(std::tolower(ch)));
        } else if (!current.empty()) {
            words.push_back(std::move(current));
            current.clear();
        }
    }
    if (!current.empty()) {
        words.push_back(std::move(current));
    }
    return words;
}

std::string fts_alternative(std::string_view input) {
    const auto words = fts_words(input);
    if (words.empty()) {
        return {};
    }

    if (words.size() == 1) {
        // Prefix search approximates expac/pacman substring-style recall while
        // semantic scoring remains the authority afterward.
        return "\"" + words.front() + "\"*";
    }

    std::string phrase = "\"";
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (i != 0) {
            phrase.push_back(' ');
        }
        phrase += words[i];
    }
    phrase.push_back('"');
    return phrase;
}

std::string arch_fts_group_clause(const query::ConceptGroup& group) {
    std::vector<std::string> alternatives;
    std::unordered_set<std::string> seen;
    for (const auto& alternative : group.alternatives) {
        const std::string term = fts_alternative(alternative);
        if (term.empty() || !seen.insert(term).second) {
            continue;
        }
        alternatives.push_back(term);
        if (alternatives.size() == 4) {
            break;
        }
    }

    if (alternatives.empty()) {
        return {};
    }
    if (alternatives.size() == 1) {
        return alternatives.front();
    }

    std::string clause = "(";
    for (std::size_t i = 0; i < alternatives.size(); ++i) {
        if (i != 0) {
            clause += " OR ";
        }
        clause += alternatives[i];
    }
    clause.push_back(')');
    return clause;
}

std::string arch_fts_query(const Query& query, const std::size_t group_count) {
    const auto groups = selected_search_groups(query);
    if (groups.empty()) {
        return {};
    }

    const std::size_t count = std::min(group_count, groups.size());
    std::string expression;
    for (std::size_t i = 0; i < count; ++i) {
        const std::string clause = arch_fts_group_clause(groups[i]);
        if (clause.empty()) {
            continue;
        }
        if (!expression.empty()) {
            expression += " AND ";
        }
        expression += clause;
    }
    return expression;
}

#endif


} // namespace

bool PacmanSource::available() const {
    return system::find_executable("expac").has_value() ||
           system::find_executable("pacman").has_value();
}

std::vector<std::string> PacmanSource::search_patterns(const Query& query) {
    const auto groups = selected_search_groups(query);
    std::vector<std::string> patterns;
    patterns.reserve(groups.size());
    for (const auto& group : groups) {
        const std::string pattern = group_pattern(group);
        if (!pattern.empty()) {
            patterns.push_back(pattern);
        }
    }
    return patterns;
}

std::vector<PacmanSource::PackageEntry> PacmanSource::parse_search(const std::string_view output) {
    std::vector<PackageEntry> entries;
    std::istringstream stream{std::string(output)};
    std::string line;
    PackageEntry current;
    bool have_header = false;

    auto flush = [&]() {
        if (!have_header || current.package.empty()) {
            return;
        }
        current.description = trim(current.description);
        entries.push_back(std::move(current));
        current = PackageEntry{};
        have_header = false;
    };
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }

        if (!std::isspace(static_cast<unsigned char>(line.front()))) {
            flush();

            std::istringstream header(line);
            std::string repo_package;
            if (!(header >> repo_package >> current.version)) {
                current = PackageEntry{};
                continue;
            }

            const auto slash = repo_package.find('/');
            if (slash == std::string::npos || slash == 0 || slash + 1 >= repo_package.size()) {
                current = PackageEntry{};
                continue;
            }

            current.repository = repo_package.substr(0, slash);
            current.package = repo_package.substr(slash + 1);
            current.installed = line.find("[installed") != std::string::npos;
            have_header = true;
            continue;
        }

        if (!have_header) {
            continue;
        }

        const std::string description_line = trim(line);
        if (!description_line.empty()) {
            if (!current.description.empty()) {
                current.description.push_back(' ');
            }
            current.description += description_line;
        }
    }

    flush();
    return entries;
}

std::vector<PacmanSource::PackageEntry> PacmanSource::parse_expac(const std::string_view output) {
    std::vector<PackageEntry> entries;
    std::istringstream stream{std::string(output)};
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }

        std::array<std::string, 4> fields;
        std::size_t field = 0;
        std::size_t start = 0;
        while (field < 3) {
            const auto tab = line.find('\t', start);
            if (tab == std::string::npos) {
                break;
            }
            fields[field++] = line.substr(start, tab - start);
            start = tab + 1;
        }
        if (field != 3) {
            continue;
        }
        fields[3] = line.substr(start);

        PackageEntry entry{
            .repository = trim(fields[0]),
            .package = trim(fields[1]),
            .version = trim(fields[2]),
            .description = trim(fields[3]),
            .installed = false,
            .source = "expac",
        };
        if (!entry.package.empty()) {
            entries.push_back(std::move(entry));
        }
    }

    return entries;
}

double PacmanSource::score_entry(const Query& query, const PackageEntry& entry) {
    std::string searchable = entry.description;
    if (!entry.repository.empty()) {
        searchable += " ";
        searchable += entry.repository;
    }

    auto relevance = query::score_text(query, entry.package, searchable, 0.98, 0.94);
    if (relevance.score <= 0.0) {
        return 0.0;
    }
    return std::min(0.96, relevance.score + 0.02);
}

std::filesystem::path PacmanSource::cache_path() {
    if (const char* override_path = std::getenv("ACCLORITE_ARCH_CACHE_PATH")) {
        return override_path;
    }
    if (const char* cache_home = std::getenv("XDG_CACHE_HOME")) {
#if ACCLORITE_HAS_SQLITE
        return std::filesystem::path(cache_home) / "acclorite/arch-packages.db";
#else
        return std::filesystem::path(cache_home) / "acclorite/arch-packages.tsv";
#endif
    }
    if (const char* home = std::getenv("HOME")) {
#if ACCLORITE_HAS_SQLITE
        return std::filesystem::path(home) / ".cache/acclorite/arch-packages.db";
#else
        return std::filesystem::path(home) / ".cache/acclorite/arch-packages.tsv";
#endif
    }
#if ACCLORITE_HAS_SQLITE
    return std::filesystem::temp_directory_path() / "acclorite-arch-packages.db";
#else
    return std::filesystem::temp_directory_path() / "acclorite-arch-packages.tsv";
#endif
}

std::string PacmanSource::arch_catalog_fingerprint() {
    const auto directory = sync_database_directory();
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || error) {
        return {};
    }

    std::vector<std::filesystem::path> databases;
    for (std::filesystem::directory_iterator it(
             directory,
             std::filesystem::directory_options::skip_permission_denied,
             error
         ); !error && it != std::filesystem::directory_iterator{}; it.increment(error)) {
        const auto filename = it->path().filename().string();
        if (filename.ends_with(".db") || filename.find(".db.") != std::string::npos) {
            databases.push_back(it->path());
        }
    }
    if (error || databases.empty()) {
        return {};
    }

    std::ranges::sort(databases);
    std::uint64_t hash = kFnvOffset;
    hash_text(hash, "arch-catalog-v2");
    hash_text(hash, directory.string());

    // A synchronized-package cache is only valid for the executable that built it.
    // This matters for tests, wrappers, alternate expac installations, and upgrades:
    // a temporary PATH shadow must never be able to stamp fixture data with the
    // fingerprint of the host's real pacman databases and have production trust it.
    const auto expac = system::find_executable("expac");
    if (!expac) {
        return {};
    }
    hash_text(hash, "expac");
    hash_text(hash, expac->string());
    error.clear();
    const auto expac_modified = std::filesystem::last_write_time(*expac, error);
    if (!error) {
        hash_text(hash, std::to_string(expac_modified.time_since_epoch().count()));
    }
    error.clear();
    const auto expac_size = std::filesystem::file_size(*expac, error);
    if (!error) {
        hash_text(hash, std::to_string(expac_size));
    }

    for (const auto& database : databases) {
        hash_text(hash, database.filename().string());
        error.clear();
        const auto modified = std::filesystem::last_write_time(database, error);
        if (!error) {
            hash_text(hash, std::to_string(modified.time_since_epoch().count()));
        }
        error.clear();
        const auto size = std::filesystem::file_size(database, error);
        if (!error) {
            hash_text(hash, std::to_string(size));
        }
    }
    return hash_hex(hash);
}

std::optional<std::vector<PacmanSource::PackageEntry>> PacmanSource::load_cache(
    const std::string_view fingerprint
) {
    if (fingerprint.empty()) {
        return std::nullopt;
    }

    std::ifstream file(cache_path());
    if (!file) {
        return std::nullopt;
    }

    std::string header;
    if (!std::getline(file, header)) {
        return std::nullopt;
    }
    const std::string expected = std::string(kArchCacheMagic) + "\t" + std::string(fingerprint);
    if (header != expected) {
        return std::nullopt;
    }

    std::vector<PackageEntry> entries;
    std::string line;
    while (std::getline(file, line)) {
        std::array<std::string, 5> fields;
        std::size_t field = 0;
        std::size_t start = 0;
        while (field < 4) {
            const auto tab = line.find('\t', start);
            if (tab == std::string::npos) {
                break;
            }
            fields[field++] = line.substr(start, tab - start);
            start = tab + 1;
        }
        if (field != 4) {
            continue;
        }
        fields[4] = line.substr(start);
        if (fields[2].empty()) {
            continue;
        }
        entries.push_back(PackageEntry{
            .repository = std::move(fields[1]),
            .package = std::move(fields[2]),
            .version = std::move(fields[3]),
            .description = std::move(fields[4]),
            .installed = false,
            .source = std::move(fields[0]),
        });
    }
    return entries;
}

std::optional<std::vector<PacmanSource::PackageEntry>> PacmanSource::rebuild_cache(
    const std::string_view fingerprint
) {
    if (fingerprint.empty() || !system::find_executable("expac")) {
        return std::nullopt;
    }

    // -S without -s enumerates synchronized packages. expac reads pacman.conf itself,
    // so the persisted snapshot follows the same configured ALPM repositories as the
    // live backend without Acclorite refreshing or mutating package metadata.
    const auto process = system::run_capture_stdout(
        {"expac", "-S", "%r\t%n\t%v\t%d"},
        64 * 1024 * 1024
    );
    if (process.exit_code != 0 || process.stdout_text.empty()) {
        return std::nullopt;
    }

    auto entries = parse_expac(process.stdout_text);
    if (entries.empty()) {
        return std::nullopt;
    }

    const auto path = cache_path();
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return entries; // cache acceleration is optional; discovery still works.
        }
    }

    const auto temporary = path.string() + ".tmp." + std::to_string(::getpid());
    {
        std::ofstream file(temporary, std::ios::trunc);
        if (!file) {
            return entries;
        }
        file << kArchCacheMagic << '\t' << fingerprint << '\n';
        for (const auto& entry : entries) {
            file << sanitize_cache_field(entry.source) << '\t'
                 << sanitize_cache_field(entry.repository) << '\t'
                 << sanitize_cache_field(entry.package) << '\t'
                 << sanitize_cache_field(entry.version) << '\t'
                 << sanitize_cache_field(entry.description) << '\n';
        }
        if (!file) {
            std::filesystem::remove(temporary, error);
            return entries;
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
        if (error) {
            std::filesystem::remove(temporary, error);
        }
    }
    return entries;
}


std::optional<std::vector<PacmanSource::PackageEntry>> PacmanSource::load_fast_cache(
    const Query& query,
    const std::string_view fingerprint
) {
#if ACCLORITE_HAS_SQLITE
    if (fingerprint.empty()) {
        return std::nullopt;
    }

    auto db = open_arch_database(cache_path(), SQLITE_OPEN_READONLY);
    if (!db) {
        return std::nullopt;
    }

    {
        auto metadata = arch_prepare(
            db.get(),
            "SELECT value FROM metadata WHERE key='fingerprint' LIMIT 1"
        );
        if (!metadata || sqlite3_step(metadata.get()) != SQLITE_ROW ||
            arch_text_column(metadata.get(), 0) != fingerprint) {
            return std::nullopt;
        }
    }
    {
        auto schema = arch_prepare(
            db.get(),
            "SELECT value FROM metadata WHERE key='schema_version' LIMIT 1"
        );
        if (!schema || sqlite3_step(schema.get()) != SQLITE_ROW ||
            arch_text_column(schema.get(), 0) != "1") {
            return std::nullopt;
        }
    }

    const auto groups = selected_search_groups(query);
    if (groups.empty()) {
        return std::vector<PackageEntry>{};
    }

    const auto run_fts = [&](const std::size_t group_count) {
        std::vector<PackageEntry> matches;
        const std::string expression = arch_fts_query(query, group_count);
        if (expression.empty()) {
            return matches;
        }

        auto statement = arch_prepare(
            db.get(),
            "SELECT source, repository, package, version, description "
            "FROM packages_fts WHERE packages_fts MATCH ? "
            "ORDER BY bm25(packages_fts, 0.0, 0.0, 5.0, 0.0, 2.0) LIMIT 800"
        );
        if (!statement) {
            return matches;
        }

        sqlite3_bind_text(
            statement.get(), 1, expression.c_str(), -1, SQLITE_TRANSIENT
        );
        while (sqlite3_step(statement.get()) == SQLITE_ROW) {
            matches.push_back(PackageEntry{
                .repository = arch_text_column(statement.get(), 1),
                .package = arch_text_column(statement.get(), 2),
                .version = arch_text_column(statement.get(), 3),
                .description = arch_text_column(statement.get(), 4),
                .installed = false,
                .source = arch_text_column(statement.get(), 0),
            });
        }
        return matches;
    };

    auto matches = run_fts(groups.size());
    if (matches.empty() && groups.size() > 1) {
        matches = run_fts(1);
    }
    return matches;
#else
    auto catalog = load_cache(fingerprint);
    if (!catalog) {
        return std::nullopt;
    }
    return filter_cached_entries(query, *catalog);
#endif
}

std::optional<std::vector<PacmanSource::PackageEntry>> PacmanSource::rebuild_fast_cache(
    const Query& query,
    const std::string_view fingerprint
) {
#if ACCLORITE_HAS_SQLITE
    if (fingerprint.empty() || !system::find_executable("expac")) {
        return std::nullopt;
    }

    const auto process = system::run_capture_stdout(
        {"expac", "-S", "%r\t%n\t%v\t%d"},
        64 * 1024 * 1024
    );
    if (process.exit_code != 0 || process.stdout_text.empty()) {
        return std::nullopt;
    }

    auto entries = parse_expac(process.stdout_text);
    if (entries.empty()) {
        return std::nullopt;
    }

    const auto filtered = filter_cached_entries(query, entries);
    const auto path = cache_path();
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return filtered;
        }
    }

    const auto temporary = std::filesystem::path(
        path.string() + ".tmp." + std::to_string(::getpid())
    );
    std::filesystem::remove(temporary, error);
    error.clear();

    {
        auto db = open_arch_database(
            temporary,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
        );
        if (!db) {
            return filtered;
        }

        if (!arch_exec(db.get(), "PRAGMA journal_mode=OFF;") ||
            !arch_exec(db.get(), "PRAGMA synchronous=OFF;") ||
            !arch_exec(
                db.get(),
                "CREATE TABLE metadata(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
            ) ||
            !arch_exec(
                db.get(),
                "CREATE VIRTUAL TABLE packages_fts USING fts5("
                "source UNINDEXED, repository UNINDEXED, package, version UNINDEXED, "
                "description, tokenize='unicode61');"
            ) ||
            !arch_exec(db.get(), "BEGIN;")) {
            std::filesystem::remove(temporary, error);
            return filtered;
        }

        auto metadata = arch_prepare(
            db.get(), "INSERT INTO metadata(key, value) VALUES(?, ?)"
        );
        auto package = arch_prepare(
            db.get(),
            "INSERT INTO packages_fts(source, repository, package, version, description) "
            "VALUES(?, ?, ?, ?, ?)"
        );
        if (!metadata || !package) {
            arch_exec(db.get(), "ROLLBACK;");
            std::filesystem::remove(temporary, error);
            return filtered;
        }

        const auto write_meta = [&](const char* key, const std::string_view value) {
            sqlite3_reset(metadata.get());
            sqlite3_clear_bindings(metadata.get());
            sqlite3_bind_text(metadata.get(), 1, key, -1, SQLITE_STATIC);
            sqlite3_bind_text(
                metadata.get(), 2, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT
            );
            return sqlite3_step(metadata.get()) == SQLITE_DONE;
        };

        if (!write_meta("schema_version", "1") ||
            !write_meta("fingerprint", fingerprint)) {
            arch_exec(db.get(), "ROLLBACK;");
            std::filesystem::remove(temporary, error);
            return filtered;
        }

        for (const auto& entry : entries) {
            sqlite3_reset(package.get());
            sqlite3_clear_bindings(package.get());
            sqlite3_bind_text(package.get(), 1, entry.source.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(package.get(), 2, entry.repository.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(package.get(), 3, entry.package.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(package.get(), 4, entry.version.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(package.get(), 5, entry.description.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(package.get()) != SQLITE_DONE) {
                arch_exec(db.get(), "ROLLBACK;");
                std::filesystem::remove(temporary, error);
                return filtered;
            }
        }

        if (!arch_exec(db.get(), "COMMIT;")) {
            std::filesystem::remove(temporary, error);
            return filtered;
        }
    }

    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return filtered;
        }
    }

    // The cold query should observe exactly the same indexed retrieval semantics
    // as every subsequent warm query. Fall back to the in-memory catalog only if
    // the just-written cache cannot be reopened for some environmental reason.
    if (auto indexed = load_fast_cache(query, fingerprint)) {
        return indexed;
    }
    return filtered;
#else
    auto catalog = rebuild_cache(fingerprint);
    if (!catalog) {
        return std::nullopt;
    }
    return filter_cached_entries(query, *catalog);
#endif
}

std::vector<PacmanSource::PackageEntry> PacmanSource::filter_cached_entries(
    const Query& query,
    const std::vector<PackageEntry>& entries
) {
    const auto groups = selected_search_groups(query);
    if (groups.empty()) {
        return {};
    }

    const auto filter = [&](const std::size_t group_count) {
        std::vector<PackageEntry> matches;
        for (const auto& entry : entries) {
            std::string searchable;
            searchable.reserve(entry.package.size() + entry.description.size() + 1);
            searchable += lower_ascii(entry.package);
            searchable.push_back(' ');
            searchable += lower_ascii(entry.description);

            bool matched = true;
            for (std::size_t i = 0; i < group_count; ++i) {
                if (!cached_group_matches(searchable, groups[i])) {
                    matched = false;
                    break;
                }
            }
            if (matched) {
                matches.push_back(entry);
            }
        }
        return matches;
    };

    auto matches = filter(groups.size());
    if (matches.empty() && groups.size() > 1) {
        matches = filter(1);
    }
    return matches;
}

std::vector<Candidate> PacmanSource::search(const Query& query) const {
    if (!available()) {
        return {};
    }

    const auto patterns = search_patterns(query);
    if (patterns.empty()) {
        return {};
    }

    const bool has_expac = system::find_executable("expac").has_value();
    const bool has_pacman = system::find_executable("pacman").has_value();

    std::vector<PackageEntry> entries;
    bool used_cache = false;
    if (has_expac && !cache_disabled()) {
        const std::string fingerprint = arch_catalog_fingerprint();
        if (!fingerprint.empty()) {
            auto cached = load_fast_cache(query, fingerprint);
            if (!cached) {
                cached = rebuild_fast_cache(query, fingerprint);
            }
            if (cached) {
                entries = std::move(*cached);
                used_cache = true;
            }
        }
    }

    const auto run_search = [&](const std::vector<std::string>& requested_patterns) {
        if (has_expac) {
            std::vector<std::string> args{
                "expac", "-Ss", "%r\t%n\t%v\t%d"
            };
            args.insert(args.end(), requested_patterns.begin(), requested_patterns.end());
            const auto process = system::run_capture_stdout(args, 4 * 1024 * 1024);
            if (process.exit_code == 0) {
                return parse_expac(process.stdout_text);
            }
        }

        if (!has_pacman) {
            return std::vector<PackageEntry>{};
        }

        std::vector<std::string> args{"pacman", "--color", "never", "-Ss"};
        args.insert(args.end(), requested_patterns.begin(), requested_patterns.end());
        const auto process = system::run_capture_stdout(args, 4 * 1024 * 1024);
        if (process.exit_code != 0 && process.stdout_text.empty()) {
            return std::vector<PackageEntry>{};
        }
        return parse_search(process.stdout_text);
    };

    if (!used_cache) {
        entries = run_search(patterns);
        if (entries.empty() && patterns.size() > 1) {
            entries = run_search({patterns.front()});
        }
    }

    std::vector<Candidate> result;
    result.reserve(entries.size());

    for (const auto& entry : entries) {
        std::string searchable = entry.description;
        if (!entry.repository.empty()) {
            searchable += " ";
            searchable += entry.repository;
        }

        auto relevance = query::score_text(query, entry.package, searchable, 0.98, 0.94);
        if (relevance.score <= 0.0) {
            continue;
        }

        std::vector<ranking::RankingAdjustment> source_adjustments;
        const double before_metadata_bonus = relevance.score;
        const double score = std::min(0.96, relevance.score + 0.02);
        if (score != before_metadata_bonus) {
            source_adjustments.push_back(ranking::RankingAdjustment{
                .id = "repository-metadata",
                .label = "repository metadata support",
                .kind = ranking::AdjustmentKind::Add,
                .value = score - before_metadata_bonus,
                .before = before_metadata_bonus,
                .after = score,
            });
        }

        const auto executable = system::find_executable(entry.package);
        Candidate candidate{
            .command = entry.package,
            .path = executable ? executable->string() : std::string{},
            .summary = entry.description.empty() ? "Package available in Arch repositories" : entry.description,
            .source = entry.source,
            .package = entry.package,
            .repository = entry.repository,
            .package_version = entry.version,
            .installed = entry.installed || executable.has_value(),
            .repository_available = true,
            .cli_capable = executable.has_value(),
            .gui_capable = false,
            .matched_terms = relevance.matched_terms,
            .provided_commands = {},
            .descriptive_evidence = {},
                .examples = {},
                .learning_resources = {},
            .evidence_trace = {},
            .base_merge_trace = {},
            .score = score,
            .ranking = std::nullopt,
        };
        candidate.semantic_fit = relevance.semantic_fit;
        if (query.explain_ranking) {
            candidate.evidence_trace.push_back(ranking::semantic_evidence(
                entry.source, "repository package semantic match", relevance,
                std::move(source_adjustments), candidate.score
            ));
        }
        result.push_back(std::move(candidate));
    }

    std::ranges::sort(result, [](const Candidate& left, const Candidate& right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        return left.command < right.command;
    });

    if (result.size() > 80) {
        result.resize(80);
    }
    return result;
}

} // namespace acclorite
