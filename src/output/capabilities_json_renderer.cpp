#include "acclorite/output/capabilities_json_renderer.hpp"

#include <ostream>

#include "acclorite/core/machine_interface.hpp"

namespace acclorite {
namespace {

void boolean(std::ostream& out, const bool value) {
    out << (value ? "true" : "false");
}

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
        default: out << static_cast<char>(ch); break;
        }
    }
    out << '"';
}

} // namespace

void CapabilitiesJsonRenderer::render(
    const CapabilityReport& report,
    const std::string_view acclorite_version,
    std::ostream& out
) const {
    out << "{\n";
    out << "  \"capabilities_schema_version\": " << machine::kCapabilitiesSchemaVersion << ",\n";
    out << "  \"acclorite_version\": "; json_string(out, acclorite_version); out << ",\n";
    out << "  \"search_schema_version\": " << machine::kSearchSchemaVersion << ",\n";
    out << "  \"doctor_schema_version\": " << machine::kDoctorSchemaVersion << ",\n";
    out << "  \"offline_runtime\": true,\n";
    out << "  \"non_interactive\": true,\n";
    out << "  \"commands\": {\n";
    out << "    \"search\": true,\n";
    out << "    \"doctor\": true,\n";
    out << "    \"reindex\": true\n";
    out << "  },\n";
    out << "  \"output\": {\n";
    out << "    \"json\": true,\n";
    out << "    \"ranking_explanations\": true,\n";
    out << "    \"profiling\": true\n";
    out << "  },\n";
    out << "  \"build\": {\n";
    out << "    \"sqlite_fts\": "; boolean(out, report.sqlite_fts); out << '\n';
    out << "  },\n";
    out << "  \"integrations\": {\n";
    out << "    \"manual_guidance\": "; boolean(out, report.manual_guidance); out << ",\n";
    out << "    \"manual_syntax\": "; boolean(out, report.manual_syntax); out << ",\n";
    out << "    \"fish_completion_syntax\": "; boolean(out, report.fish_completion_syntax); out << ",\n";
    out << "    \"info_guidance\": "; boolean(out, report.info_guidance); out << ",\n";
    out << "    \"tldr_cache\": "; boolean(out, report.tldr_cache); out << ",\n";
    out << "    \"curated_guidance\": "; boolean(out, report.curated_guidance); out << ",\n";
    out << "    \"arch_packages\": "; boolean(out, report.arch_packages); out << ",\n";
    out << "    \"apt_packages\": "; boolean(out, report.apt_packages); out << ",\n";
    out << "    \"dnf_packages\": "; boolean(out, report.dnf_packages); out << ",\n";
    out << "    \"zypper_packages\": "; boolean(out, report.zypper_packages); out << ",\n";
    out << "    \"xbps_packages\": "; boolean(out, report.xbps_packages); out << ",\n";
    out << "    \"pkgfile\": "; boolean(out, report.pkgfile); out << '\n';
    out << "  },\n";
    out << "  \"exit_codes\": {\n";
    out << "    \"success\": " << machine::exit_code(machine::ExitCode::Success) << ",\n";
    out << "    \"no_result\": " << machine::exit_code(machine::ExitCode::NoResult) << ",\n";
    out << "    \"usage_error\": " << machine::exit_code(machine::ExitCode::UsageError) << ",\n";
    out << "    \"operational_error\": " << machine::exit_code(machine::ExitCode::OperationalError) << ",\n";
    out << "    \"diagnostic_error\": " << machine::exit_code(machine::ExitCode::DiagnosticError) << '\n';
    out << "  }\n";
    out << "}\n";
}

} // namespace acclorite
