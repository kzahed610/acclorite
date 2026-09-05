#include "acclorite/sources/pkgfile_enricher.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <ranges>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>

#include "acclorite/query/relevance.hpp"
#include "acclorite/system/executable.hpp"
#include "acclorite/system/process.hpp"

namespace acclorite {
namespace {


std::string trim(std::string_view input) {
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = input.find_last_not_of(" \t\r\n");
    return std::string(input.substr(first, last - first + 1));
}

} // namespace

bool PkgfileEnricher::available() const {
    return system::find_executable("pkgfile").has_value();
}

std::vector<std::string> PkgfileEnricher::parse_binaries(const std::string_view output) {
    std::vector<std::string> commands;
    std::unordered_set<std::string> seen;
    std::istringstream stream{std::string(output)};
    std::string line;

    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        const auto separator = line.find_first_of(" \t");
        if (separator == std::string::npos) {
            continue;
        }

        std::string path = trim(std::string_view(line).substr(separator + 1));
        if (path.empty() || path.back() == '/') {
            continue;
        }

        const std::filesystem::path file_path(path);
        const std::string command = file_path.filename().string();
        if (command.empty() || command == "." || command == "..") {
            continue;
        }
        if (seen.insert(command).second) {
            commands.push_back(command);
        }
    }

    std::ranges::sort(commands);
    return commands;
}

std::string PkgfileEnricher::preferred_command(
    const Query& query,
    const Candidate& candidate,
    const std::vector<std::string>& commands
) {
    if (commands.empty()) {
        return {};
    }

    const auto exact_existing = std::ranges::find(commands, candidate.command);
    if (exact_existing != commands.end()) {
        return *exact_existing;
    }

    if (!candidate.package.empty()) {
        const auto exact_package = std::ranges::find(commands, candidate.package);
        if (exact_package != commands.end()) {
            return *exact_package;
        }
    }

    if (commands.size() == 1) {
        return commands.front();
    }

    std::string best;
    double best_score = -1.0;
    for (const auto& command : commands) {
        const auto relevance = query::score_text(query, command, candidate.summary, 1.0, 0.88);
        double score = relevance.score;

        // Prefer a concise launcher when semantic evidence is otherwise tied.
        // This is only a tie-breaker; query relevance remains authoritative.
        score += 0.0005 / static_cast<double>(std::max<std::size_t>(1, command.size()));

        if (score > best_score) {
            best_score = score;
            best = command;
        }
    }

    return best;
}

std::size_t PkgfileEnricher::worker_count(const std::size_t candidate_count) {
    if (candidate_count == 0) {
        return 0;
    }

    std::size_t requested = 4;
    if (const char* raw = std::getenv("ACCLORITE_ENRICH_WORKERS")) {
        std::size_t parsed = 0;
        const std::string_view text(raw);
        const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (ec == std::errc{} && ptr == text.data() + text.size() && parsed > 0) {
            requested = parsed;
        }
    }
    return std::min<std::size_t>({requested, candidate_count, 8});
}

void PkgfileEnricher::enrich_candidate(const Query& query, Candidate& candidate) const {
    if (candidate.package.empty() || !candidate.repository_available) {
        return;
    }

    std::string target = candidate.package;
    if (!candidate.repository.empty()) {
        target = candidate.repository + "/" + candidate.package;
    }

    auto process = system::run_capture_stdout(
        {"pkgfile", "--list", "--binaries", target},
        512 * 1024
    );
    auto commands = parse_binaries(process.stdout_text);

    // Third-party Arch-family repos may not expose a .files database under the
    // exact repo name returned by pacman/expac. If the qualified lookup yields
    // nothing, retry by package name across pkgfile's configured repositories.
    if (commands.empty() && !candidate.repository.empty()) {
        process = system::run_capture_stdout(
            {"pkgfile", "--list", "--binaries", candidate.package},
            512 * 1024
        );
        commands = parse_binaries(process.stdout_text);
    }

    if (commands.empty()) {
        return;
    }

    candidate.provided_commands = commands;
    candidate.cli_capable = true;
    append_source(candidate.source, "pkgfile");

    const std::string preferred = preferred_command(query, candidate, commands);
    if (!preferred.empty()) {
        candidate.command = preferred;
        if (const auto executable = system::find_executable(preferred)) {
            candidate.path = executable->string();
            candidate.installed = true;
        }
    }
}

void PkgfileEnricher::enrich(const Query& query, Candidate& candidate) const {
    if (!available()) {
        return;
    }
    enrich_candidate(query, candidate);
}

void PkgfileEnricher::enrich_many(
    const Query& query,
    std::vector<Candidate>& candidates,
    const std::size_t count
) const {
    const std::size_t bounded = std::min(count, candidates.size());
    if (bounded == 0 || !available()) {
        return;
    }

    const std::size_t workers = worker_count(bounded);
    if (workers <= 1) {
        for (std::size_t i = 0; i < bounded; ++i) {
            enrich_candidate(query, candidates[i]);
        }
        return;
    }

    std::atomic_size_t next{0};
    std::vector<std::jthread> pool;
    pool.reserve(workers);
    for (std::size_t worker = 0; worker < workers; ++worker) {
        pool.emplace_back([&]() {
            while (true) {
                const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
                if (index >= bounded) {
                    break;
                }
                enrich_candidate(query, candidates[index]);
            }
        });
    }
}

} // namespace acclorite
