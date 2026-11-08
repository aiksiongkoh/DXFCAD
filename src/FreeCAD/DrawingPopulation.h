#pragma once

#include "AdapterTypes.h"
#include <DXFCAD/Drawing.h>

namespace DXFCAD::FreeCAD {

// Python supplies lifecycle adapters and presentation in the same transaction.
class DrawingPopulation {
public:
    Drawing source;
    py::object root;
    std::vector<py::object> entities;
    std::map<std::string,py::object> layerGroups;

    DrawingPopulation(py::object document,const Drawing& drawing,const std::string& sourceFile,
                      const std::string& label,const std::string& name,const std::string& font,
                      py::object progress = py::none());
};

} // namespace DXFCAD::FreeCAD
