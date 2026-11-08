#include <Python.h>
#include <DXFCAD/Native/CircleObject.h>

namespace DXFCAD {
PROPERTY_SOURCE(DXFCAD::CircleObject, DXFCAD::EntityObject)
CircleObject::CircleObject()
    : EntityObject("CIRCLE", "0\nCIRCLE\n10\n0\n20\n0\n40\n1\n")
{}
}
