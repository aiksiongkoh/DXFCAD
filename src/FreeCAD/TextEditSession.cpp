#include "TextEditSession.h"
#include "DocumentOperations.h"
#include <DXFCAD/Drafting.h>

#include <algorithm>
#include <limits>
#include <set>

namespace DXFCAD::FreeCAD {

TextEditSession::TextEditSession(py::object object) : snapshot_(object) {
    Drafting::textInfo(snapshot_.originalData());
}

py::object TextEditSession::object() const { return snapshot_.object(); }

const std::string& TextEditSession::originalData() const { return snapshot_.originalData(); }

py::tuple TextEditSession::textInfo() const {
    const auto info=Drafting::textInfo(originalData());
    return py::make_tuple(info.text,info.height,info.referenceWidth);
}

std::unique_ptr<EntityUpdatePlan> TextEditSession::prepare(const std::string& content,double height,double width) const {
    snapshot_.validateCurrent();
    auto raw=Drafting::editText(originalData(),content,height,width);
    return std::make_unique<EntityUpdatePlan>(std::vector<py::object>{object()},std::vector<std::string>{raw});
}

} // namespace DXFCAD::FreeCAD
