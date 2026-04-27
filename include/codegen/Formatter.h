#ifndef ASN1_CODEGEN_FORMATTER_H
#define ASN1_CODEGEN_FORMATTER_H

#include <string>
#include <vector>

namespace asn1::codegen {

class Formatter {
private:
    int indentLevel;
    std::string indentString;

public:
    Formatter();
    ~Formatter() = default;

    void indent();
    void dedent();
    void reset();
    
    std::string getCurrentIndent() const;
    std::string formatComment(const std::string& text) const;
    std::string formatCode(const std::string& code) const;
    std::string joinLines(const std::vector<std::string>& lines) const;
};

} // namespace asn1::codegen

#endif // ASN1_CODEGEN_FORMATTER_H
