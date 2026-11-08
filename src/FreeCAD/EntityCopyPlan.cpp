#include "EntityCopyPlan.h"
#include "DocumentOperations.h"

#include <algorithm>
#include <limits>
#include <set>

namespace DXFCAD::FreeCAD {

EntityCopyPlan::EntityCopyPlan(const std::vector<py::object>& objects,const std::vector<std::string>& records)
    : BatchObjectTransformPlan(objects,"copy",Vector3D{},0,Vector3D{},true,&records) {}

} // namespace DXFCAD::FreeCAD
