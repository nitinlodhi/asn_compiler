#include "codegen/TemplateManager.h"

namespace asn1::codegen {

TemplateManager::TemplateManager() {
    // Initialize built-in templates
    registerTemplate("encoder_integer", R"(
void encode_{{name}}(BitWriter& writer, int64_t value) {
    writer.writeBits(value, {{bits}});
}
)");
    
    registerTemplate("decoder_integer", R"(
int64_t decode_{{name}}(BitReader& reader) {
    return reader.readBits({{bits}});
}
)");
}

std::string TemplateManager::getTemplate(const std::string& templateName) const {
    auto it = templates.find(templateName);
    if (it != templates.end()) {
        return it->second;
    }
    return "";
}

void TemplateManager::registerTemplate(const std::string& name, const std::string& content) {
    templates[name] = content;
}

std::string TemplateManager::substituteTemplate(const std::string& templateName, 
                                               const std::unordered_map<std::string, std::string>& substitutions) {
    std::string result = getTemplate(templateName);
    
    for (const auto& pair : substitutions) {
        std::string placeholder = "{{" + pair.first + "}}";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), pair.second);
            pos += pair.second.length();
        }
    }
    
    return result;
}

} // namespace asn1::codegen
