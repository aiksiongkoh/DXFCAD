#pragma once

#include "AdapterTypes.h"
#include <DXFCAD/Contour.h>
#include <DXFCAD/EditPlan.h>

namespace DXFCAD::FreeCAD {

class ContourObjectEditPlan : public EditPlan {
    py::object document_,root_;
    std::vector<py::object> selected_,created_;
    std::vector<std::string> names_;
    std::vector<std::pair<int,std::string>> selectedSnapshot_;
    std::string sourceSnapshot_,liveSnapshot_;
    std::unique_ptr<ContourChangeSet> domain_;
    bool applied_=false;
public:
    ContourObjectEditPlan(const std::vector<py::object>& objects,const std::vector<std::string>& results,bool preserveFirst);

    py::object document() const;
    py::object root() const;
    py::object templateObject() const;
    bool preservesFirst() const;
    const std::vector<py::object>& createdObjects() const;
    py::list previewRecords() const;
    py::bytes drawingBytes() const;

    std::vector<py::object> apply();
};

} // namespace DXFCAD::FreeCAD
