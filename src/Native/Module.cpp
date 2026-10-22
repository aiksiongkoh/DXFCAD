#include <pybind11/pybind11.h>
#include <DXFCAD/LWPolylineObject.h>
#include <DXFCAD/LineObject.h>
#include <DXFCAD/ArcObject.h>
#include <DXFCAD/CircleObject.h>
#include <DXFCAD/TextObject.h>
#include <DXFCAD/DXFDocumentObject.h>
#include <App/DocumentObjectPy.h>

PYBIND11_MODULE(_DXFCADObjects, module)
{
    if (DXFCAD::DXFEntityObject::getClassTypeId().isBad()) DXFCAD::DXFEntityObject::init();
    if (DXFCAD::LWPolylineObject::getClassTypeId().isBad()) DXFCAD::LWPolylineObject::init();
    if (DXFCAD::LineObject::getClassTypeId().isBad()) DXFCAD::LineObject::init();
    if (DXFCAD::ArcObject::getClassTypeId().isBad()) DXFCAD::ArcObject::init();
    if (DXFCAD::CircleObject::getClassTypeId().isBad()) DXFCAD::CircleObject::init();
    if (DXFCAD::TextObject::getClassTypeId().isBad()) DXFCAD::TextObject::init();
    if (DXFCAD::MTextObject::getClassTypeId().isBad()) DXFCAD::MTextObject::init();
    if (DXFCAD::DimensionObject::getClassTypeId().isBad()) DXFCAD::DimensionObject::init();
    if (DXFCAD::InsertObject::getClassTypeId().isBad()) DXFCAD::InsertObject::init();
    if (DXFCAD::DXFDocumentObject::getClassTypeId().isBad()) DXFCAD::DXFDocumentObject::init();
    module.doc() = "FreeCAD-native DXFCAD document object registration.";
    auto drawingObject=[](pybind11::object object) -> DXFCAD::DXFDocumentObject& {
        if (!PyObject_TypeCheck(object.ptr(),&App::DocumentObjectPy::Type))
            throw pybind11::type_error("Expected a FreeCAD document object");
        auto* wrapper=static_cast<App::DocumentObjectPy*>(object.ptr());
        if (!static_cast<Base::PyObjectBase*>(wrapper)->isValid())
            throw pybind11::value_error("Drawing object has been deleted");
        auto* native=wrapper->getDocumentObjectPtr();
        auto* drawing=dynamic_cast<DXFCAD::DXFDocumentObject*>(native);
        if (!drawing) throw pybind11::type_error("Expected DXFCAD::DXFDocumentObject");
        return *drawing;
    };
    module.def("initialize_drawing",[drawingObject](pybind11::object object,pybind11::bytes bytes,const std::string& sourceFile) {
        drawingObject(object).setDrawing(DXFCAD::DXFDocument::parse(bytes.cast<std::string>()),sourceFile);
    });
    module.def("drawing_bytes",[drawingObject](pybind11::object object) {
        return pybind11::bytes(drawingObject(object).sourceBytes());
    });
    module.def("current_drawing_bytes",[drawingObject](pybind11::object object) {
        return pybind11::bytes(drawingObject(object).currentBytes());
    });
}
