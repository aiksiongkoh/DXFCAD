#include "PolylineEditSession.h"
#include "DocumentOperations.h"

#include <algorithm>
#include <limits>
#include <set>

namespace DXFCAD::FreeCAD {

PolylineEditSession::PolylineEditSession(py::object object) : snapshot_(object) {
    readLWPolyline(snapshot_.originalData()).worldSegments();
}

py::object PolylineEditSession::object() const { return snapshot_.object(); }

const std::string& PolylineEditSession::originalData() const { return snapshot_.originalData(); }

DXFLWPolyline PolylineEditSession::polyline() const { return readLWPolyline(originalData()); }

std::unique_ptr<EntityUpdatePlan> PolylineEditSession::prepare(const DXFLWPolyline& polyline,const std::vector<int>& origins) const {
    snapshot_.validateCurrent();
    auto raw=updateLWPolyline(originalData(),polyline,origins);
    return std::make_unique<EntityUpdatePlan>(std::vector<py::object>{object()},std::vector<std::string>{raw});
}

} // namespace DXFCAD::FreeCAD
