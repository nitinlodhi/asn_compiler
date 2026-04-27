#ifndef ASN1_UTILS_LOGGER_H
#define ASN1_UTILS_LOGGER_H

#include <string>
#include <iostream>

namespace asn1::utils {

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
};

class Logger {
private:
    static LogLevel currentLevel;

public:
    static void setLogLevel(LogLevel level);
    static void debug(const std::string& message);
    static void info(const std::string& message);
    static void warning(const std::string& message);
    static void error(const std::string& message);
};

} // namespace asn1::utils

#endif // ASN1_UTILS_LOGGER_H
