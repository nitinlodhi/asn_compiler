#ifndef ASN1_CODEGEN_TEMPLATE_MANAGER_H
#define ASN1_CODEGEN_TEMPLATE_MANAGER_H

#include <string>
#include <unordered_map>

namespace asn1::codegen {

class TemplateManager {
private:
    std::unordered_map<std::string, std::string> templates;

public:
    TemplateManager();
    ~TemplateManager() = default;

    std::string getTemplate(const std::string& templateName) const;
    void registerTemplate(const std::string& name, const std::string& content);
    std::string substituteTemplate(const std::string& templateName, 
                                  const std::unordered_map<std::string, std::string>& substitutions);
};

} // namespace asn1::codegen

#endif // ASN1_CODEGEN_TEMPLATE_MANAGER_H
