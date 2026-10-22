#pragma once

#include <App/PropertyStandard.h>
#include <App/PropertyLinks.h>
#include <Mod/Part/App/PartFeature.h>

namespace DXFCAD {
class DXFEntity;

// Shared native persistence and lifecycle for DXF entities.
class DXFEntityObject : public Part::Feature {
    PROPERTY_HEADER_WITH_OVERRIDE(DXFCAD::DXFEntityObject);
public:
    App::PropertyString DXFData;
    App::PropertyString DXFType;
    App::PropertyString DXFLayer;
    App::PropertyString DXFHandle;
    App::PropertyString DisplayStatus;
    App::PropertyLinkHidden DXFSourceRoot;
    App::PropertyInteger DXFSourceIndex;

    short mustExecute() const override;
    App::DocumentObjectExecReturn* execute() override;
    void onDocumentRestored() override;
    const char* getViewProviderName() const override;
protected:
    DXFEntityObject(const char* type, const char* initialData);
    virtual const char* entityType() const = 0;
    virtual std::string entityContext() const { return {}; }
    virtual TopoDS_Shape buildShape(const DXFEntity& entity) const;
    virtual const char* shapeStatus() const { return "Centerline"; }
};

} // namespace DXFCAD
