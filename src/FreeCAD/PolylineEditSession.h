#pragma once

#include "AdapterTypes.h"
#include <DXFCAD/DXFLWPolyline.h>
#include "EntityUpdatePlan.h"
#include "EntityEditSnapshot.h"

namespace DXFCAD::FreeCAD {

class PolylineEditSession {
    EntityEditSnapshot snapshot_;
public:
    explicit PolylineEditSession(py::object object);
    py::object object() const;
    const std::string& originalData() const;
    DXFLWPolyline polyline() const;
    std::unique_ptr<EntityUpdatePlan> prepare(const DXFLWPolyline& polyline,const std::vector<int>& origins) const;
};

} // namespace DXFCAD::FreeCAD
