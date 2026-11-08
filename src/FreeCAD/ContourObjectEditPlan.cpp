#include "ContourObjectEditPlan.h"
#include "DocumentOperations.h"

#include <algorithm>
#include <limits>
#include <set>

namespace DXFCAD::FreeCAD {

ContourObjectEditPlan::ContourObjectEditPlan(const std::vector<py::object>& objects,const std::vector<std::string>& results,bool preserveFirst)
    : selected_(objects)
{
    if (objects.empty()) throw py::value_error("Select contours to replace");
    document_=objects.front().attr("Document");
    if (!py::hasattr(objects.front(),"DXFSourceRoot") || objects.front().attr("DXFSourceRoot").is_none())
        throw py::value_error("Contour cleanup requires objects in one DXFCAD drawing");
    root_=objects.front().attr("DXFSourceRoot");
    if (!document_.equal(py::object(root_.attr("Document"))))
        throw py::value_error("Contour cleanup requires objects in one DXFCAD drawing");
    const auto source=drawingSource(root_);
    sourceSnapshot_=root_.attr("DXFSourceData").cast<std::string>();
    liveSnapshot_=currentDrawingBytes(root_); // Includes placement and ownership validation.
    std::vector<py::object> liveObjects;
    std::vector<std::pair<int,std::string>> live;
    for (auto item : document_.attr("Objects")) {
        auto object=py::reinterpret_borrow<py::object>(item);
        if (py::hasattr(object,"DXFSourceRoot") && root_.equal(py::object(object.attr("DXFSourceRoot")))
            && py::hasattr(object,"DXFData")) {
            liveObjects.push_back(object);
            live.emplace_back(object.attr("DXFSourceIndex").cast<int>(),object.attr("DXFData").cast<std::string>());
        }
    }
    std::vector<std::size_t> selection;
    for (auto object : objects) {
        if (!document_.equal(py::object(object.attr("Document"))) || !py::hasattr(object,"DXFSourceRoot")
            || !root_.equal(py::object(object.attr("DXFSourceRoot"))))
            throw py::value_error("Contour cleanup requires objects in one DXFCAD drawing");
        auto found=std::find_if(liveObjects.begin(),liveObjects.end(),[&](const auto& candidate) { return candidate.equal(object); });
        if (found==liveObjects.end()) throw py::value_error("Select native DXF contours");
        selection.push_back(static_cast<std::size_t>(found-liveObjects.begin()));
        names_.push_back(object.attr("Name").cast<std::string>());
        selectedSnapshot_.emplace_back(object.attr("DXFSourceIndex").cast<int>(),object.attr("DXFData").cast<std::string>());
    }
    domain_=std::make_unique<ContourChangeSet>(source,live,selection,results,preserveFirst);
    // Resolve every target group before any mutation, including legacy ambiguity.
    for (const auto& raw : domain_->records()) findLayerGroup(root_,parseEntity(raw)->layer());
}

py::object ContourObjectEditPlan::document() const { return document_; }

py::object ContourObjectEditPlan::root() const { return root_; }

py::object ContourObjectEditPlan::templateObject() const { return selected_.front(); }

bool ContourObjectEditPlan::preservesFirst() const { return domain_->preservesFirst(); }

const std::vector<py::object>& ContourObjectEditPlan::createdObjects() const { return created_; }

py::list ContourObjectEditPlan::previewRecords() const {
    py::list records;
    for (const auto& raw : domain_->records()) records.append(py::make_tuple(selected_.front(),raw));
    return records;
}

py::bytes ContourObjectEditPlan::drawingBytes() const { return py::bytes(domain_->drawingBytes()); }

std::vector<py::object> ContourObjectEditPlan::apply()
{
    if (applied_) throw py::value_error("Contour edit plan has already been applied");
    for (std::size_t i=0;i<selected_.size();++i) {
        py::object current=document_.attr("getObject")(names_[i]);
        if (current.is_none() || !current.equal(selected_[i])
            || !root_.equal(py::object(current.attr("DXFSourceRoot")))
            || current.attr("DXFSourceIndex").cast<int>()!=selectedSnapshot_[i].first
            || current.attr("DXFData").cast<std::string>()!=selectedSnapshot_[i].second)
            throw py::value_error("Selected contours changed; rebuild the contour edit plan");
    }
    if (sourceSnapshot_!=root_.attr("DXFSourceData").cast<std::string>() || liveSnapshot_!=currentDrawingBytes(root_))
        throw py::value_error("Drawing changed; rebuild the contour edit plan");
    // Group membership can change without changing DXF bytes.
    std::map<std::string,py::object> groups;
    for (const auto& raw : domain_->records()) {
        const auto layer=parseEntity(raw)->layer();
        groups.emplace(layer,findLayerGroup(root_,layer));
    }
    applied_=true; // One mutation attempt; callers roll back failures in their transaction.
    for (auto& [layer,group] : groups) group=materializeLayerGroup(root_,layer,group);
    std::vector<py::object> result;
    for (std::size_t i=0;i<domain_->records().size();++i) {
        const auto& raw=domain_->records()[i];
        const auto entity=parseEntity(raw);
        py::object object;
        if (i==0 && domain_->preservesFirst()) {
            object=selected_.front();
            object.attr("DXFData")=raw;
        } else {
            object=createOwnedEntity(root_,entity,-1,"",groups.at(entity->layer()));
            created_.push_back(object);
        }
        auto target=groups.at(entity->layer());
        py::list owned=root_.attr("Group"), parents=object.attr("InList");
        for (auto item : parents) {
            auto group=py::reinterpret_borrow<py::object>(item);
            if (owned.contains(group) && !group.equal(target) && py::hasattr(group,"Group"))
                group.attr("removeObject")(object);
        }
        if (!py::list(target.attr("Group")).contains(object)) target.attr("addObject")(object);
        result.push_back(object);
    }
    for (std::size_t i=domain_->preservesFirst() ? 1 : 0;i<names_.size();++i)
        document_.attr("removeObject")(names_[i]);
    return result;
}

} // namespace DXFCAD::FreeCAD
