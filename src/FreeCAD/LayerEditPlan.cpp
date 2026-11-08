#include "LayerEditPlan.h"
#include "DocumentOperations.h"
#include <DXFCAD/Drafting.h>

#include <algorithm>
#include <limits>
#include <set>

namespace DXFCAD::FreeCAD {

LayerEditPlan::LayerEditPlan(const std::vector<py::object>& objects,const std::string& name,int color,
              bool visible,int layerColor,bool layerOn,bool editLayer,bool preserveVisibility) : layer(name)
{
    if (objects.empty()) throw py::value_error("Select DXFCAD objects to edit");
    document=objects.front().attr("Document");
    std::set<std::string> seen;
    for (auto object : objects) {
        if (!document.equal(py::object(object.attr("Document"))) || !py::hasattr(object,"DXFData"))
            throw py::value_error("Layer editing requires DXFCAD objects in one document");
        if (!seen.insert(object.attr("Name").cast<std::string>()).second) continue;
        if (!object.attr("Placement").attr("isIdentity")().cast<bool>())
            throw py::value_error("Reset FreeCAD Placement before editing native DXF properties");
        if (!py::hasattr(object,"DXFSourceRoot") || object.attr("DXFSourceRoot").is_none())
            throw py::value_error("Properties require objects in a DXFCAD drawing");
        py::object root=object.attr("DXFSourceRoot");
        if (!document.equal(py::object(root.attr("Document"))))
            throw py::value_error("Entity and drawing must belong to the same document");
        const auto key=root.attr("Name").cast<std::string>();
        if (!sources.count(key)) {
            roots.emplace(key,root);
            sources.emplace(key,layerSource(drawingSource(root),name,layerColor,layerOn,editLayer));
            groups.emplace(key,findLayerGroup(root,name));
        }
        const auto context=py::hasattr(object,"DXFContext") ? object.attr("DXFContext").cast<std::string>() : "";
        const auto raw=object.attr("DXFData").cast<std::string>();
        const bool on=preserveVisibility ? entityVisible(*parseEntity(raw,context)) : visible;
        records.emplace_back(object,root,Drafting::properties(raw,name,color,on,context));
    }
}

py::tuple LayerEditPlan::result() const { return py::make_tuple(document,roots,sources,records); }

void LayerEditPlan::apply()
{
    for (const auto& [key,root] : roots) {
        retainSource(root,sources.at(key));
        groups.at(key)=materializeLayerGroup(root,layer,groups.at(key));
    }
    for (const auto& [object,root,raw] : records) {
        auto target=groups.at(root.attr("Name").cast<std::string>());
        py::list parents=object.attr("InList");
        py::list owned=root.attr("Group");
        for (auto item : parents) {
            auto group=py::reinterpret_borrow<py::object>(item);
            if (owned.contains(group) && !group.equal(target) && py::hasattr(group,"Group"))
                group.attr("removeObject")(object);
        }
        if (!py::list(target.attr("Group")).contains(object)) target.attr("addObject")(object);
        object.attr("DXFData")=raw;
        if (object.attr("DXFType").cast<std::string>()=="INSERT")
            object.attr("DXFContext")=sources.at(root.attr("Name").cast<std::string>()).resolveEntity(raw)->context();
    }
}

} // namespace DXFCAD::FreeCAD
