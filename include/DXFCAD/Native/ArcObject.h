#pragma once
#include <DXFCAD/Native/EntityObject.h>

namespace DXFCAD {
class ArcObject final : public EntityObject {
    PROPERTY_HEADER_WITH_OVERRIDE(DXFCAD::ArcObject);
public:
    ArcObject();
protected:
    const char* entityType() const override { return "ARC"; }
};
}
