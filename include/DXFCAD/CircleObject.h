#pragma once
#include <DXFCAD/DXFEntityObject.h>

namespace DXFCAD {
class CircleObject final : public DXFEntityObject {
    PROPERTY_HEADER_WITH_OVERRIDE(DXFCAD::CircleObject);
public:
    CircleObject();
protected:
    const char* entityType() const override { return "CIRCLE"; }
};
}
