#include "EntityEditSnapshot.h"
#include "DocumentOperations.h"

#include <algorithm>
#include <limits>
#include <set>

namespace DXFCAD::FreeCAD {

py::list EntityEditSnapshot::state() const {
    py::list result;
    for (auto handle : document_.attr("Objects")) {
        auto object=py::reinterpret_borrow<py::object>(handle);
        if (root_.is_none() ? !object.equal(object_) :
            !root_.equal(py::getattr(object,"DXFSourceRoot",py::none()))) continue;
        if (!py::hasattr(object,"DXFData")) continue;
        result.append(py::make_tuple(object,object.attr("DXFData"),
            py::getattr(object,"DXFSourceIndex",py::int_(-1)),
            py::getattr(object,"DXFContext",py::str("")),
            py::getattr(object,"TextFont",py::str("")),object.attr("Placement").attr("toMatrix")().attr("A")));
    }
    return result;
}

EntityEditSnapshot::EntityEditSnapshot(py::object object) : object_(object)
{
    if (!py::hasattr(object,"DXFData")) throw py::value_error("Select one native DXF entity");
    original_=object.attr("DXFData").cast<std::string>();
    if (!object.attr("Placement").attr("isIdentity")().cast<bool>())
        throw py::value_error("Reset FreeCAD Placement before editing a native DXF entity");
    document_=object.attr("Document");
    name_=object.attr("Name").cast<std::string>();
    root_=py::getattr(object,"DXFSourceRoot",py::none());
    if (!root_.is_none()) {
        drawingSource(root_);
        source_=root_.attr("DXFSourceData").cast<std::string>();
    }
    // Opening/previewing is read-only; export eligibility is checked by prepare().
    snapshot_=state();
}

py::object EntityEditSnapshot::object() const { return object_; }

const std::string& EntityEditSnapshot::originalData() const { return original_; }

void EntityEditSnapshot::validateCurrent() const
{
    auto current=document_.attr("getObject")(name_);
    if (current.is_none() || !py::object(current).equal(object_)
        || !root_.equal(py::getattr(object_,"DXFSourceRoot",py::none()))
        || (!root_.is_none() && source_!=root_.attr("DXFSourceData").cast<std::string>())
        || !snapshot_.equal(state()))
        throw py::value_error("Entity or drawing changed; close and reopen the editor");
}

} // namespace DXFCAD::FreeCAD
