#include "CornerObjectEditPlan.h"
#include "DocumentOperations.h"

#include <algorithm>
#include <limits>
#include <set>

namespace DXFCAD::FreeCAD {

CornerObjectEditPlan::CornerObjectEditPlan(const std::vector<py::object>& objects,const std::vector<std::string>& updates,
                     const std::string& connector,const std::string& connectorType) : objects_(objects)
{
    root_=cornerRoot(objects);
    document_=objects.front().attr("Document");
    snapshot_=std::make_unique<BatchObjectTransformPlan>(objects,"move",Vector3D{},0,Vector3D{},true);
    std::vector<std::pair<int,std::string>> live;
    std::array<std::size_t,2> selected{};
    for (auto handle : document_.attr("Objects")) {
        auto object=py::reinterpret_borrow<py::object>(handle);
        if (!root_.equal(py::getattr(object,"DXFSourceRoot",py::none())) || !py::hasattr(object,"DXFData")) continue;
        for (std::size_t i=0;i<2;++i) if (object.equal(objects[i])) selected[i]=live.size();
        live.emplace_back(object.attr("DXFSourceIndex").cast<int>(),object.attr("DXFData").cast<std::string>());
    }
    domain_=std::make_unique<CornerChangeSet>(drawingSource(root_),live,selected,updates,connector,connectorType);
    findLayerGroup(root_,parseEntity(domain_->connector())->layer());
}

py::object CornerObjectEditPlan::document() const { return document_; }

py::object CornerObjectEditPlan::root() const { return root_; }

const std::vector<py::object>& CornerObjectEditPlan::objects() const { return objects_; }

py::list CornerObjectEditPlan::previewRecords() const {
    py::list result;
    for (std::size_t i=0;i<2;++i) result.append(py::make_tuple(objects_[i],domain_->updates()[i]));
    result.append(py::make_tuple(objects_[0],domain_->connector()));
    return result;
}

py::bytes CornerObjectEditPlan::drawingBytes() const { return py::bytes(domain_->drawingBytes()); }

py::object CornerObjectEditPlan::apply()
{
    if (applied_) throw py::value_error("Corner edit plan has already been applied");
    snapshot_->validateCurrent("corner edit");
    const auto entity=drawingSource(root_).resolveEntity(domain_->connector());
    auto group=findLayerGroup(root_,entity->layer());
    applied_=true;
    group=materializeLayerGroup(root_,entity->layer(),group);
    for (std::size_t i=0;i<2;++i) objects_[i].attr("DXFData")=domain_->updates()[i];
    py::object connector=createOwnedEntity(root_,entity,-1,"",group);
    return connector;
}

} // namespace DXFCAD::FreeCAD
