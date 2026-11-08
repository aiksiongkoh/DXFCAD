#pragma once

#include "AdapterTypes.h"
#include "BatchObjectTransformPlan.h"

namespace DXFCAD::FreeCAD {

class EntityCopyPlan : public BatchObjectTransformPlan {
public:
    EntityCopyPlan(const std::vector<py::object>& objects,const std::vector<std::string>& records);
};

} // namespace DXFCAD::FreeCAD
