#include "utils/Logger.h"

namespace asn1::utils {

LogLevel Logger::currentLevel = LogLevel::INFO;

void Logger::setLogLevel(LogLevel level) {
    currentLevel = level;
}

void Logger::debug(const std::string& message) {
    if (currentLevel <= LogLevel::DEBUG) {
        std::cout << "[DEBUG] " << message << std::endl;
    }
}

void Logger::info(const std::string& message) {
    if (currentLevel <= LogLevel::INFO) {
        std::cout << "[INFO] " << message << std::endl;
    }
}

void Logger::warning(const std::string& message) {
    if (currentLevel <= LogLevel::WARNING) {
        std::cerr << "[WARNING] " << message << std::endl;
    }
}

void Logger::error(const std::string& message) {
    if (currentLevel <= LogLevel::ERROR) {
        std::cerr << "[ERROR] " << message << std::endl;
    }
}

} // namespace asn1::utils
