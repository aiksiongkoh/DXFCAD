#pragma once

#include "AdapterTypes.h"
#include <DXFCAD/EditPlan.h>
#include "BatchObjectTransformPlan.h"

namespace DXFCAD::FreeCAD {

// Geometry/content replacements preserve object identity and presentation.
// Layer/style changes use LayerEditPlan, which also updates group membership.
class EntityUpdatePlan : public EditPlan {
    std::vector<py::object> objects_;
    std::vector<std::string> records_;
    std::unique_ptr<BatchObjectTransformPlan> snapshot_;
    py::object document_;
    std::map<std::string,std::string> drawingBytes_;
    bool applied_=false;
public:
    EntityUpdatePlan(const std::vector<py::object>& objects,const std::vector<std::string>& records);
    py::object document() const;
    const std::vector<py::object>& objects() const;
    py::list previewRecords() const;
    py::dict drawingBytes() const;
    std::vector<py::object> apply();
};

} // namespace DXFCAD::FreeCAD
