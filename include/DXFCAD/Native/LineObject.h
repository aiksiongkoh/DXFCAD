#pragma once
#include <DXFCAD/Native/EntityObject.h>

namespace DXFCAD {
class LineObject final : public EntityObject {
    PROPERTY_HEADER_WITH_OVERRIDE(DXFCAD::LineObject);
public:
    LineObject();
protected:
    const char* entityType() const override { return "LINE"; }
};
}
