#pragma once

#include "AdapterTypes.h"
#include <DXFCAD/DXFEntity.h>
#include <DXFCAD/Vector3D.h>

namespace DXFCAD::FreeCAD {

py::object vector(const Vector3D& p);
py::object shape(const DXFEntity& entity, const std::string& fontFile, bool showWidths);
std::vector<std::pair<py::object,std::vector<double>>> blockShapes(
    const DXFEntity& entity, py::object geometry, const std::string& fontFile);

} // namespace DXFCAD::FreeCAD
