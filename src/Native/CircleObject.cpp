#include <Python.h>
#include <DXFCAD/CircleObject.h>

namespace DXFCAD {
PROPERTY_SOURCE(DXFCAD::CircleObject, DXFCAD::DXFEntityObject)
CircleObject::CircleObject()
    : DXFEntityObject("CIRCLE", "0\nCIRCLE\n10\n0\n20\n0\n40\n1\n")
{}
}
