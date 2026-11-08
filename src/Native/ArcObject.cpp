#include <Python.h>
#include <DXFCAD/Native/ArcObject.h>

namespace DXFCAD {
PROPERTY_SOURCE(DXFCAD::ArcObject, DXFCAD::EntityObject)
ArcObject::ArcObject()
    : EntityObject("ARC", "0\nARC\n10\n0\n20\n0\n40\n1\n50\n0\n51\n90\n")
{}
}
