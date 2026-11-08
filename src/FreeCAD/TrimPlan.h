#pragma once

#include "AdapterTypes.h"
#include <DXFCAD/EditPlan.h>
#include "BatchObjectTransformPlan.h"
#include "EntityUpdatePlan.h"

namespace DXFCAD::FreeCAD {

class TrimPlan : public EditPlan {
    py::object target_,boundary_;
    std::unique_ptr<BatchObjectTransformPlan> dependencies_;
    std::unique_ptr<EntityUpdatePlan> update_;
    bool applied_=false;
public:
    TrimPlan(py::object target,py::object boundary,bool start,bool extend);
    py::object document() const;
    py::object target() const;
    py::object boundary() const;
    const std::vector<py::object>& objects() const;
    py::list previewRecords() const;
    py::dict drawingBytes() const;
    std::vector<py::object> apply();
};

} // namespace DXFCAD::FreeCAD
