#pragma once

#include <App/PropertyStandard.h>
#include <Mod/Part/App/PartFeature.h>

namespace DXFCAD {

// FreeCAD persistence and recomputation live here. LWPolyline remains the
// independent C++ geometry model, reconstructed from authoritative DXFData.
class LWPolylineObject final : public Part::Feature {
    PROPERTY_HEADER_WITH_OVERRIDE(DXFCAD::LWPolylineObject);

public:
    LWPolylineObject();
    App::PropertyString DXFData;
    App::PropertyString DXFType;
    App::PropertyString DXFLayer;
    App::PropertyString DXFHandle;
    App::PropertyString DisplayStatus;
    App::PropertyBool ShowWidths;

    short mustExecute() const override;
    App::DocumentObjectExecReturn* execute() override;
    void onDocumentRestored() override;
    const char* getViewProviderName() const override;
};

} // namespace DXFCAD
