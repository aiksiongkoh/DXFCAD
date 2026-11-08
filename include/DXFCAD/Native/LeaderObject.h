#pragma once
#include <DXFCAD/Native/EntityObject.h>

namespace DXFCAD {
class LeaderObject final : public EntityObject {
    PROPERTY_HEADER_WITH_OVERRIDE(DXFCAD::LeaderObject);
public:
    LeaderObject();
    App::PropertyString DXFContext;
    short mustExecute() const override;
protected:
    const char* entityType() const override { return "LEADER"; }
    std::string entityContext() const override { return DXFContext.getValue(); }
    const char* shapeStatus() const override { return "Leader path and arrowhead"; }
};
}
