#pragma once

#include "AdapterTypes.h"
#include <DXFCAD/EditPlan.h>
#include "ContourObjectEditPlan.h"
#include "BatchObjectTransformPlan.h"

namespace DXFCAD::FreeCAD {

class ContourOperationPlan : public EditPlan {
    std::unique_ptr<BatchObjectTransformPlan> dependencies_;
    std::unique_ptr<ContourObjectEditPlan> edit_;
    bool applied_=false;
public:
    ContourOperationPlan(const std::vector<py::object>& objects,const std::string& operation,
                         std::size_t targetIndex,const std::vector<double>& distances,double tolerance);
    py::object document() const;
    py::object root() const;
    py::object templateObject() const;
    bool preservesFirst() const;
    const std::vector<py::object>& createdObjects() const;
    py::list previewRecords() const;
    py::bytes drawingBytes() const;
    std::vector<py::object> apply();
};

} // namespace DXFCAD::FreeCAD
