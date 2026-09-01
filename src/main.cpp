#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "acclorite/core/query.hpp"
#include "acclorite/core/search_engine.hpp"
#include "acclorite/output/json_renderer.hpp"
#include "acclorite/output/terminal_renderer.hpp"
#include "acclorite/sources/path_source.hpp"

namespace {

constexpr std::string_view kVersion = "0.0.1-dev";

void print_usage(std::ostream& out) {
    out << "Acclorite — Linux tool discovery assistant\n\n";
    out << "Usage:\n";
    out << "  acclorite [--json] <query...>\n";
    out << "  acclorite --version\n";
    out << "  acclorite --help\n";
}

std::string join_query(const std::vector<std::string>& parts) {
    std::string query;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            query.push_back(' ');
        }
        query += parts[i];
    }
    return query;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(std::cerr);
        return 2;
    }

    bool json = false;
    std::vector<std::string> query_parts;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);

        if (arg == "--help" || arg == "-h") {
            print_usage(std::cout);
            return 0;
        }

        if (arg == "--version" || arg == "-V") {
            std::cout << "acclorite " << kVersion << '\n';
            return 0;
        }

        if (arg == "--json") {
            json = true;
            continue;
        }

        query_parts.emplace_back(arg);
    }

    if (query_parts.empty()) {
        std::cerr << "error: missing query\n\n";
        print_usage(std::cerr);
        return 2;
    }

    const acclorite::Query query = acclorite::Query::parse(join_query(query_parts));

    acclorite::SearchEngine engine;
    engine.add_source(std::make_unique<acclorite::PathSource>());

    const acclorite::SearchResult result = engine.search(query);

    if (json) {
        acclorite::JsonRenderer{}.render(result, std::cout);
    } else {
        acclorite::TerminalRenderer{}.render(result, std::cout);
    }

    return result.candidates.empty() ? 1 : 0;
}
