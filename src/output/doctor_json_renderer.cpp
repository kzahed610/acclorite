#include "acclorite/output/doctor_json_renderer.hpp"

#include "acclorite/core/machine_interface.hpp"

#include <iomanip>
#include <ostream>
#include <string_view>

namespace acclorite {
namespace {

void json_string(std::ostream& out, const std::string_view value) {
    out << '"';
    for (const unsigned char ch : value) {
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
                    << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch)
                    << std::dec << std::setfill(' ');
            } else {
                out << static_cast<char>(ch);
            }
        }
    }
    out << '"';
}

} // namespace

void DoctorJsonRenderer::render(
    const diagnostics::DoctorReport& report,
    std::ostream& out
) const {
    out << "{\n";
    out << "  \"doctor_schema_version\": " << machine::kDoctorSchemaVersion << ",\n";
    out << "  \"status\": ";
    json_string(out, report.status());
    out << ",\n";
    out << "  \"mutated_system\": false,\n";
    out << "  \"checks\": [\n";

    for (std::size_t i = 0; i < report.checks.size(); ++i) {
        const auto& item = report.checks[i];
        out << "    {\n";
        out << "      \"id\": "; json_string(out, item.id); out << ",\n";
        out << "      \"section\": "; json_string(out, item.section); out << ",\n";
        out << "      \"label\": "; json_string(out, item.label); out << ",\n";
        out << "      \"state\": "; json_string(out, diagnostics::doctor_state_name(item.state)); out << ",\n";
        out << "      \"detail\": "; json_string(out, item.detail); out << ",\n";
        out << "      \"hint\": "; json_string(out, item.hint); out << '\n';
        out << "    }" << (i + 1 == report.checks.size() ? "\n" : ",\n");
    }

    out << "  ]\n";
    out << "}\n";
}

} // namespace acclorite
