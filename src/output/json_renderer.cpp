#include "acclorite/output/json_renderer.hpp"

#include <iomanip>
#include <ostream>
#include <sstream>

namespace acclorite {

std::string JsonRenderer::escape(const std::string_view input) {
    std::ostringstream out;

    for (const unsigned char ch : input) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    out << "\\u"
                        << std::hex << std::uppercase << std::setw(4)
                        << std::setfill('0') << static_cast<int>(ch)
                        << std::dec << std::nouppercase << std::setfill(' ');
                } else {
                    out << static_cast<char>(ch);
                }
        }
    }

    return out.str();
}

void JsonRenderer::render(const SearchResult& result, std::ostream& out) const {
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"query\": \"" << escape(result.raw_query) << "\",\n";
    out << "  \"normalized_query\": \"" << escape(result.normalized_query) << "\",\n";
    out << "  \"results\": [";

    if (!result.candidates.empty()) {
        out << '\n';
    }

    for (std::size_t i = 0; i < result.candidates.size(); ++i) {
        const auto& candidate = result.candidates[i];
        out << "    {\n";
        out << "      \"command\": \"" << escape(candidate.command) << "\",\n";
        out << "      \"path\": \"" << escape(candidate.path) << "\",\n";
        out << "      \"summary\": \"" << escape(candidate.summary) << "\",\n";
        out << "      \"source\": \"" << escape(candidate.source) << "\",\n";
        out << "      \"installed\": " << (candidate.installed ? "true" : "false") << ",\n";
        out << "      \"repository_available\": "
            << (candidate.repository_available ? "true" : "false") << ",\n";
        out << "      \"score\": " << std::fixed << std::setprecision(4) << candidate.score << '\n';
        out << "    }";
        if (i + 1 < result.candidates.size()) {
            out << ',';
        }
        out << '\n';
    }

    out << "  ]\n";
    out << "}\n";
}

} // namespace acclorite
