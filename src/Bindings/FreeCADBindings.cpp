#include <DXFCAD/Drawing.h>
#include <DXFCAD/ParametricCurves.h>
#include <DXFCAD/Drafting.h>
#include <DXFCAD/DXFLeader.h>
#include <DXFCAD/SnapGeometry.h>
#include <DXFCAD/Contour.h>
#include <DXFCAD/BatchTransform.h>
#include <DXFCAD/CornerEdit.h>
#include <DXFCAD/EditPlan.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <filesystem>
#include <map>
#include <set>
#include <algorithm>
#include <memory>
#include <limits>

#include "../FreeCAD/DocumentOperations.h"
#include "../FreeCAD/ShapeGeometry.h"
#include "../FreeCAD/LayerEditPlan.h"
#include "../FreeCAD/ContourObjectEditPlan.h"
#include "../FreeCAD/BatchObjectTransformPlan.h"
#include "../FreeCAD/EntityCopyPlan.h"
#include "../FreeCAD/EntityUpdatePlan.h"
#include "../FreeCAD/ContourOperationPlan.h"
#include "../FreeCAD/TrimPlan.h"
#include "../FreeCAD/OffsetPlan.h"
#include "../FreeCAD/EntityEditSnapshot.h"
#include "../FreeCAD/PolylineEditSession.h"
#include "../FreeCAD/TextEditSession.h"
#include "../FreeCAD/CornerObjectEditPlan.h"
#include "../FreeCAD/DrawingPopulation.h"
#include "../FreeCAD/LayoutPreview.h"

namespace py = pybind11;
using namespace DXFCAD;
using namespace DXFCAD::FreeCAD;

