#include "acclorite/output/terminal_renderer.hpp"

#include <iomanip>
#include <ostream>

namespace acclorite {

void TerminalRenderer::render(const SearchResult& result, std::ostream& out) const {
    if (result.candidates.empty()) {
        out << "No matches yet for: " << result.raw_query << '\n';
        out << "(No installed command matched strongly enough in the available local sources.)\n";
        return;
    }

    const auto& best = result.candidates.front();

    out << "Best Match\n";
    out << "────────────────────────────\n\n";
    out << best.command << '\n';
    out << best.summary << "\n\n";
    out << "Installed\n";
    out << (best.installed ? "✓ yes" : "✗ no") << '\n';

    out << "\nInterface\n" << interface_kind_name(best.interface_kind()) << '\n';

    if (!best.path.empty()) {
        out << "\nPath\n" << best.path << '\n';
    }

    if (!best.matched_terms.empty()) {
        out << "\nMatched concepts\n";
        for (std::size_t i = 0; i < best.matched_terms.size(); ++i) {
            if (i != 0) {
                out << " · ";
            }
            out << best.matched_terms[i];
        }
        out << '\n';
    }

    if (!best.source.empty()) {
        out << "\nSources\n" << best.source << '\n';
    }

    out << "\nScore\n" << std::fixed << std::setprecision(2) << best.score << '\n';

    if (result.candidates.size() > 1) {
        out << "\nAlternatives\n";
        for (std::size_t i = 1; i < result.candidates.size(); ++i) {
            out << "  " << i << ". " << result.candidates[i].command
                << "  [" << interface_kind_name(result.candidates[i].interface_kind()) << "]"
                << "  (" << std::fixed << std::setprecision(2)
                << result.candidates[i].score << ")\n";
        }
    }
}

} // namespace acclorite
