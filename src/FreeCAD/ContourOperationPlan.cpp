#include "ContourOperationPlan.h"
#include "DocumentOperations.h"

#include <algorithm>
#include <limits>
#include <set>

namespace DXFCAD::FreeCAD {

ContourOperationPlan::ContourOperationPlan(const std::vector<py::object>& objects,const std::string& operation,
                     std::size_t targetIndex,const std::vector<double>& distances,double tolerance)
{
    if (operation!="join" && operation!="close" && operation!="split" && operation!="break" && operation!="trim")
        throw py::value_error("Unknown contour operation");
    if (objects.empty() || targetIndex>=objects.size()) throw py::value_error("Select a valid target contour");
    if ((operation=="join" || operation=="trim") ? objects.size()<2 : objects.size()!=1)
        throw py::value_error("Select the required contours for this operation");
    for (std::size_t i=0;i<objects.size();++i)
        for (std::size_t j=0;j<i;++j)
            if (objects[i].equal(objects[j])) throw py::value_error("Select each contour only once");
    dependencies_=std::make_unique<BatchObjectTransformPlan>(objects,"move",Vector3D{},0,Vector3D{},true);
    auto target=objects[targetIndex];
    std::vector<std::string> records,raws;
    for (auto object : objects) raws.push_back(object.attr("DXFData").cast<std::string>());
    if (operation=="join" || operation=="close") {
        if (!distances.empty()) throw py::value_error("Join/close do not take cut distances");
        records={operation=="join" ? Contour::join(raws,tolerance) : Contour::close(raws[targetIndex],tolerance)};
    } else if (operation=="split") records=Contour::split(raws[targetIndex],distances);
    else if (operation=="break") {
        if (distances.size()!=2) throw py::value_error("Break requires two distances");
        records=Contour::breakBetween(raws[targetIndex],distances[0],distances[1]);
    } else {
        if (distances.size()!=1) throw py::value_error("Interior trim requires one pick distance");
        raws.erase(raws.begin()+targetIndex);
        records=Contour::trimInterior(target.attr("DXFData").cast<std::string>(),raws,distances[0]);
    }
    edit_=std::make_unique<ContourObjectEditPlan>(operation=="join" ? objects : std::vector<py::object>{target},
                                                records,operation!="join");
}

py::object ContourOperationPlan::document() const { return edit_->document(); }

py::object ContourOperationPlan::root() const { return edit_->root(); }

py::object ContourOperationPlan::templateObject() const { return edit_->templateObject(); }

bool ContourOperationPlan::preservesFirst() const { return edit_->preservesFirst(); }

const std::vector<py::object>& ContourOperationPlan::createdObjects() const { return edit_->createdObjects(); }

py::list ContourOperationPlan::previewRecords() const { return edit_->previewRecords(); }

py::bytes ContourOperationPlan::drawingBytes() const { return edit_->drawingBytes(); }

std::vector<py::object> ContourOperationPlan::apply() {
    if (applied_) throw py::value_error("Contour operation plan has already been applied");
    dependencies_->validateCurrent("contour operation");
    applied_=true;
    return edit_->apply();
}

} // namespace DXFCAD::FreeCAD
