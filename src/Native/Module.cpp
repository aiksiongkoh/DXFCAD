#include <pybind11/pybind11.h>
#include <DXFCAD/LWPolylineObject.h>
#include <DXFCAD/LineObject.h>
#include <DXFCAD/ArcObject.h>
#include <DXFCAD/CircleObject.h>
#include <DXFCAD/TextObject.h>

PYBIND11_MODULE(_DXFCADObjects, module)
{
    if (DXFCAD::LWPolylineObject::getClassTypeId().isBad()) DXFCAD::LWPolylineObject::init();
    if (DXFCAD::DXFEntityObject::getClassTypeId().isBad()) DXFCAD::DXFEntityObject::init();
    if (DXFCAD::LineObject::getClassTypeId().isBad()) DXFCAD::LineObject::init();
    if (DXFCAD::ArcObject::getClassTypeId().isBad()) DXFCAD::ArcObject::init();
    if (DXFCAD::CircleObject::getClassTypeId().isBad()) DXFCAD::CircleObject::init();
    if (DXFCAD::TextObject::getClassTypeId().isBad()) DXFCAD::TextObject::init();
    module.doc() = "FreeCAD-native DXFCAD document object registration.";
}
