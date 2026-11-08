#include "EntityUpdatePlan.h"
#include "DocumentOperations.h"
#include <DXFCAD/Drafting.h>

#include <algorithm>
#include <limits>
#include <set>

namespace DXFCAD::FreeCAD {

EntityUpdatePlan::EntityUpdatePlan(const std::vector<py::object>& objects,const std::vector<std::string>& records)
    : objects_(objects),records_(records)
{
    if (objects.empty() || objects.size()!=records.size())
        throw py::value_error("Provide one replacement record per selected DXFCAD object");
    std::set<std::string> names;
    for (auto object : objects)
        if (!names.insert(object.attr("Name").cast<std::string>()).second)
            throw py::value_error("Select each object only once for an entity update");
    snapshot_=std::make_unique<BatchObjectTransformPlan>(objects,"move",Vector3D{},0,Vector3D{},true);
    document_=snapshot_->document();
    std::map<std::string,py::object> roots;
    std::map<std::string,std::string> updates;
    for (std::size_t i=0;i<objects.size();++i) {
        auto object=objects[i];
        const auto context=py::getattr(object,"DXFContext",py::str("")).cast<std::string>();
        const auto original=parseEntity(object.attr("DXFData").cast<std::string>(),context);
        const auto replacement=parseEntity(records[i],context);
        replacement->geometry();
        const auto before=nativeEntityFields(original->record());
        const auto after=nativeEntityFields(replacement->record());
        // Compare every occurrence, so duplicate identity/style fields cannot bypass validation.
        for (int code : {0,5,330,67,410,8,62,420,430,440,60,6,48,370}) {
            std::vector<std::string> a,b;
            for (const auto& field : before.groups) if (field.first==code) a.push_back(field.second);
            for (const auto& field : after.groups) if (field.first==code) b.push_back(field.second);
            if (a!=b) throw py::value_error("Entity updates must retain type, identity, layer and display properties");
        }
        updates.emplace(object.attr("Name").cast<std::string>(),records[i]);
        auto root=py::getattr(object,"DXFSourceRoot",py::none());
        if (!root.is_none()) roots.emplace(root.attr("Name").cast<std::string>(),root);
    }
    // Validate the complete proposed drawing, including untouched and opaque records.
    for (const auto& [name,root] : roots) {
        std::vector<std::pair<int,std::string>> proposed;
        for (auto handle : document_.attr("Objects")) {
            auto object=py::reinterpret_borrow<py::object>(handle);
            if (!root.equal(py::getattr(object,"DXFSourceRoot",py::none())) || !py::hasattr(object,"DXFData")) continue;
            const auto found=updates.find(object.attr("Name").cast<std::string>());
            proposed.emplace_back(object.attr("DXFSourceIndex").cast<int>(),
                found==updates.end() ? object.attr("DXFData").cast<std::string>() : found->second);
        }
        drawingBytes_[name]=drawingSource(root).serializeCurrent(proposed);
    }
}

py::object EntityUpdatePlan::document() const { return document_; }

const std::vector<py::object>& EntityUpdatePlan::objects() const { return objects_; }

py::list EntityUpdatePlan::previewRecords() const {
    py::list result;
    for (std::size_t i=0;i<objects_.size();++i) result.append(py::make_tuple(objects_[i],records_[i]));
    return result;
}

py::dict EntityUpdatePlan::drawingBytes() const {
    py::dict result;
    for (const auto& [name,bytes] : drawingBytes_) result[py::str(name)]=py::bytes(bytes);
    return result;
}

std::vector<py::object> EntityUpdatePlan::apply() {
    if (applied_) throw py::value_error("Entity update plan has already been applied");
    snapshot_->validateCurrent("entity update");
    applied_=true;
    for (std::size_t i=0;i<objects_.size();++i) objects_[i].attr("DXFData")=records_[i];
    return objects_;
}

} // namespace DXFCAD::FreeCAD
