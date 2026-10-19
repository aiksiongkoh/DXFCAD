#include <Python.h>
#include <DXFCAD/DXFEntityObject.h>
#include <DXFCAD/DXFDocument.h>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Circ.hxx>
#include <stdexcept>

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
}

short DXFEntityObject::mustExecute() const
{
    return DXFData.isTouched() ? 1 : Part::Feature::mustExecute();
}

TopoDS_Shape DXFEntityObject::buildShape(const DXFEntity& entity) const
{
    const auto geometry = entity.geometry();
    auto point = [](const Vector3D& p) { return gp_Pnt(p.x, p.y, p.z); };
    if (geometry.circles.size() == 1) {
        const auto& c = geometry.circles.front();
        BRepBuilderAPI_MakeEdge edge(gp_Circ(gp_Ax2(point(c.center),
            gp_Dir(c.normal.x, c.normal.y, c.normal.z)), c.radius));
        if (!edge.IsDone()) throw std::runtime_error("Cannot build DXF circle edge");
        return edge.Edge();
    }
    if (geometry.segments.size() != 1) throw std::runtime_error("Expected one DXF segment");
    const auto& segment = geometry.segments.front();
    TopoDS_Shape result;
    if (segment.arc) {
        GC_MakeArcOfCircle arc(point(segment.start), point(segment.arc->midpoint), point(segment.end));
        if (!arc.IsDone()) throw std::runtime_error("Cannot build DXF arc");
        BRepBuilderAPI_MakeEdge edge(arc.Value());
        if (!edge.IsDone()) throw std::runtime_error("Cannot build DXF arc edge");
        result = edge.Edge();
    } else if (segment.start.x != segment.end.x || segment.start.y != segment.end.y
               || segment.start.z != segment.end.z) {
        BRepBuilderAPI_MakeEdge edge(point(segment.start), point(segment.end));
        if (!edge.IsDone()) throw std::runtime_error("Cannot build DXF line edge");
        result = edge.Edge();
    }
    return result;
}

App::DocumentObjectExecReturn* DXFEntityObject::execute()
{
    try {
        const auto entity = parseEntity(DXFData.getValue());
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
