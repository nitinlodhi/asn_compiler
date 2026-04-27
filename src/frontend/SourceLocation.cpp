#include "frontend/SourceLocation.h"

namespace asn1::frontend {

SourceLocation::SourceLocation()
    : filename("<unknown>"), line(1), column(1) {}

SourceLocation::SourceLocation(const std::string& filename, unsigned int line, unsigned int column)
    : filename(filename), line(line), column(column) {}

std::string SourceLocation::toString() const {
    return filename + ":" + std::to_string(line) + ":" + std::to_string(column);
}

} // namespace asn1::frontend
