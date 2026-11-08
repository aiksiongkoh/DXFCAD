#pragma once
#include <DXFCAD/Native/EntityObject.h>

namespace DXFCAD {
class CircleObject final : public EntityObject {
    PROPERTY_HEADER_WITH_OVERRIDE(DXFCAD::CircleObject);
public:
    CircleObject();
protected:
    const char* entityType() const override { return "CIRCLE"; }
};
}
