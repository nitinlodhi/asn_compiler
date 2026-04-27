#ifndef ASN1_UTILS_FILE_LOADER_H
#define ASN1_UTILS_FILE_LOADER_H

#include <string>
#include <vector>
#include <optional>

namespace asn1::utils {

class FileLoader {
public:
    static std::optional<std::string> loadFile(const std::string& filename);
    static bool saveFile(const std::string& filename, const std::string& content);
    static std::vector<std::string> loadDirectory(const std::string& dirname, const std::string& extension);
    static std::string getDirectory(const std::string& path);
    static bool fileExists(const std::string& path);
    static std::optional<std::string> findModuleFile(const std::string& moduleName, const std::string& currentFilePath, const std::vector<std::string>& includePaths);
};

} // namespace asn1::utils

#endif // ASN1_UTILS_FILE_LOADER_H
