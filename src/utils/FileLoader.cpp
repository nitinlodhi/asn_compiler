#include "utils/FileLoader.h"
#include <fstream>
#include <sstream>

namespace asn1::utils {

std::optional<std::string> FileLoader::loadFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return std::nullopt;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool FileLoader::saveFile(const std::string& filename, const std::string& content) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    file << content;
    return true;
}

std::vector<std::string> FileLoader::loadDirectory(const std::string& dirname, const std::string& extension) {
    // TODO: Implement directory loading
    std::vector<std::string> files;
    return files;
}

std::string FileLoader::getDirectory(const std::string& path) {
    size_t found = path.find_last_of("/\\");
    if (found != std::string::npos) {
        return path.substr(0, found);
    }
    return ""; // No directory part
}

bool FileLoader::fileExists(const std::string& path) {
    std::ifstream f(path.c_str());
    return f.good();
}

std::optional<std::string> FileLoader::findModuleFile(const std::string& moduleName, const std::string& currentFilePath, const std::vector<std::string>& includePaths) {
    std::string filename = moduleName + ".asn1";

    // 1. Search relative to the current file's directory
    std::string currentDir = getDirectory(currentFilePath);
    if (!currentDir.empty()) {
        std::string relativePath = currentDir + "/" + filename;
        if (fileExists(relativePath)) {
            return relativePath;
        }
    }

    // 2. Search in the provided include paths
    for (const auto& path : includePaths) {
        std::string includePath = path + "/" + filename;
        if (fileExists(includePath)) {
            return includePath;
        }
    }

    // 3. As a last resort, check the current working directory
    if (fileExists(filename)) {
        return filename;
    }

    return std::nullopt;
}

} // namespace asn1::utils
