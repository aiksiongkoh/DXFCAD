#pragma once

#include "AdapterTypes.h"
#include <DXFCAD/Drawing.h>
#include <DXFCAD/EditPlan.h>

namespace DXFCAD::FreeCAD {

class LayerEditPlan : public EditPlan {
public:
    py::object document=py::none();
    std::map<std::string,py::object> roots, groups;
    std::map<std::string,Drawing> sources;
    std::vector<std::tuple<py::object,py::object,std::string>> records;
    std::string layer;

    LayerEditPlan(const std::vector<py::object>& objects,const std::string& name,int color,
                  bool visible,int layerColor,bool layerOn,bool editLayer,bool preserveVisibility=false);

    py::tuple result() const;

    void apply();
};

} // namespace DXFCAD::FreeCAD
