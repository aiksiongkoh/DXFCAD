#pragma once

#include <DXFCAD/DXFEntityObject.h>

namespace DXFCAD {

// FreeCAD persistence and recomputation live here. LWPolyline remains the
// independent C++ geometry model, reconstructed from authoritative DXFData.
class LWPolylineObject final : public DXFEntityObject {
    PROPERTY_HEADER_WITH_OVERRIDE(DXFCAD::LWPolylineObject);

public:
    LWPolylineObject();
    App::PropertyBool ShowWidths;

    short mustExecute() const override;
protected:
    const char* entityType() const override { return "LWPOLYLINE"; }
    TopoDS_Shape buildShape(const DXFEntity& entity) const override;
    const char* shapeStatus() const override;
};

} // namespace DXFCAD
