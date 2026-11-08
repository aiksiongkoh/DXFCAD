#pragma once

#include "AdapterTypes.h"

namespace DXFCAD::FreeCAD {

// Shared read-only opening snapshot for native entity editors.
class EntityEditSnapshot {
    py::object object_,document_,root_;
    py::list snapshot_;
    std::string original_,name_,source_;
    py::list state() const;
public:
    explicit EntityEditSnapshot(py::object object);
    py::object object() const;
    const std::string& originalData() const;
    void validateCurrent() const;
};

} // namespace DXFCAD::FreeCAD
