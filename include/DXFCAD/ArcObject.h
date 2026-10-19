#pragma once
#include <DXFCAD/DXFEntityObject.h>

namespace DXFCAD {
class ArcObject final : public DXFEntityObject {
    PROPERTY_HEADER_WITH_OVERRIDE(DXFCAD::ArcObject);
public:
    ArcObject();
protected:
    const char* entityType() const override { return "ARC"; }
};
}
