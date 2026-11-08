#pragma once

#include "AdapterTypes.h"
#include "EntityUpdatePlan.h"
#include "EntityEditSnapshot.h"

namespace DXFCAD::FreeCAD {

class TextEditSession {
    EntityEditSnapshot snapshot_;
public:
    explicit TextEditSession(py::object object);
    py::object object() const;
    const std::string& originalData() const;
    py::tuple textInfo() const;
    std::unique_ptr<EntityUpdatePlan> prepare(const std::string& content,double height,double width) const;
};

} // namespace DXFCAD::FreeCAD
