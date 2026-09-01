#include "acclorite/output/terminal_renderer.hpp"

#include <iomanip>
#include <ostream>

namespace acclorite {

void TerminalRenderer::render(const SearchResult& result, std::ostream& out) const {
    if (result.candidates.empty()) {
        out << "No matches yet for: " << result.raw_query << '\n';
        out << "(Current milestone only searches executable names in PATH.)\n";
        return;
    }

    const auto& best = result.candidates.front();

    out << "Best Match\n";
    out << "────────────────────────────\n\n";
    out << best.command << '\n';
    out << best.summary << "\n\n";
    out << "Installed\n";
    out << (best.installed ? "✓ yes" : "✗ no") << '\n';

    if (!best.path.empty()) {
        out << "\nPath\n" << best.path << '\n';
    }

    out << "\nScore\n" << std::fixed << std::setprecision(2) << best.score << '\n';

    if (result.candidates.size() > 1) {
        out << "\nAlternatives\n";
        for (std::size_t i = 1; i < result.candidates.size(); ++i) {
            out << "  " << i << ". " << result.candidates[i].command
                << "  (" << std::fixed << std::setprecision(2)
                << result.candidates[i].score << ")\n";
        }
    }
}

} // namespace acclorite
