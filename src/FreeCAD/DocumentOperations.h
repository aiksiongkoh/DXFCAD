#pragma once

#include "AdapterTypes.h"
#include <DXFCAD/DXFEntity.h>
#include <DXFCAD/Drawing.h>

namespace DXFCAD::FreeCAD {

void property(py::object object, const char* name, const py::object& value);
void ensureDisplayProperties(py::object object);
void setDrawingSource(py::object root,const Drawing& drawing,const std::string& sourceFile);
Drawing drawingSource(py::object root);
std::string currentDrawingBytes(py::object root);
py::object findLayerGroup(py::object root,const std::string& name);
py::object materializeLayerGroup(py::object root,const std::string& name,py::object group);
Drawing layerSource(const Drawing& source,const std::string& name,int color,bool visible,bool edit);
void retainSource(py::object root,const Drawing& source);
void setEntityOwnership(py::object object,py::object root,int index);
py::object createOwnedEntity(py::object root,const std::shared_ptr<DXFEntity>& entity,
                             int index,const std::string& font,py::object group);
py::object createEntity(py::object document,const std::string& raw,py::object root,const std::string& font);
py::object ensureLayer(py::object root,const std::string& name,int color,bool visible,bool edit);
py::object cornerRoot(const std::vector<py::object>& objects);

} // namespace DXFCAD::FreeCAD
