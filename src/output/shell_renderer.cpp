#include "acclorite/output/shell_renderer.hpp"

#include <cctype>
#include <string>
#include <string_view>

namespace acclorite::output {
namespace {

bool shell_safe_byte(const unsigned char ch) {
    return std::isalnum(ch) || ch == '_' || ch == '@' || ch == '%' || ch == '+' ||
           ch == '=' || ch == ':' || ch == ',' || ch == '.' || ch == '/' || ch == '-' ||
           ch == '~';
}

} // namespace

std::string shell_quote(std::string_view value) {
    if (value.empty()) {
        return "''";
    }

    bool safe = true;
    for (const unsigned char ch : value) {
        if (!shell_safe_byte(ch)) {
            safe = false;
            break;
        }
    }
    if (safe) {
        return std::string(value);
    }

    // Preserve ordinary shell tilde expansion while quoting the path remainder.
    // Quoting the entire `~/...` token would turn `~` into a literal character.
    if (value.starts_with("~/")) {
        return "~/" + shell_quote(value.substr(2));
    }

    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('\'');
    for (const char ch : value) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('\'');
    return out;
}

std::string render_shell_invocation(const CommandInvocation& invocation) {
    std::string out = shell_quote(invocation.command);
    for (const auto& argument : invocation.arguments) {
        out.push_back(' ');
        out += argument.placeholder ? argument.value : shell_quote(argument.value);
    }
    return out;
}

} // namespace acclorite::output
