#include "codegen/Formatter.h"

namespace asn1::codegen {

Formatter::Formatter() : indentLevel(0), indentString("    ") {}

void Formatter::indent() {
    indentLevel++;
}

void Formatter::dedent() {
    if (indentLevel > 0) {
        indentLevel--;
    }
}

void Formatter::reset() {
    indentLevel = 0;
}

std::string Formatter::getCurrentIndent() const {
    std::string result;
    for (int i = 0; i < indentLevel; i++) {
        result += indentString;
    }
    return result;
}

std::string Formatter::formatComment(const std::string& text) const {
    return getCurrentIndent() + "// " + text;
}

std::string Formatter::formatCode(const std::string& code) const {
    return getCurrentIndent() + code;
}

std::string Formatter::joinLines(const std::vector<std::string>& lines) const {
    std::string result;
    for (const auto& line : lines) {
        result += line + "\n";
    }
    return result;
}

} // namespace asn1::codegen
