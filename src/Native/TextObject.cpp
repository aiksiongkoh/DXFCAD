#include <Python.h>
#include <DXFCAD/TextObject.h>
#include <DXFCAD/DXFDocument.h>
#include <Mod/Part/App/Geometry.h>
#include <BRepBuilderAPI_GTransform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_GTrsf.hxx>
#include <filesystem>
#include <stdexcept>

namespace DXFCAD {
PROPERTY_SOURCE(DXFCAD::TextObject, DXFCAD::DXFEntityObject)

TextObject::TextObject()
    : DXFEntityObject("TEXT", "0\nTEXT\n10\n0\n20\n0\n40\n1\n1\n\n")
{
    ADD_PROPERTY_TYPE(TextFont, (""), "Display", App::Prop_None,
                      "Substitute font file used to derive text outlines");
}

short TextObject::mustExecute() const
{
    return TextFont.isTouched() ? 1 : DXFEntityObject::mustExecute();
}

TopoDS_Shape TextObject::buildShape(const DXFEntity& entity) const
{
    const auto geometry = entity.geometry();
    if (!geometry.text) throw std::runtime_error("Expected DXF text geometry");
    const auto& t = *geometry.text;
    if (t.text.empty()) return {};
    std::string font = TextFont.getValue();
    if (!std::filesystem::is_regular_file(std::filesystem::u8path(font)))
        throw std::runtime_error("Select an existing font file for TEXT display");
    std::string text = t.text;
    const auto wires = Part::makeTextWires(text, font, t.height);
    if (wires.empty()) {
        if (text.find_first_not_of(" \t\r\n") == std::string::npos) return {};
        throw std::runtime_error("Cannot build TEXT outlines from the selected font");
    }
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    for (const auto& wire : wires) builder.Add(compound, wire);
    // Scale in local text X, then orient and translate using C++ DXF OCS axes.
    gp_GTrsf transform;
    transform.SetVectorialPart(gp_Mat(
        t.xAxis.x*t.widthFactor, t.yAxis.x, t.normal.x,
        t.xAxis.y*t.widthFactor, t.yAxis.y, t.normal.y,
        t.xAxis.z*t.widthFactor, t.yAxis.z, t.normal.z));
    transform.SetTranslationPart(gp_XYZ(t.origin.x, t.origin.y, t.origin.z));
    BRepBuilderAPI_GTransform operation(compound, transform, true);
    if (!operation.IsDone() || !BRepCheck_Analyzer(operation.Shape()).IsValid())
        throw std::runtime_error("Cannot transform TEXT outlines");
    return operation.Shape();
}
}
