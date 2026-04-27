#ifndef ASN1_FRONTEND_SOURCE_LOCATION_H
#define ASN1_FRONTEND_SOURCE_LOCATION_H

#include <string>

namespace asn1::frontend {

class SourceLocation {
public:
    std::string filename;
    unsigned int line;
    unsigned int column;

    SourceLocation();
    SourceLocation(const std::string& filename, unsigned int line, unsigned int column);
    ~SourceLocation() = default;

    std::string toString() const;
};

} // namespace asn1::frontend

#endif // ASN1_FRONTEND_SOURCE_LOCATION_H
