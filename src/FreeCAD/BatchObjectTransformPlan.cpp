#include "BatchObjectTransformPlan.h"
#include "DocumentOperations.h"

#include <algorithm>
#include <limits>
#include <set>

namespace DXFCAD::FreeCAD {

BatchObjectTransformPlan::BatchObjectTransformPlan(const std::vector<py::object>& objects,const std::string& operation,
                         Vector3D offset,double value,Vector3D pivot,bool allowIdentity,
                         const std::vector<std::string>* copyRecords)
{
    if (objects.empty()) throw py::value_error("Select DXFCAD objects to transform");
    if (copyRecords && copyRecords->size()!=objects.size())
        throw py::value_error("Provide one copy record per selected object");
    document_=objects.front().attr("Document");
    std::set<std::string> seen;
    std::vector<BatchTransformInput> inputs;
    for (auto object : objects) {
        if (!document_.equal(py::object(object.attr("Document"))) || !py::hasattr(object,"DXFData"))
            throw py::value_error("Transform requires DXFCAD objects in one document");
        const auto name=object.attr("Name").cast<std::string>();
        if (!seen.insert(name).second) {
            if (copyRecords) throw py::value_error("Select each object only once for a copy plan");
            continue;
        }
        if (!object.attr("Placement").attr("isIdentity")().cast<bool>())
            throw py::value_error("Reset FreeCAD Placement before using native DXFCAD transforms");
        const auto status=py::getattr(object,"DisplayStatus",py::str("")).cast<std::string>();
        if (status.rfind("Error:",0)==0) throw py::value_error(status);
        Selected item{object,py::getattr(object,"DXFSourceRoot",py::none()),name,
            object.attr("DXFData").cast<std::string>(),
            py::getattr(object,"DXFContext",py::str("")).cast<std::string>(),
            py::getattr(object,"TextFont",py::str("")).cast<std::string>(),
            py::getattr(object,"DXFSourceIndex",py::int_(-1)).cast<int>(),py::hasattr(object,"DXFSourceIndex")};
        inputs.push_back({item.raw,item.context});
        if (!item.root.is_none()) {
            if (!document_.equal(py::object(item.root.attr("Document"))))
                throw py::value_error("Entity and drawing must belong to the same document");
            const auto key=item.root.attr("Name").cast<std::string>();
            if (!roots_.count(key)) {
                roots_.emplace(key,item.root);
                sourceSnapshots_[key]=item.root.attr("DXFSourceData").cast<std::string>();
                liveSnapshots_[key]=currentDrawingBytes(item.root);
            }
        }
        selected_.push_back(std::move(item));
    }
    domain_=std::make_unique<BatchTransformChangeSet>(inputs,operation,offset,value,pivot,allowIdentity);
    records_=domain_->records();
    if (copyRecords) {
        if (!domain_->copies()) throw py::value_error("Copy records require a copy operation");
        for (std::size_t i=0;i<selected_.size();++i) {
            const auto original=parseEntity(selected_[i].raw,selected_[i].context);
            const auto candidate=parseEntity((*copyRecords)[i],selected_[i].context);
            candidate->geometry();
            if (candidate->type()!=original->type() || candidate->layer()!=original->layer())
                throw py::value_error("Related copies must retain entity type and layer");
            records_[i]=candidate->copied(Vector3D{});
        }
    }
    if (domain_->copies())
        for (const auto& item : selected_)
            if (item.root.is_none()) throw py::value_error("Copy requires objects belonging to a DXFCAD drawing");
    for (const auto& [key,root] : roots_) {
        std::map<std::string,std::string> updates;
        std::vector<std::string> additions;
        for (std::size_t i=0;i<selected_.size();++i) {
            const auto& item=selected_[i];
            if (!item.root.equal(root)) continue;
            if (domain_->copies()) {
                additions.push_back(records_[i]);
                findLayerGroup(root,parseEntity(records_[i],item.context)->layer());
            } else updates.emplace(item.name,records_[i]);
        }
        std::vector<std::pair<int,std::string>> proposed;
        for (auto handle : document_.attr("Objects")) {
            auto object=py::reinterpret_borrow<py::object>(handle);
            if (!root.equal(py::getattr(object,"DXFSourceRoot",py::none())) || !py::hasattr(object,"DXFData")) continue;
            const auto found=updates.find(object.attr("Name").cast<std::string>());
            proposed.emplace_back(object.attr("DXFSourceIndex").cast<int>(),
                found==updates.end() ? object.attr("DXFData").cast<std::string>() : found->second);
        }
        for (const auto& raw : additions) proposed.emplace_back(-1,raw);
        drawingBytes_[key]=drawingSource(root).serializeCurrent(proposed);
    }
}

py::object BatchObjectTransformPlan::document() const { return document_; }

bool BatchObjectTransformPlan::copies() const { return domain_->copies(); }

py::list BatchObjectTransformPlan::objects() const {
    py::list result;
    for (const auto& item : selected_) result.append(item.object);
    return result;
}

py::list BatchObjectTransformPlan::previewRecords() const {
    py::list result;
    for (std::size_t i=0;i<selected_.size();++i)
        result.append(py::make_tuple(selected_[i].object,records_[i]));
    return result;
}

py::dict BatchObjectTransformPlan::drawingBytes() const {
    py::dict result;
    for (const auto& [key,bytes] : drawingBytes_) result[py::str(key)]=py::bytes(bytes);
    return result;
}

void BatchObjectTransformPlan::validateCurrent(const std::string& kind) const
{
    if (applied_) throw py::value_error("Batch transform plan has already been applied");
    for (const auto& item : selected_) {
        py::object current=document_.attr("getObject")(item.name);
        if (current.is_none() || !current.equal(item.object)
            || current.attr("DXFData").cast<std::string>()!=item.raw
            || py::getattr(current,"DXFContext",py::str("")).cast<std::string>()!=item.context
            || py::getattr(current,"TextFont",py::str("")).cast<std::string>()!=item.font
            || !item.root.equal(py::getattr(current,"DXFSourceRoot",py::none()))
            || py::hasattr(current,"DXFSourceIndex")!=item.hasIndex
            || py::getattr(current,"DXFSourceIndex",py::int_(-1)).cast<int>()!=item.index)
            throw py::value_error("Selected objects changed; rebuild the "+kind+" plan");
        if (!current.attr("Placement").attr("isIdentity")().cast<bool>())
            throw py::value_error("Reset FreeCAD Placement before using native DXFCAD transforms");
    }
    for (const auto& [key,root] : roots_)
        if (sourceSnapshots_.at(key)!=root.attr("DXFSourceData").cast<std::string>()
            || liveSnapshots_.at(key)!=currentDrawingBytes(root))
            throw py::value_error("Drawing changed; rebuild the "+kind+" plan");
    if (domain_->copies())
        for (std::size_t i=0;i<selected_.size();++i)
            findLayerGroup(selected_[i].root,parseEntity(records_[i],selected_[i].context)->layer());
}

std::vector<py::object> BatchObjectTransformPlan::apply()
{
    validateCurrent();
    applied_=true;
    std::vector<py::object> result;
    for (std::size_t i=0;i<selected_.size();++i) {
        const auto& item=selected_[i];
        const auto& raw=records_[i];
        if (!domain_->copies()) {
            item.object.attr("DXFData")=raw;
            result.push_back(item.object);
        } else {
            const auto entity=drawingSource(item.root).resolveEntity(raw);
            auto group=materializeLayerGroup(item.root,entity->layer(),findLayerGroup(item.root,entity->layer()));
            py::object clone=createOwnedEntity(item.root,entity,-1,item.font,group);
            result.push_back(clone);
        }
    }
    return result;
}

} // namespace DXFCAD::FreeCAD
