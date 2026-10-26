#pragma once
#include <DXFCAD/DXFDocument.h>

namespace DXFCAD {
// General native-record conversions. No downstream application dependency.
class EntityConversion {
public:
    static std::vector<std::string> explode(const DXFEntity& entity);
    // An explicit 0..360 ARC is a full-turn working curve, distinct from the
    // ambiguous equal-angle ARC. The native CIRCLE source is never modified.
    static std::string circleAsArc(const std::string& raw);
};
}
