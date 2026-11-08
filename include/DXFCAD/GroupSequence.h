#pragma once

#include <string>
#include <utility>
#include <vector>

namespace DXFCAD {

// Ordered DXF group-code/value pairs, not a DXF entity/object type. A sequence
// can hold one entity, a compound entity or a complete drawing's source data.
// Unknown codes, their values and their order remain authoritative.
class GroupSequence {
public:
    std::vector<std::pair<int, std::string>> groups;
    std::string value(int code, const std::string& fallback = "") const;
    double number(int code, double fallback = 0) const;
    int integer(int code, int fallback = 0) const;
    std::string raw() const;
};


} // namespace DXFCAD
