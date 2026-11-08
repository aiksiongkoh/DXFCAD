#pragma once

#include "AdapterTypes.h"
#include <DXFCAD/EditPlan.h>
#include "EntityCopyPlan.h"
#include "EntityUpdatePlan.h"

namespace DXFCAD::FreeCAD {

class OffsetPlan : public EditPlan {
    std::unique_ptr<EntityCopyPlan> copy_;
    std::unique_ptr<EntityUpdatePlan> update_;
public:
    OffsetPlan(const std::vector<py::object>& objects,double distance,bool keepOriginals);
    bool copies() const;
    py::object document() const;
    py::list objects() const;
    py::list previewRecords() const;
    py::dict drawingBytes() const;
    std::vector<py::object> apply();
};

} // namespace DXFCAD::FreeCAD
