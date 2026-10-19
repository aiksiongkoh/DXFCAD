#include <Python.h>
#include <DXFCAD/ArcObject.h>

namespace DXFCAD {
PROPERTY_SOURCE(DXFCAD::ArcObject, DXFCAD::DXFEntityObject)
ArcObject::ArcObject()
    : DXFEntityObject("ARC", "0\nARC\n10\n0\n20\n0\n40\n1\n50\n0\n51\n90\n")
{}
}
