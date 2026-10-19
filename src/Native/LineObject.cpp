#include <Python.h>
#include <DXFCAD/LineObject.h>

namespace DXFCAD {
PROPERTY_SOURCE(DXFCAD::LineObject, DXFCAD::DXFEntityObject)
LineObject::LineObject()
    : DXFEntityObject("LINE", "0\nLINE\n10\n0\n20\n0\n11\n1\n21\n0\n")
{}
}