void bindFreeCAD(py::module_& module)
{
#ifdef DXFCAD_NATIVE_OBJECTS
    py::module_::import("FreeCAD");
    py::module_::import("Part");
    // Load Part before the companion DLL, whose native superclass depends on
    // it. The main DXFCAD module itself has no FreeCAD C++ DLL dependencies.
    py::module_::import("_DXFCADObjects");
    module.attr("NATIVE_DOCUMENT_OBJECTS") = true;
#else
    module.attr("NATIVE_DOCUMENT_OBJECTS") = false;
#endif
    py::class_<DXFItem, std::shared_ptr<DXFItem>>(module, "DXFItem",
        "Abstract grouping of concepts modeled with the DXF Reference's meaning; not a DXF record type.");
    py::class_<EditPlan>(module, "EditPlan",
        "Abstract identity of a prepared document edit; concrete types define application and lifecycle rules.");
    py::class_<ViewFrame>(module, "ViewFrame")
        .def_readonly("center", &ViewFrame::center)
        .def_readonly("x_axis", &ViewFrame::xAxis)
        .def_readonly("y_axis", &ViewFrame::yAxis)
        .def_readonly("normal", &ViewFrame::normal)
        .def_readonly("height", &ViewFrame::height);
    py::class_<PaperRectangle>(module,"PaperRectangle")
        .def_readonly("min_x",&PaperRectangle::minX).def_readonly("min_y",&PaperRectangle::minY)
        .def_readonly("max_x",&PaperRectangle::maxX).def_readonly("max_y",&PaperRectangle::maxY);
    py::class_<DXFViewport, DXFItem, std::shared_ptr<DXFViewport>>(module, "DXFViewport")
        .def_property_readonly("layout", &DXFViewport::layout)
        .def_property_readonly("handle", &DXFViewport::handle)
        .def_property_readonly("raw", &DXFViewport::raw)
        .def_property_readonly("id", &DXFViewport::id)
        .def_property_readonly("paper_width", &DXFViewport::paperWidth)
        .def_property_readonly("paper_height", &DXFViewport::paperHeight)
        .def_property_readonly("view_height", &DXFViewport::viewHeight)
        .def_property_readonly("scale", &DXFViewport::scale)
        .def_property_readonly("frozen_layers", &DXFViewport::frozenLayers)
        .def("freezes_layer", &DXFViewport::freezesLayer)
        .def_property_readonly("navigation_issue", &DXFViewport::navigationIssue)
        .def_property_readonly("paper_bounds",&DXFViewport::paperBounds)
        .def("model_to_paper",&DXFViewport::modelToPaper)
        .def("model_view", &DXFViewport::modelView);
    py::class_<DXFLayout,DXFItem,std::shared_ptr<DXFLayout>>(module,"DXFLayout")
        .def_property_readonly("name",&DXFLayout::name)
        .def_property_readonly("raw",&DXFLayout::raw)
        .def_property_readonly("tab_order",&DXFLayout::tabOrder)
        .def_property_readonly("limits",&DXFLayout::limits)
        .def_property_readonly("entities",&DXFLayout::entities)
        .def_property_readonly("viewports",&DXFLayout::viewports);
    py::class_<LayoutPath>(module,"LayoutPath")
        .def_readonly("contours",&LayoutPath::contours)
        .def_readonly("color",&LayoutPath::color)
        .def_readonly("filled",&LayoutPath::filled);
    py::class_<LayoutPreview>(module,"LayoutPreview")
        .def_readonly("paths",&LayoutPreview::paths)
        .def_readonly("viewports",&LayoutPreview::viewports)
        .def_readonly("limits",&LayoutPreview::limits)
        .def_readonly("warnings",&LayoutPreview::warnings);
    module.def("preview_layout",[](const Drawing& drawing,const std::string& name,const std::string& font,double tolerance) {
        return LayoutPreview(drawing,name,font,tolerance);
    },py::arg("drawing"),py::arg("layout"),py::arg("font_file")="",py::arg("tolerance")=0.02,
      "Read-only sheet preview in native paper coordinates; curves are sampled at the given paper-space deflection.");
    py::class_<DXFLayer, DXFItem, std::shared_ptr<DXFLayer>>(module, "DXFLayer")
        .def_readonly("name", &DXFLayer::name).def_readonly("visible", &DXFLayer::visible)
        .def_readonly("color", &DXFLayer::color);
    py::class_<SnapPoint>(module, "SnapPoint")
        .def_readonly("position", &SnapPoint::position)
        .def_readonly("kind", &SnapPoint::kind);
    py::class_<BlockPart>(module,"BlockPart")
        .def_readonly("entity",&BlockPart::entity)
        .def_readonly("origin",&BlockPart::origin)
        .def_readonly("x_axis",&BlockPart::xAxis)
        .def_readonly("y_axis",&BlockPart::yAxis)
        .def_readonly("z_axis",&BlockPart::zAxis)
        .def_readonly("layer",&BlockPart::layer)
        .def_readonly("color",&BlockPart::color)
        .def_readonly("visible",&BlockPart::visible)
        .def("transform_point",&BlockPart::transformPoint);
    py::class_<DXFEntity, DXFItem, std::shared_ptr<DXFEntity>>(module, "DXFEntity")
        .def("snap_points", &DXFEntity::snapPoints)
        .def("block_parts", &DXFEntity::blockParts)
        .def("translated", &DXFEntity::translated, py::arg("offset"))
        .def("copied", &DXFEntity::copied, py::arg("offset"))
        .def("rotated", &DXFEntity::rotated, py::arg("degrees"), py::arg("pivot"))
        .def("scaled", &DXFEntity::scaled, py::arg("factor"), py::arg("pivot"))
        .def("mirrored", &DXFEntity::mirrored, py::arg("line_degrees"), py::arg("line_point"))
        .def_property_readonly("type", &DXFEntity::type)
        .def_property_readonly("layer", &DXFEntity::layer)
        .def_property_readonly("handle", &DXFEntity::handle)
        .def_property_readonly("raw", &DXFEntity::raw)
        .def_property_readonly("context", &DXFEntity::context)
        .def_property_readonly("visible", &entityVisible)
        .def("color", &entityColor)
        .def("width_regions", &DXFEntity::widthRegions, py::arg("tolerance") = 0.0001);
    py::class_<DXFLeader,DXFEntity,std::shared_ptr<DXFLeader>>(module,"DXFLeader")
        .def("vertices",&DXFLeader::vertices);
    py::class_<Drawing>(module, "Drawing")
        .def_static("read", &Drawing::read)
        .def_static("parse", &Drawing::parse)
        .def_static("empty", &Drawing::empty, py::arg("insertion_units") = 0)
        .def("resolve_entity", &Drawing::resolveEntity, py::arg("raw"))
        .def("serialize", &Drawing::serialize, py::arg("replacements") = std::map<std::size_t, std::string>{},
             py::arg("removed") = std::vector<std::size_t>{}, py::arg("added") = std::vector<std::string>{})
        .def("serialize_bytes", [](const Drawing& doc) { return py::bytes(doc.serialize()); })
        .def("write", &Drawing::write, py::arg("path"),
             py::arg("replacements") = std::map<std::size_t, std::string>{},
             py::arg("removed") = std::vector<std::size_t>{}, py::arg("added") = std::vector<std::string>{})
        .def_readonly("entities", &Drawing::entities)
        .def_readonly("layers", &Drawing::layers)
        .def_readonly("warnings", &Drawing::warnings)
        .def_readonly("viewports", &Drawing::viewports)
        .def_readonly("layouts", &Drawing::layouts)
        .def_readonly("insertion_units", &Drawing::insertionUnits);
    module.def("create_drawing",[](py::object document,const Drawing& drawing,
                                    const std::string& sourceFile,const std::string& label,const std::string& name) {
        drawing.serialize(); // Reject uninitialized models before document mutation.
#ifdef DXFCAD_NATIVE_OBJECTS
        auto root=document.attr("addObject")("DXFCAD::DrawingObject",name);
#else
        auto root=document.attr("addObject")("App::DocumentObjectGroup",name);
#endif
        try {
            root.attr("Label")=label;
            setDrawingSource(root,drawing,sourceFile);
            return root;
        } catch (...) {
            document.attr("removeObject")(root.attr("Name"));
            throw;
        }
    },py::arg("document"),py::arg("drawing"),py::arg("source_file")="",
      py::arg("label")="DXF Drawing",py::arg("name")="DXFDrawing");
    py::class_<DrawingPopulation>(module,"DrawingPopulation")
        .def_readonly("root",&DrawingPopulation::root)
        .def_readonly("entities",&DrawingPopulation::entities)
        .def_readonly("layer_groups",&DrawingPopulation::layerGroups)
        .def_readonly("source",&DrawingPopulation::source)
        .def_property_readonly("warnings",[](const DrawingPopulation& result) { return result.source.warnings; });
    module.def("populate_drawing",[](py::object document,const Drawing& drawing,
        const std::string& sourceFile,const std::string& label,const std::string& name,const std::string& font,
        py::object progress) {
        return std::make_unique<DrawingPopulation>(document,drawing,sourceFile,label,name,font,progress);
    },py::arg("document"),py::arg("drawing"),py::arg("source_file")="",py::arg("label")="DXF Drawing",
       py::arg("name")="DXFDrawing",py::arg("font_file")="",py::arg("progress")=py::none(),
       "Create the drawing root, referenced layer groups and owned entities. Optional progress(completed, total) observes entity creation synchronously. Use a transaction; Python initializes presentation/compatibility proxies and recomputes before committing.");
    module.def("create_entity",&createEntity,py::arg("document"),py::arg("raw"),py::arg("root")=py::none(),
               py::arg("font_file")="",
               "Validate and create a new owned entity. Select the sole drawing or create an empty mm drawing when root is omitted. Use a transaction; Python initializes presentation and recomputes.");
    module.def("add_entity",[](py::object root,const std::string& raw,const std::string& font) {
        return createEntity(root.attr("Document"),raw,root,font);
    },py::arg("root"),py::arg("raw"),py::arg("font_file")="",
       "Add an identity-free model-space record to an explicit drawing, ensuring its layer and membership. Use a transaction.");
    module.def("drawing_document",&drawingSource,py::arg("root"),
               "Read the retained source snapshot; live entity edits are overlaid during export.");
    module.def("current_drawing_bytes",[](py::object root) { return py::bytes(currentDrawingBytes(root)); },
               py::arg("root"),"Assemble validated DXF bytes from the source snapshot and live entities.");
    module.def("current_drawing",[](py::object root) { return Drawing::parse(currentDrawingBytes(root)); },
               py::arg("root"),"Read the current drawing including edits, additions and deletions.");
    module.def("attach_entity",[](py::object object,py::object root,int index) {
        const auto source=drawingSource(root);
        if (!py::hasattr(object,"DXFData") || !object.attr("Document").equal(root.attr("Document")))
            throw py::value_error("Entity and drawing must belong to the same document");
        if (index < -1 || (index >= 0 && static_cast<std::size_t>(index) >= source.entities.size()))
            throw py::value_error("Invalid source entity index");
        if (py::hasattr(object,"DXFSourceRoot") && !object.attr("DXFSourceRoot").is_none()
            && !root.equal(py::object(object.attr("DXFSourceRoot"))))
            throw py::value_error("Entity already belongs to another DXF drawing");
        for (auto item : root.attr("Document").attr("Objects")) {
            auto other=py::reinterpret_borrow<py::object>(item);
            if (index >= 0 && !other.equal(object) && py::hasattr(other,"DXFSourceRoot")
                && root.equal(py::object(other.attr("DXFSourceRoot"))) && py::hasattr(other,"DXFSourceIndex")
                && other.attr("DXFSourceIndex").cast<int>() == index)
                throw py::value_error("Drawing has duplicate source entity indices");
        }
        setEntityOwnership(object,root,index);
    },py::arg("entity"),py::arg("root"),py::arg("source_index")=-1);
    module.def("set_drawing_source",[](py::object root,const Drawing& drawing) {
        if (!py::hasattr(root,"DXFSourceData")) throw py::value_error("Select a DXFCAD drawing");
        const auto path=py::hasattr(root,"SourceFile") ? root.attr("SourceFile").cast<std::string>() : "";
        setDrawingSource(root,drawing,path);
    },py::arg("root"),py::arg("drawing"),"Update retained drawing metadata/snapshot without changing child objects.");
    module.def("ensure_layer",[](py::object root,const std::string& name,int color,bool visible) {
        return ensureLayer(root,name,color,visible,false);
    },py::arg("root"),py::arg("name"),py::arg("color")=7,py::arg("visible")=true,
       "Ensure a layer and its persistent group; preserve existing layer properties. Use a document transaction.");
    module.def("source_layer_group",[](py::object root,const std::string& name) {
        const auto source=drawingSource(root);
        bool referenced=source.layers.count(name)!=0;
        for (const auto& entity : source.entities) referenced=referenced || entity->layer()==name;
        if (!referenced) throw py::value_error("Layer is not present or referenced in the source drawing");
        return materializeLayerGroup(root,name,findLayerGroup(root,name));
    },py::arg("root"),py::arg("name"),
       "Ensure a group for a source layer without changing retained DXF records, including undeclared imported layers.");
    module.def("set_layer_properties",[](py::object root,const std::string& name,int color,bool visible) {
        return ensureLayer(root,name,color,visible,true);
    },py::arg("root"),py::arg("name"),py::arg("color"),py::arg("visible"),
       "Update native layer properties and ensure its group. Presentation is managed by Python.");
    module.def("assign_layer",[](const std::vector<py::object>& objects,const std::string& name) {
        LayerEditPlan plan(objects,name,-1,true,7,true,false,true);
        plan.apply();
    },py::arg("entities"),py::arg("name"),"Assign native records and group membership, preserving entity color/visibility. Use a transaction.");
    module.def("prepare_entity_properties",[](const std::vector<py::object>& objects,const std::string& name,
        int color,bool visible,int layerColor,bool layerOn,bool editLayer) {
        return LayerEditPlan(objects,name,color,visible,layerColor,layerOn,editLayer).result();
    },py::arg("entities"),py::arg("layer"),py::arg("color"),py::arg("visible"),
       py::arg("layer_color")=7,py::arg("layer_on")=true,py::arg("edit_layer")=false,
       "Validate a complete property edit without changing the drawing; return preview records.");
    module.def("apply_entity_properties",[](const std::vector<py::object>& objects,const std::string& name,
        int color,bool visible,int layerColor,bool layerOn,bool editLayer) {
        LayerEditPlan plan(objects,name,color,visible,layerColor,layerOn,editLayer);
        plan.apply();
        return plan.result();
    },py::arg("entities"),py::arg("layer"),py::arg("color"),py::arg("visible"),
       py::arg("layer_color")=7,py::arg("layer_on")=true,py::arg("edit_layer")=false,
       "Validate and apply native properties, layer records and membership. Use a transaction and recompute.");
    py::class_<BatchObjectTransformPlan,EditPlan>(module,"BatchObjectTransformPlan")
        .def_property_readonly("document",&BatchObjectTransformPlan::document)
        .def_property_readonly("copies",&BatchObjectTransformPlan::copies)
        .def_property_readonly("objects",&BatchObjectTransformPlan::objects)
        .def_property_readonly("preview_records",&BatchObjectTransformPlan::previewRecords)
        .def_property_readonly("drawing_bytes",&BatchObjectTransformPlan::drawingBytes)
        .def("apply",&BatchObjectTransformPlan::apply,
             "Apply once in a transaction; Python initializes presentation/compatibility proxies and recomputes. Rejects stale plans.");
    module.def("prepare_batch_transform",[](const std::vector<py::object>& objects,const std::string& operation,
        py::object offset,double value,py::object pivot,bool allowIdentity) {
        return std::make_unique<BatchObjectTransformPlan>(objects,operation,
            offset.is_none() ? Vector3D{} : offset.cast<Vector3D>(),value,
            pivot.is_none() ? Vector3D{} : pivot.cast<Vector3D>(),allowIdentity);
    },py::arg("entities"),py::arg("operation")="move",py::arg("offset")=py::none(),
       py::arg("value")=0,py::arg("pivot")=py::none(),py::arg("allow_identity")=false,
       "Plan move/copy/rotate/scale/mirror for a complete selection without mutation. Zero move is allowed only for validation/previews when allow_identity=True.");
    py::class_<EntityCopyPlan,BatchObjectTransformPlan>(module,"EntityCopyPlan");
    module.def("prepare_entity_copy",[](const std::vector<py::object>& objects,const std::vector<std::string>& records) {
        return std::make_unique<EntityCopyPlan>(objects,records);
    },py::arg("entities"),py::arg("records"),
       "Plan related copies without mutation: strip native identity, validate complete drawings and reject stale state on apply. Retains type and layer.");
    py::class_<EntityUpdatePlan,EditPlan>(module,"EntityUpdatePlan")
        .def_property_readonly("document",&EntityUpdatePlan::document)
        .def_property_readonly("objects",&EntityUpdatePlan::objects)
        .def_property_readonly("preview_records",&EntityUpdatePlan::previewRecords)
        .def_property_readonly("drawing_bytes",&EntityUpdatePlan::drawingBytes)
        .def("apply",&EntityUpdatePlan::apply,
             "Apply once inside a transaction and recompute. Rejects stale plans before mutation.");
    module.def("prepare_entity_update",[](const std::vector<py::object>& objects,const std::vector<std::string>& records) {
        return std::make_unique<EntityUpdatePlan>(objects,records);
    },py::arg("entities"),py::arg("records"),
       "Validate geometry/content replacements and the complete resulting drawing without mutation. Retains identity, layer and style.");
    py::class_<TrimPlan,EditPlan>(module,"TrimPlan")
        .def_property_readonly("document",&TrimPlan::document)
        .def_property_readonly("target",&TrimPlan::target)
        .def_property_readonly("boundary",&TrimPlan::boundary)
        .def_property_readonly("objects",&TrimPlan::objects)
        .def_property_readonly("preview_records",&TrimPlan::previewRecords)
        .def_property_readonly("drawing_bytes",&TrimPlan::drawingBytes)
        .def("apply",&TrimPlan::apply,
             "Apply once inside a transaction and recompute. Rejects stale target and boundary dependencies before mutation.");
    module.def("prepare_trim",[](py::object target,py::object boundary,bool start,bool extend) {
        return std::make_unique<TrimPlan>(target,boundary,start,extend);
    },py::arg("target"),py::arg("boundary"),py::arg("start")=false,py::arg("extend")=false,
       "Plan native endpoint trimming/extending without mutation. Tracks both objects and their drawings; only the target is changed.");
    py::class_<OffsetPlan,EditPlan>(module,"OffsetPlan")
        .def_property_readonly("document",&OffsetPlan::document)
        .def_property_readonly("objects",&OffsetPlan::objects)
        .def_property_readonly("copies",&OffsetPlan::copies)
        .def_property_readonly("preview_records",&OffsetPlan::previewRecords)
        .def_property_readonly("drawing_bytes",&OffsetPlan::drawingBytes)
        .def("apply",&OffsetPlan::apply,
             "Apply once in a transaction; Python initializes copied-object presentation and recomputes. Rejects stale plans.");
    module.def("prepare_offset",[](const std::vector<py::object>& objects,double distance,bool keepOriginals) {
        return std::make_unique<OffsetPlan>(objects,distance,keepOriginals);
    },py::arg("entities"),py::arg("distance"),py::arg("keep_originals")=true,
       "Plan a complete selection's native offsets and copy/replacement semantics without mutation. Validates resulting drawings and deduplicates selection.");
    py::class_<TextEditSession>(module,"TextEditSession")
        .def_property_readonly("object",&TextEditSession::object)
        .def_property_readonly("original_data",&TextEditSession::originalData)
        .def_property_readonly("text_info",&TextEditSession::textInfo)
        .def("prepare",&TextEditSession::prepare,py::arg("content"),py::arg("height"),py::arg("width")=0,
             "Prepare a validated EntityUpdatePlan; reject changes since the text editor opened.");
    module.def("begin_text_edit",[](py::object object) {
        return std::make_unique<TextEditSession>(object);
    },py::arg("entity"),"Capture native TEXT/MTEXT and its drawing state without mutation.");
    module.def("prepare_polyline_fillet",[](py::object object,std::size_t vertex,double radius) {
        if (!py::hasattr(object,"DXFData")) throw py::value_error("Select one native LWPolyline");
        auto raw=Drafting::fillet(object.attr("DXFData").cast<std::string>(),vertex,radius);
        return std::make_unique<EntityUpdatePlan>(std::vector<py::object>{object},std::vector<std::string>{raw});
    },py::arg("target"),py::arg("vertex"),py::arg("radius"),
       "Prepare a validated LWPolyline fillet update without mutation. Preserves native metadata and rejects stale drawing state on apply.");
    module.def("prepare_polyline_chamfer",[](py::object object,std::size_t vertex,double incoming,double outgoing) {
        if (!py::hasattr(object,"DXFData")) throw py::value_error("Select one native LWPolyline");
        auto raw=Drafting::chamfer(object.attr("DXFData").cast<std::string>(),vertex,incoming,outgoing);
        return std::make_unique<EntityUpdatePlan>(std::vector<py::object>{object},std::vector<std::string>{raw});
    },py::arg("target"),py::arg("vertex"),py::arg("incoming"),py::arg("outgoing"),
       "Prepare a validated LWPolyline chamfer update without mutation. Preserves native metadata and rejects stale drawing state on apply.");
    module.def("prepare_text_edit",[](py::object object,const std::string& content,double height,double width) {
        return TextEditSession(object).prepare(content,height,width);
    },py::arg("entity"),py::arg("content"),py::arg("height"),py::arg("width")=0,
       "Prepare a validated TEXT/MTEXT update from current object state.");
    py::class_<PolylineEditSession>(module,"PolylineEditSession")
        .def_property_readonly("object",&PolylineEditSession::object)
        .def_property_readonly("original_data",&PolylineEditSession::originalData)
        .def_property_readonly("polyline",&PolylineEditSession::polyline)
        .def("prepare",&PolylineEditSession::prepare,py::arg("polyline"),py::arg("origins")=std::vector<int>{},
             "Prepare an EntityUpdatePlan preserving vertex provenance; reject changes since the editor opened.");
    module.def("begin_polyline_edit",[](py::object object) {
        return std::make_unique<PolylineEditSession>(object);
    },py::arg("entity"),"Capture the original LWPolyline and drawing state without mutation.");
    module.def("prepare_polyline_edit",[](py::object object,const DXFLWPolyline& polyline,const std::vector<int>& origins) {
        return PolylineEditSession(object).prepare(polyline,origins);
    },py::arg("entity"),py::arg("polyline"),py::arg("origins")=std::vector<int>{},
       "Prepare a validated LWPolyline replacement from the current object state.");
    py::class_<CornerObjectEditPlan,EditPlan>(module,"CornerObjectEditPlan")
        .def_property_readonly("document",&CornerObjectEditPlan::document)
        .def_property_readonly("root",&CornerObjectEditPlan::root)
        .def_property_readonly("objects",&CornerObjectEditPlan::objects)
        .def_property_readonly("preview_records",&CornerObjectEditPlan::previewRecords)
        .def_property_readonly("drawing_bytes",&CornerObjectEditPlan::drawingBytes)
        .def("apply",&CornerObjectEditPlan::apply,
             "Apply once inside a transaction; Python initializes connector presentation and recomputes. Rejects stale plans.");
    module.def("prepare_corner_edit",[](const std::vector<py::object>& objects,const std::vector<std::string>& updates,
        const std::string& connector,const std::string& type) {
        return std::make_unique<CornerObjectEditPlan>(objects,updates,connector,type);
    },py::arg("entities"),py::arg("updates"),py::arg("connector"),py::arg("connector_type"));
    module.def("prepare_line_fillet",[](const std::vector<py::object>& objects,double radius,bool keepFirst,bool keepSecond) {
        cornerRoot(objects);
        const auto result=Drafting::filletLines(objects[0].attr("DXFData").cast<std::string>(),
            objects[1].attr("DXFData").cast<std::string>(),radius,keepFirst,keepSecond);
        return std::make_unique<CornerObjectEditPlan>(objects,std::vector<std::string>{result.first,result.second},result.arc,"ARC");
    },py::arg("entities"),py::arg("radius"),py::arg("keep_first_start")=true,py::arg("keep_second_start")=false);
    module.def("prepare_line_chamfer",[](const std::vector<py::object>& objects,double first,double second,bool keepFirst,bool keepSecond) {
        cornerRoot(objects);
        const auto result=Drafting::chamferLines(objects[0].attr("DXFData").cast<std::string>(),
            objects[1].attr("DXFData").cast<std::string>(),first,second,keepFirst,keepSecond);
        return std::make_unique<CornerObjectEditPlan>(objects,std::vector<std::string>{result.first,result.second},result.connector,"LINE");
    },py::arg("entities"),py::arg("first_distance"),py::arg("second_distance"),
       py::arg("keep_first_start")=true,py::arg("keep_second_start")=false);
    py::class_<ContourObjectEditPlan,EditPlan>(module,"ContourObjectEditPlan")
        .def_property_readonly("document",&ContourObjectEditPlan::document)
        .def_property_readonly("root",&ContourObjectEditPlan::root)
        .def_property_readonly("template_object",&ContourObjectEditPlan::templateObject)
        .def_property_readonly("preserves_first",&ContourObjectEditPlan::preservesFirst)
        .def_property_readonly("created_objects",&ContourObjectEditPlan::createdObjects)
        .def_property_readonly("preview_records",&ContourObjectEditPlan::previewRecords)
        .def_property_readonly("drawing_bytes",&ContourObjectEditPlan::drawingBytes)
        .def("apply",&ContourObjectEditPlan::apply,
             "Apply once inside a document transaction; Python initializes presentation and recomputes. Rejects stale plans.");
    module.def("prepare_contour_edit",[](const std::vector<py::object>& objects,
        const std::vector<std::string>& records,bool preserveFirst) {
        return std::make_unique<ContourObjectEditPlan>(objects,records,preserveFirst);
    },py::arg("entities"),py::arg("records"),py::arg("preserve_first")=true,
       "Plan contour identity, replacements, additions and deletions; validate the complete resulting drawing without mutation.");
    py::class_<ContourOperationPlan,EditPlan>(module,"ContourOperationPlan")
        .def_property_readonly("document",&ContourOperationPlan::document)
        .def_property_readonly("root",&ContourOperationPlan::root)
        .def_property_readonly("template_object",&ContourOperationPlan::templateObject)
        .def_property_readonly("preserves_first",&ContourOperationPlan::preservesFirst)
        .def_property_readonly("created_objects",&ContourOperationPlan::createdObjects)
        .def_property_readonly("preview_records",&ContourOperationPlan::previewRecords)
        .def_property_readonly("drawing_bytes",&ContourOperationPlan::drawingBytes)
        .def("apply",&ContourOperationPlan::apply,
             "Apply once in a transaction; Python initializes presentation and recomputes. Rejects stale inputs, including trim boundaries.");
    module.def("prepare_contour_operation",[](const std::vector<py::object>& objects,const std::string& operation,
        std::size_t targetIndex,const std::vector<double>& distances,double tolerance) {
        return std::make_unique<ContourOperationPlan>(objects,operation,targetIndex,distances,tolerance);
    },py::arg("entities"),py::arg("operation"),py::arg("target_index")=0,
       py::arg("distances")=std::vector<double>{},py::arg("tolerance")=1e-7);
    module.def("prepare_contour_join",[](const std::vector<py::object>& objects,double tolerance) {
        return std::make_unique<ContourOperationPlan>(objects,"join",0,std::vector<double>{},tolerance);
    },py::arg("entities"),py::arg("tolerance")=1e-7);
    module.def("prepare_contour_close",[](py::object target,double tolerance) {
        return std::make_unique<ContourOperationPlan>(std::vector<py::object>{target},"close",0,std::vector<double>{},tolerance);
    },py::arg("target"),py::arg("tolerance")=1e-7);
    module.def("prepare_contour_split",[](py::object target,const std::vector<double>& distances) {
        return std::make_unique<ContourOperationPlan>(std::vector<py::object>{target},"split",0,distances,1e-7);
    },py::arg("target"),py::arg("distances"));
    module.def("prepare_contour_break",[](py::object target,double first,double second) {
        return std::make_unique<ContourOperationPlan>(std::vector<py::object>{target},"break",0,std::vector<double>{first,second},1e-7);
    },py::arg("target"),py::arg("first"),py::arg("second"));
    module.def("prepare_interior_trim",[](py::object target,const std::vector<py::object>& boundaries,double pickDistance) {
        std::vector<py::object> objects{target};
        objects.insert(objects.end(),boundaries.begin(),boundaries.end());
        return std::make_unique<ContourOperationPlan>(objects,"trim",0,std::vector<double>{pickDistance},1e-7);
    },py::arg("target"),py::arg("boundaries"),py::arg("pick_distance"));
    module.def("parse_entity", &parseEntity, py::arg("raw"), py::arg("context") = "");
    py::class_<SnapGeometry>(module,"SnapGeometry")
        .def(py::init([](const DXFEntity& entity) { return SnapGeometry(entity); }),py::arg("entity"))
        .def(py::init<const DXFEntity&,Vector3D,Vector3D,Vector3D,Vector3D>(),py::arg("entity"),
             py::arg("origin"),py::arg("x_axis"),py::arg("y_axis"),py::arg("z_axis"))
        .def("intersections",&SnapGeometry::intersections)
        .def_property_readonly("bound_points",&SnapGeometry::boundPoints);
    module.def("read_lwpolyline", &readLWPolyline);
    module.def("update_lwpolyline", &updateLWPolyline, py::arg("raw"), py::arg("polyline"), py::arg("origins") = std::vector<int>{});
    module.def("create_lwpolyline", &createLWPolyline, py::arg("polyline"), py::arg("layer") = "0");
    module.def("entity_properties", &Drafting::properties, py::arg("raw"), py::arg("layer"), py::arg("color"), py::arg("visible"), py::arg("context") = "");
    module.def("set_layer", &Drafting::setLayer, py::arg("drawing"), py::arg("name"), py::arg("color"), py::arg("on"));
    module.def("property_info", [](const std::string& raw) { const auto entity=parseEntity(raw); return py::make_tuple(entity->layer(),nativeEntityFields(entity->record()).integer(62,256),entityVisible(*entity)); });
    module.def("text_info", [](const std::string& raw) { const auto info=Drafting::textInfo(raw); return py::make_tuple(info.text,info.height,info.referenceWidth); });
    module.def("edit_text", &Drafting::editText, py::arg("raw"), py::arg("content"), py::arg("height"), py::arg("width") = 0);
    module.def("create_text", &Drafting::createText, py::arg("content"), py::arg("height"), py::arg("origin"), py::arg("multiline") = false, py::arg("width") = 0, py::arg("layer") = "0");
    module.def("chamfer_corner", &Drafting::chamfer, py::arg("raw"), py::arg("vertex"), py::arg("incoming"), py::arg("outgoing"));
    module.def("fillet_corner", &Drafting::fillet, py::arg("raw"), py::arg("vertex"), py::arg("radius"));
    py::class_<LineFilletResult>(module,"LineFilletResult")
        .def_readonly("first",&LineFilletResult::first)
        .def_readonly("second",&LineFilletResult::second)
        .def_readonly("arc",&LineFilletResult::arc);
    module.def("fillet_lines",&Drafting::filletLines,py::arg("first"),py::arg("second"),py::arg("radius"),
               py::arg("keep_first_start")=true,py::arg("keep_second_start")=false);
    py::class_<LineChamferResult>(module,"LineChamferResult")
        .def_readonly("first",&LineChamferResult::first)
        .def_readonly("second",&LineChamferResult::second)
        .def_readonly("connector",&LineChamferResult::connector);
    module.def("chamfer_lines",&Drafting::chamferLines,py::arg("first"),py::arg("second"),
               py::arg("first_distance"),py::arg("second_distance"),
               py::arg("keep_first_start")=true,py::arg("keep_second_start")=false);
    module.def("offset_entity", &Drafting::offset, py::arg("raw"), py::arg("distance"));
    module.def("trim_line", &Drafting::trimLine, py::arg("target"), py::arg("boundary"), py::arg("start"), py::arg("extend") = false);
    module.def("trim_entity", &Drafting::trim, py::arg("target"), py::arg("boundary"), py::arg("start"), py::arg("extend") = false);
    py::class_<Contour>(module,"Contour")
        .def_static("length",&Contour::length)
        .def_static("distance_at_point",&Contour::distanceAtPoint)
        .def_static("distance_at_ray",&Contour::distanceAtRay)
        .def_static("split",&Contour::split)
        .def_static("break_between",&Contour::breakBetween)
        .def_static("close",&Contour::close,py::arg("raw"),py::arg("tolerance")=1e-7)
        .def_static("join",&Contour::join,py::arg("entities"),py::arg("tolerance")=1e-7)
        .def_static("trim_interior",&Contour::trimInterior);
    module.def("create_rectangle", &createRectangle, py::arg("first"), py::arg("opposite"), py::arg("layer") = "0");
    module.def("create_line", &createLine, py::arg("start"), py::arg("end"), py::arg("layer") = "0");
    module.def("scale_factor", &scaleFactor, py::arg("center"), py::arg("target"), py::arg("reference_length"));
    module.def("rotation_angle", &rotationAngle, py::arg("center"), py::arg("direction"));
    module.def("displacement", &displacement, py::arg("start"), py::arg("end"));
    module.def("entity_preview_shape", [](const std::string& raw, const std::string& font, bool widths, const std::string& context) {
        return shape(*parseEntity(raw,context),font,widths);
    }, py::arg("raw"), py::arg("font") = "", py::arg("show_widths") = false, py::arg("context") = "");
    module.def("create_circle", &createCircle, py::arg("center"), py::arg("radius"), py::arg("layer") = "0");
    module.def("create_arc", &createArc, py::arg("center"),py::arg("radius"),py::arg("start_degrees"),py::arg("end_degrees"),
               py::arg("clockwise")=false,py::arg("layer")="0");
    module.def("create_arc_through_points", &createArcThroughPoints,py::arg("start"),py::arg("through"),py::arg("end"),py::arg("layer")="0");
    module.def("circle_radius", &circleRadius, py::arg("center"), py::arg("circumference"));
    module.def("circle_preview_shape", [](const Vector3D& center, double radius) {
        return shape(*parseEntity(createCircle(center,radius)), "", false);
    }, py::arg("center"), py::arg("radius"));
    module.def("point_on_xy_plane", &pointOnXYPlane, py::arg("start"), py::arg("end"), py::arg("elevation") = 0);
    module.def("point_in_xy_plane", &pointInXYPlane, py::arg("point"), py::arg("elevation") = 0);
    module.def("polyline_preview_shape", [](const DXFLWPolyline& polyline) {
        return shape(*parseEntity(createLWPolyline(polyline)), "", false);
    }, py::arg("polyline"), "Derive a temporary centerline Part shape without creating a document object.");
    module.def("create_feature", [](py::object document, const DXFEntity& entity, const std::string& font) {
#ifdef DXFCAD_NATIVE_OBJECTS
        const auto type = entity.type() == "LINE" ? "DXFCAD::LineObject" :
            entity.type() == "ARC" ? "DXFCAD::ArcObject" :
            entity.type() == "CIRCLE" ? "DXFCAD::CircleObject" :
            entity.type() == "TEXT" ? "DXFCAD::TextObject" :
            entity.type() == "MTEXT" ? "DXFCAD::MTextObject" :
            entity.type() == "DIMENSION" ? "DXFCAD::DimensionObject" :
            entity.type() == "INSERT" ? "DXFCAD::InsertObject" :
            entity.type() == "LEADER" ? "DXFCAD::LeaderObject" :
            entity.type() == "LWPOLYLINE" ? "DXFCAD::LWPolylineObject" : nullptr;
        if (type) {
            auto object = document.attr("addObject")(type, "DXF"+entity.type());
            object.attr("Label") = entity.type()+" "+entity.handle();
            object.attr("DXFData") = entity.raw();
            if (entity.type() == "TEXT" || entity.type() == "MTEXT" || entity.type() == "DIMENSION" || entity.type() == "INSERT") object.attr("TextFont") = font;
            if (entity.type() == "DIMENSION" || entity.type() == "INSERT" || entity.type() == "LEADER") object.attr("DXFContext") = entity.context();
            return object;
        }
#endif
        auto object = document.attr("addObject")("Part::FeaturePython", "DXF"+entity.type());
        object.attr("Label") = entity.type()+" "+entity.handle();
        property(object, "DXFType", py::str(entity.type()));
        property(object, "DXFLayer", py::str(entity.layer()));
        property(object, "DXFHandle", py::str(entity.handle()));
        property(object, "DXFData", py::str(entity.raw()));
        if (entity.type() == "DIMENSION" || entity.type() == "INSERT" || entity.type() == "LEADER") {
            property(object, "DXFContext", py::str(entity.context()));
            object.attr("setEditorMode")("DXFContext", 2);
        }
        object.attr("setEditorMode")("DXFData", 2);
        property(object, "DisplayStatus", py::str("Pending recompute"));
        object.attr("addProperty")("App::PropertyFile", "TextFont", "Display");
        object.attr("TextFont") = font;
        ensureDisplayProperties(object);
        return object;
    });
    module.def("ensure_display_properties", &ensureDisplayProperties);
    module.def("block_display_colors",[](py::object object) {
        std::vector<std::vector<double>> edges,faces;
        if (object.attr("Shape").attr("isNull")().cast<bool>()) return py::make_tuple(edges,faces);
        const auto entity=parseEntity(object.attr("DXFData").cast<std::string>(),object.attr("DXFContext").cast<std::string>());
        for (const auto& [child,color] : blockShapes(*entity,object.attr("Shape"),object.attr("TextFont").cast<std::string>())) {
            edges.insert(edges.end(),py::len(child.attr("Edges")),color);
            faces.insert(faces.end(),py::len(child.attr("Faces")),color);
        }
        return py::make_tuple(edges,faces);
    },py::arg("object"),"Resolve per-edge and per-face INSERT colors against its derived shape.");
    module.def("recompute_feature", [](py::object object) {
#ifdef DXFCAD_NATIVE_OBJECTS
        if (object.attr("isDerivedFrom")("DXFCAD::EntityObject").cast<bool>()) {
            object.attr("recompute")();
            return;
        }
#endif
        try {
            const auto entity = parseEntity(object.attr("DXFData").cast<std::string>(),
                py::hasattr(object, "DXFContext") ? object.attr("DXFContext").cast<std::string>() : "");
            ensureDisplayProperties(object);
            const bool showWidths = py::hasattr(object, "ShowWidths") && object.attr("ShowWidths").cast<bool>();
            object.attr("Shape") = shape(*entity, object.attr("TextFont").cast<std::string>(), showWidths);
            object.attr("DXFLayer") = entity->layer();
            object.attr("DXFHandle") = entity->handle();
            object.attr("DisplayStatus") = "Centerline";
            if (showWidths) object.attr("DisplayStatus") = "Widths (curved boundaries approximated)";
            if (entity->type() == "TEXT" || entity->type() == "MTEXT") object.attr("DisplayStatus") = "Text outlines (substitute font)";
            if (entity->type() == "DIMENSION") object.attr("DisplayStatus") = "Cached dimension picture (substitute font)";
            if (entity->type() == "INSERT") object.attr("DisplayStatus") = "Block instance (substitute font for text)";
        } catch (const std::exception& error) {
            object.attr("Shape") = py::module_::import("Part").attr("Shape")();
            object.attr("DisplayStatus") = std::string("Error: ")+error.what();
            throw;
        }
    });

}
