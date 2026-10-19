#pragma once
#include <DXFCAD/DXFEntityObject.h>
#include <App/PropertyFile.h>

namespace DXFCAD {
class TextObject final : public DXFEntityObject {
    PROPERTY_HEADER_WITH_OVERRIDE(DXFCAD::TextObject);
public:
    TextObject();
    App::PropertyFile TextFont;
    short mustExecute() const override;
protected:
    const char* entityType() const override { return "TEXT"; }
    TopoDS_Shape buildShape(const DXFEntity& entity) const override;
    const char* shapeStatus() const override { return "Text outlines (substitute font)"; }
};
}
