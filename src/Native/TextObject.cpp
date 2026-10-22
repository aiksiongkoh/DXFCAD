#include <Python.h>
#include <DXFCAD/TextObject.h>
#include <DXFCAD/DXFDocument.h>
#include <Mod/Part/App/Geometry.h>
#include <BRepBuilderAPI_GTransform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_GTrsf.hxx>
#include <filesystem>
#include <map>
#include <stdexcept>

namespace DXFCAD {
PROPERTY_SOURCE(DXFCAD::TextObject, DXFCAD::DXFEntityObject)
PROPERTY_SOURCE(DXFCAD::MTextObject, DXFCAD::TextObject)
PROPERTY_SOURCE(DXFCAD::DimensionObject, DXFCAD::TextObject)
PROPERTY_SOURCE(DXFCAD::InsertObject, DXFCAD::TextObject)

InsertObject::InsertObject() : TextObject("INSERT", "0\nINSERT\n2\n\n10\n0\n20\n0\n")
{
    ADD_PROPERTY_TYPE(DXFContext, (""), "DXF", App::Prop_Hidden,
                      "Referenced native block definitions for source-free display");
}

short InsertObject::mustExecute() const
{
    return DXFContext.isTouched() ? 1 : TextObject::mustExecute();
}

TextObject::TextObject()
    : TextObject("TEXT", "0\nTEXT\n10\n0\n20\n0\n40\n1\n1\n\n")
{}

MTextObject::MTextObject()
    : TextObject("MTEXT", "0\nMTEXT\n10\n0\n20\n0\n40\n1\n1\n\n")
{}

DimensionObject::DimensionObject()
    : TextObject("DIMENSION", "0\nDIMENSION\n70\n32\n")
{
    ADD_PROPERTY_TYPE(DXFContext, (""), "DXF", App::Prop_Hidden,
                      "Native picture block retained for source-free display");
}

short DimensionObject::mustExecute() const
{
    return DXFContext.isTouched() ? 1 : TextObject::mustExecute();
}

TextObject::TextObject(const char* type, const char* initialData)
    : DXFEntityObject(type, initialData)
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
    BRep_Builder builder;
    TopoDS_Compound result;
    builder.MakeCompound(result);
    bool hasShape = false;
    for (const auto& part : entity.blockParts()) {
        const auto source=TextObject::buildShape(*part.entity);
        if (source.IsNull()) continue;
        gp_GTrsf transform;
        transform.SetVectorialPart(gp_Mat(part.xAxis.x,part.yAxis.x,part.zAxis.x,
            part.xAxis.y,part.yAxis.y,part.zAxis.y,part.xAxis.z,part.yAxis.z,part.zAxis.z));
        transform.SetTranslationPart(gp_XYZ(part.origin.x,part.origin.y,part.origin.z));
        BRepBuilderAPI_GTransform operation(source,transform,true);
        if (!operation.IsDone() || !BRepCheck_Analyzer(operation.Shape()).IsValid())
            throw std::runtime_error("Cannot transform BLOCK geometry");
        builder.Add(result,operation.Shape()); hasShape=true;
    }
    if (!geometry.segments.empty() || !geometry.circles.empty() || !geometry.faces.empty()) {
        const auto primitives = DXFEntityObject::buildShape(entity);
        if (!primitives.IsNull()) { builder.Add(result, primitives); hasShape = true; }
    }
    auto texts = geometry.texts;
    if (geometry.text) texts.push_back(*geometry.text);
    for (const auto& t : texts) {
        if (t.text.empty()) continue;
        std::string font = TextFont.getValue();
        if (!std::filesystem::is_regular_file(std::filesystem::u8path(font)))
            throw std::runtime_error("Select an existing font file for text display");
        std::map<std::string, std::pair<TopoDS_Shape, TextBounds>> cache;
        auto measure = [&](const std::string& value) {
            if (cache.count(value)) return cache.at(value).second;
            if (value.empty()) { cache[value] = {}; return TextBounds{}; }
            std::string text = value;
            const auto wires = Part::makeTextWires(text, font, t.height);
            if (wires.empty()) {
                if (text.find_first_not_of(" \t\r\n") != std::string::npos)
                    throw std::runtime_error("Cannot build text outlines from the selected font");
                cache[value] = {};
                return TextBounds{};
            }
            TopoDS_Compound compound;
            builder.MakeCompound(compound);
            for (const auto& wire : wires) builder.Add(compound, wire);
            Bnd_Box bounds;
            BRepBndLib::AddOptimal(compound, bounds);
            TextBounds b;
            double minZ, maxZ;
            bounds.Get(b.minX, b.minY, minZ, b.maxX, b.maxY, maxZ);
            cache[value] = {compound, b};
            return b;
        };
        for (const auto& run : t.layout(measure)) {
            const auto b = measure(run.text);
            const auto compound = cache.at(run.text).first;
            if (compound.IsNull()) continue;
            const auto origin = run.alignedOrigin(b.minX, b.minY, b.maxX, b.maxY);
            // Scale in local text X, then orient and translate in the text plane.
            gp_GTrsf transform;
            transform.SetVectorialPart(gp_Mat(
                run.xAxis.x*run.widthFactor, run.yAxis.x, run.normal.x,
                run.xAxis.y*run.widthFactor, run.yAxis.y, run.normal.y,
                run.xAxis.z*run.widthFactor, run.yAxis.z, run.normal.z));
            transform.SetTranslationPart(gp_XYZ(origin.x, origin.y, origin.z));
            BRepBuilderAPI_GTransform operation(compound, transform, true);
            if (!operation.IsDone() || !BRepCheck_Analyzer(operation.Shape()).IsValid())
                throw std::runtime_error("Cannot transform text outlines");
            builder.Add(result, operation.Shape());
            hasShape = true;
        }
    }
    return hasShape ? result : TopoDS_Shape();
}
}
