#include "OffsetPlan.h"
#include "DocumentOperations.h"
#include <DXFCAD/Drafting.h>

#include <algorithm>
#include <limits>
#include <set>

namespace DXFCAD::FreeCAD {

OffsetPlan::OffsetPlan(const std::vector<py::object>& objects,double distance,bool keepOriginals)
{
    // Validate and normalize the complete selection before calculating any offsets.
    BatchObjectTransformPlan selection(objects,"move",Vector3D{},0,Vector3D{},true);
    const auto selected=selection.objects().cast<std::vector<py::object>>();
    std::vector<std::string> records;
    records.reserve(selected.size());
    for (auto object : selected)
        records.push_back(Drafting::offset(object.attr("DXFData").cast<std::string>(),distance));
    if (keepOriginals) copy_=std::make_unique<EntityCopyPlan>(selected,records);
    else update_=std::make_unique<EntityUpdatePlan>(selected,records);
}

bool OffsetPlan::copies() const { return static_cast<bool>(copy_); }

py::object OffsetPlan::document() const { return copy_ ? copy_->document() : update_->document(); }

py::list OffsetPlan::objects() const {
    if (copy_) return copy_->objects();
    py::list result;
    for (auto object : update_->objects()) result.append(object);
    return result;
}

py::list OffsetPlan::previewRecords() const { return copy_ ? copy_->previewRecords() : update_->previewRecords(); }

py::dict OffsetPlan::drawingBytes() const { return copy_ ? copy_->drawingBytes() : update_->drawingBytes(); }

std::vector<py::object> OffsetPlan::apply() { return copy_ ? copy_->apply() : update_->apply(); }

} // namespace DXFCAD::FreeCAD
