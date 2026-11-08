#include <Python.h>
#include <DXFCAD/Native/LineObject.h>

namespace DXFCAD {
PROPERTY_SOURCE(DXFCAD::LineObject, DXFCAD::EntityObject)
LineObject::LineObject()
    : EntityObject("LINE", "0\nLINE\n10\n0\n20\n0\n11\n1\n21\n0\n")
{}
}
