#pragma once
#include <DXFCAD/DXFEntityObject.h>

namespace DXFCAD {
class LineObject final : public DXFEntityObject {
    PROPERTY_HEADER_WITH_OVERRIDE(DXFCAD::LineObject);
public:
    LineObject();
protected:
    const char* entityType() const override { return "LINE"; }
};
}
