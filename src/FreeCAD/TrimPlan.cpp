#include "TrimPlan.h"
#include "DocumentOperations.h"
#include <DXFCAD/Drafting.h>

#include <algorithm>
#include <limits>
#include <set>

namespace DXFCAD::FreeCAD {

TrimPlan::TrimPlan(py::object target,py::object boundary,bool start,bool extend)
    : target_(target),boundary_(boundary)
{
    if (target.equal(boundary)) throw py::value_error("Choose distinct trim target and boundary objects");
    dependencies_=std::make_unique<BatchObjectTransformPlan>(
        std::vector<py::object>{target,boundary},"move",Vector3D{},0,Vector3D{},true);
    auto raw=Drafting::trim(target.attr("DXFData").cast<std::string>(),
        boundary.attr("DXFData").cast<std::string>(),start,extend);
    update_=std::make_unique<EntityUpdatePlan>(std::vector<py::object>{target},std::vector<std::string>{raw});
}

py::object TrimPlan::document() const { return update_->document(); }

py::object TrimPlan::target() const { return target_; }

py::object TrimPlan::boundary() const { return boundary_; }

const std::vector<py::object>& TrimPlan::objects() const { return update_->objects(); }

py::list TrimPlan::previewRecords() const { return update_->previewRecords(); }

py::dict TrimPlan::drawingBytes() const { return update_->drawingBytes(); }

std::vector<py::object> TrimPlan::apply() {
    if (applied_) throw py::value_error("Trim/extend plan has already been applied");
    dependencies_->validateCurrent("trim/extend");
    applied_=true;
    return update_->apply();
}

} // namespace DXFCAD::FreeCAD
