#pragma once

#include "AdapterTypes.h"
#include <DXFCAD/CornerEdit.h>
#include <DXFCAD/EditPlan.h>
#include "BatchObjectTransformPlan.h"

namespace DXFCAD::FreeCAD {

class CornerObjectEditPlan : public EditPlan {
    py::object document_,root_;
    std::vector<py::object> objects_;
    std::unique_ptr<BatchObjectTransformPlan> snapshot_;
    std::unique_ptr<CornerChangeSet> domain_;
    bool applied_=false;
public:
    CornerObjectEditPlan(const std::vector<py::object>& objects,const std::vector<std::string>& updates,
                         const std::string& connector,const std::string& connectorType);
    py::object document() const;
    py::object root() const;
    const std::vector<py::object>& objects() const;
    py::list previewRecords() const;
    py::bytes drawingBytes() const;
    py::object apply();
};

} // namespace DXFCAD::FreeCAD
