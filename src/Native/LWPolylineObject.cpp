#include <Python.h>
#include <DXFCAD/LWPolylineObject.h>
#include <DXFCAD/DXFDocument.h>

#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRep_Builder.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <Mod/Part/App/TopoShape.h>

#include <set>
#include <stdexcept>

namespace DXFCAD {
namespace {
gp_Pnt point(const Vector3D& p) { return {p.x, p.y, p.z}; }

TopoDS_Shape buildShape(const DXFEntity& entity, bool showWidths)
{
    std::vector<TopoDS_Shape> children;
    std::set<std::size_t> covered;
    if (showWidths) {
        std::vector<TopoDS_Shape> faces;
        for (const auto& region : entity.widthRegions()) {
            BRepBuilderAPI_MakePolygon polygon;
            for (const auto& p : region.points) polygon.Add(point(p));
            polygon.Close();
            if (!polygon.IsDone()) throw std::runtime_error("Cannot build width boundary");
            BRepBuilderAPI_MakeFace face(polygon.Wire());
            if (!face.IsDone() || !BRepCheck_Analyzer(face.Face()).IsValid())
                throw std::runtime_error("Cannot build a valid width face");
            faces.push_back(face.Face());
            covered.insert(region.segmentIndex);
        }
        if (!faces.empty()) {
            TopoDS_Shape merged = faces.front();
            if (faces.size() > 1) {
                TopTools_ListOfShape arguments, tools;
                arguments.Append(merged);
                for (std::size_t i = 1; i < faces.size(); ++i) tools.Append(faces[i]);
                BRepAlgoAPI_Fuse operation;
                operation.SetArguments(arguments);
                operation.SetTools(tools);
                operation.Build();
                if (!operation.IsDone() || operation.HasErrors())
                    throw std::runtime_error("Cannot unite width faces");
                merged = operation.Shape();
                const auto refined = Part::TopoShape(merged).removeSplitter();
                if (BRepCheck_Analyzer(refined).IsValid()) merged = refined;
            }
            if (!BRepCheck_Analyzer(merged).IsValid()) throw std::runtime_error("Invalid united width faces");
            children.push_back(merged);
        }
    }
    const auto segments = entity.geometry().segments;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (covered.count(i)) continue;
        const auto& segment = segments[i];
        const auto start = point(segment.start), end = point(segment.end);
        if (segment.arc) {
            GC_MakeArcOfCircle arc(start, point(segment.arc->midpoint), end);
            if (!arc.IsDone()) throw std::runtime_error("Cannot build LWPolyline arc");
            BRepBuilderAPI_MakeEdge edge(arc.Value());
            if (!edge.IsDone()) throw std::runtime_error("Cannot build LWPolyline arc edge");
            children.push_back(edge.Edge());
        } else if (start.X() != end.X() || start.Y() != end.Y() || start.Z() != end.Z()) {
            BRepBuilderAPI_MakeEdge edge(start, end);
            if (!edge.IsDone()) throw std::runtime_error("Cannot build LWPolyline line edge");
            children.push_back(edge.Edge());
        }
    }
    if (children.empty()) return {};
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    for (const auto& child : children) builder.Add(compound, child);
    return compound;
}
}

PROPERTY_SOURCE(DXFCAD::LWPolylineObject, Part::Feature)

LWPolylineObject::LWPolylineObject()
{
    constexpr auto output = static_cast<App::PropertyType>(App::Prop_ReadOnly | App::Prop_Output);
    ADD_PROPERTY_TYPE(DXFData, ("0\nLWPOLYLINE\n90\n0\n70\n0\n"), "DXF", App::Prop_Hidden,
                      "Authoritative native DXF entity group codes");
    ADD_PROPERTY_TYPE(DXFType, ("LWPOLYLINE"), "DXF", output, "DXF entity type");
    ADD_PROPERTY_TYPE(DXFLayer, ("0"), "DXF", output, "Native layer name");
    ADD_PROPERTY_TYPE(DXFHandle, (""), "DXF", output, "Native DXF handle");
    ADD_PROPERTY_TYPE(DisplayStatus, ("Empty"), "Display", output, "Derived geometry status");
    ADD_PROPERTY_TYPE(ShowWidths, (true), "Display", App::Prop_None, "Display width regions instead of centerlines");
}

short LWPolylineObject::mustExecute() const
{
    return DXFData.isTouched() || ShowWidths.isTouched() ? 1 : Part::Feature::mustExecute();
}

App::DocumentObjectExecReturn* LWPolylineObject::execute()
{
    try {
        const auto entity = parseEntity(DXFData.getValue());
        if (entity->type() != "LWPOLYLINE") throw std::invalid_argument("LWPolylineObject requires LWPOLYLINE data");
        const auto result = buildShape(*entity, ShowWidths.getValue());
        DXFLayer.setValue(entity->layer());
        DXFHandle.setValue(entity->handle());
        Shape.setValue(result);
        DisplayStatus.setValue(ShowWidths.getValue() ? "Widths (curved boundaries approximated)" : "Centerline");
        return Part::Feature::execute();
    } catch (const Standard_Failure& error) {
        Shape.setValue(TopoDS_Shape());
        const std::string message = error.GetMessageString() ? error.GetMessageString() : "OpenCASCADE error";
        DisplayStatus.setValue("Error: " + message);
        return new App::DocumentObjectExecReturn(message, this);
    } catch (const std::exception& error) {
        Shape.setValue(TopoDS_Shape());
        DisplayStatus.setValue(std::string("Error: ") + error.what());
        return new App::DocumentObjectExecReturn(error.what(), this);
    }
}

void LWPolylineObject::onDocumentRestored()
{
    Part::Feature::onDocumentRestored();
    touch();
}

const char* LWPolylineObject::getViewProviderName() const
{
    // The document object is native; only its view provider uses Python.
    return "PartGui::ViewProviderPython";
}

} // namespace DXFCAD
