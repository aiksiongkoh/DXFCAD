#include <Python.h>
#include <DXFCAD/DXFEntityObject.h>
#include <DXFCAD/DXFDocument.h>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Circ.hxx>
#include <stdexcept>
#include <set>

namespace DXFCAD {
PROPERTY_SOURCE_ABSTRACT(DXFCAD::DXFEntityObject, Part::Feature)

DXFEntityObject::DXFEntityObject(const char* type, const char* initialData)
{
    constexpr auto output = static_cast<App::PropertyType>(App::Prop_ReadOnly | App::Prop_Output);
    ADD_PROPERTY_TYPE(DXFData, (initialData), "DXF", App::Prop_Hidden,
                      "Authoritative native DXF entity group codes");
    ADD_PROPERTY_TYPE(DXFType, (type), "DXF", output, "DXF entity type");
    ADD_PROPERTY_TYPE(DXFLayer, ("0"), "DXF", output, "Native layer name");
    ADD_PROPERTY_TYPE(DXFHandle, (""), "DXF", output, "Native DXF handle");
    ADD_PROPERTY_TYPE(DisplayStatus, ("Pending recompute"), "Display", output, "Derived geometry status");
    ADD_PROPERTY_TYPE(DXFSourceRoot, (nullptr), "DXF", App::Prop_Hidden, "Owning DXF drawing");
    ADD_PROPERTY_TYPE(DXFSourceIndex, (-1), "DXF", App::Prop_Hidden, "Original source slot; -1 for a new entity");
}

short DXFEntityObject::mustExecute() const
{
    return DXFData.isTouched() ? 1 : Part::Feature::mustExecute();
}

TopoDS_Shape DXFEntityObject::buildShape(const DXFEntity& entity) const
{
    const auto geometry = entity.geometry();
    auto point = [](const Vector3D& p) { return gp_Pnt(p.x, p.y, p.z); };
    std::vector<TopoDS_Shape> shapes;
    std::set<std::size_t> covered;
    for (const auto& region : entity.widthRegions()) {
        BRepBuilderAPI_MakePolygon polygon;
        for (const auto& p : region.points) polygon.Add(point(p));
        polygon.Close();
        if (!polygon.IsDone()) throw std::runtime_error("Cannot build BLOCK polyline width boundary");
        BRepBuilderAPI_MakeFace face(polygon.Wire(),true);
        if (!face.IsDone()) throw std::runtime_error("Cannot build BLOCK polyline width face");
        shapes.push_back(face.Shape()); covered.insert(region.segmentIndex);
    }
    for (const auto& c : geometry.circles) {
        BRepBuilderAPI_MakeEdge edge(gp_Circ(gp_Ax2(point(c.center),
            gp_Dir(c.normal.x, c.normal.y, c.normal.z)), c.radius));
        if (!edge.IsDone()) throw std::runtime_error("Cannot build DXF circle edge");
        shapes.push_back(edge.Edge());
    }
    for (std::size_t i=0;i<geometry.segments.size();++i) {
        if (covered.count(i)) continue;
        const auto& segment=geometry.segments[i];
        if (segment.arc) {
            GC_MakeArcOfCircle arc(point(segment.start), point(segment.arc->midpoint), point(segment.end));
            if (!arc.IsDone()) throw std::runtime_error("Cannot build DXF arc");
            BRepBuilderAPI_MakeEdge edge(arc.Value());
            if (!edge.IsDone()) throw std::runtime_error("Cannot build DXF arc edge");
            shapes.push_back(edge.Edge());
        } else if (segment.start.x != segment.end.x || segment.start.y != segment.end.y
                   || segment.start.z != segment.end.z) {
            BRepBuilderAPI_MakeEdge edge(point(segment.start), point(segment.end));
            if (!edge.IsDone()) throw std::runtime_error("Cannot build DXF line edge");
            shapes.push_back(edge.Edge());
        }
    }
    for (const auto& face : geometry.faces) {
        BRepBuilderAPI_MakePolygon polygon;
        for (const auto& p : face) polygon.Add(point(p));
        polygon.Close();
        if (!polygon.IsDone()) throw std::runtime_error("Cannot build DIMENSION arrowhead boundary");
        BRepBuilderAPI_MakeFace operation(polygon.Wire(), true);
        if (!operation.IsDone()) throw std::runtime_error("Cannot build DIMENSION arrowhead face");
        shapes.push_back(operation.Shape());
    }
    if (shapes.empty()) return {};
    if (shapes.size() == 1) return shapes.front();
    BRep_Builder builder;
    TopoDS_Compound result;
    builder.MakeCompound(result);
    for (const auto& shape : shapes) builder.Add(result, shape);
    return result;
}

App::DocumentObjectExecReturn* DXFEntityObject::execute()
{
    try {
        const auto entity = parseEntity(DXFData.getValue(), entityContext());
        if (entity->type() != entityType())
            throw std::invalid_argument(std::string(entityType()) + " object requires matching DXF data");
        const auto result = buildShape(*entity);
        DXFLayer.setValue(entity->layer());
        DXFHandle.setValue(entity->handle());
        Shape.setValue(result);
        DisplayStatus.setValue(result.IsNull() ? "Empty" : shapeStatus());
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

void DXFEntityObject::onDocumentRestored()
{
    Part::Feature::onDocumentRestored();
    touch();
}

const char* DXFEntityObject::getViewProviderName() const
{
    return "PartGui::ViewProviderPython";
}
} // namespace DXFCAD
