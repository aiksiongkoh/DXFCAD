#include "DocumentOperations.h"
#include <DXFCAD/Drafting.h>

namespace DXFCAD::FreeCAD {
namespace {
void refreshDependencies(py::object root,const Drawing& drawing)
{
    for (auto item : root.attr("Document").attr("Objects")) {
        const auto object=py::reinterpret_borrow<py::object>(item);
        if (!py::hasattr(object,"DXFSourceRoot") || !root.equal(py::object(object.attr("DXFSourceRoot"))) ||
            !py::hasattr(object,"DXFContext")) continue;
        const auto type=object.attr("DXFType").cast<std::string>();
        if (type=="INSERT" || type=="LEADER")
            object.attr("DXFContext")=drawing.resolveEntity(object.attr("DXFData").cast<std::string>())->context();
    }
}
}

void property(py::object object, const char* name, const py::object& value)
{
    object.attr("addProperty")("App::PropertyString", name, "DXF");
    object.attr(name) = value;
    object.attr("setEditorMode")(name, 1);
}
void ensureDisplayProperties(py::object object)
{
    for (const char* name : {"DXFLayer","DXFHandle"})
        if (!py::hasattr(object,name)) property(object,name,py::str(""));
    if (object.attr("DXFType").cast<std::string>() == "LWPOLYLINE" && !py::hasattr(object, "ShowWidths")) {
        object.attr("addProperty")("App::PropertyBool", "ShowWidths", "Display",
            "Display width regions with bounded miter joins; disable to show the centerline");
        object.attr("ShowWidths") = true;
    }
}
void setDrawingSource(py::object root,const Drawing& drawing,const std::string& sourceFile)
{
    const auto bytes=drawing.serialize();
#ifdef DXFCAD_NATIVE_OBJECTS
    if (root.attr("isDerivedFrom")("DXFCAD::DrawingObject").cast<bool>()) {
        py::module_::import("_DXFCADObjects").attr("initialize_drawing")(root,py::bytes(bytes),sourceFile);
        refreshDependencies(root,drawing);
        return;
    }
#endif
    // The same C++ adapter initializes legacy/compatibility drawing groups.
    const bool newNotes=!py::hasattr(root,"DisplayNotes");
    for (const auto& entry : std::map<std::string,std::string>{{"DXFSourceData","App::PropertyString"},
        {"SourceFile","App::PropertyFile"},{"SourceUnitCode","App::PropertyInteger"},
        {"ImportWarnings","App::PropertyStringList"},{"DisplayNotes","App::PropertyString"}})
        if (!py::hasattr(root,entry.first.c_str())) root.attr("addProperty")(entry.second,entry.first,"DXF");
    root.attr("DXFSourceData")=py::module_::import("base64").attr("b64encode")(py::bytes(bytes)).attr("decode")("ascii");
    root.attr("SourceFile")=sourceFile;
    root.attr("SourceUnitCode")=drawing.insertionUnits;
    root.attr("ImportWarnings")=drawing.warnings;
    if (newNotes) root.attr("DisplayNotes")="Source coordinates; widths where present; substitute font for TEXT/MTEXT";
    root.attr("setEditorMode")("DXFSourceData",2);
    for (const char* prop : {"SourceFile","SourceUnitCode","ImportWarnings","DisplayNotes"})
        root.attr("setEditorMode")(prop,1);
    refreshDependencies(root,drawing);
}

Drawing drawingSource(py::object root)
{
    if (!py::hasattr(root,"DXFSourceData") ||
        !root.attr("isDerivedFrom")("App::DocumentObjectGroup").cast<bool>())
        throw py::value_error("Select a DXFCAD drawing");
#ifdef DXFCAD_NATIVE_OBJECTS
    if (root.attr("isDerivedFrom")("DXFCAD::DrawingObject").cast<bool>())
        return Drawing::parse(py::module_::import("_DXFCADObjects").attr("drawing_bytes")(root).cast<std::string>());
#endif
    return Drawing::parse(py::module_::import("base64").attr("b64decode")(
        root.attr("DXFSourceData"),py::arg("validate")=true).cast<std::string>());
}

std::string currentDrawingBytes(py::object root)
{
#ifdef DXFCAD_NATIVE_OBJECTS
    if (root.attr("isDerivedFrom")("DXFCAD::DrawingObject").cast<bool>())
        return py::module_::import("_DXFCADObjects").attr("current_drawing_bytes")(root).cast<std::string>();
#endif
    const auto source=drawingSource(root);
    std::vector<std::pair<int,std::string>> records;
    for (auto item : root.attr("Document").attr("Objects")) {
        auto object=py::reinterpret_borrow<py::object>(item);
        if (!py::hasattr(object,"DXFSourceRoot") || !root.equal(py::object(object.attr("DXFSourceRoot")))
            || !py::hasattr(object,"DXFData")) continue;
        if (!py::hasattr(object,"DXFSourceIndex") || !py::hasattr(object,"Placement"))
            throw py::value_error("Drawing has an invalid DXF entity");
        if (!object.attr("Placement").attr("isIdentity")().cast<bool>())
            throw py::value_error("Reset FreeCAD Placement before DXFCAD export; use DXFCAD transforms for native coordinates");
        records.emplace_back(object.attr("DXFSourceIndex").cast<int>(),object.attr("DXFData").cast<std::string>());
    }
    return source.serializeCurrent(records);
}

// FreeCAD persistence adapter for layer identity. Labels are presentation only.
py::object findLayerGroup(py::object root,const std::string& name)
{
    py::object found=py::none();
    for (auto item : root.attr("Group")) {
        auto group=py::reinterpret_borrow<py::object>(item);
        if (!group.attr("isDerivedFrom")("App::DocumentObjectGroup").cast<bool>()) continue;
        bool matches=false;
        if (py::hasattr(group,"DXFLayerName")) matches=group.attr("DXFLayerName").cast<std::string>()==name;
        else {
            // Upgrade old imported groups only when their native records agree.
            bool any=false, all=true;
            for (auto child : group.attr("Group")) {
                auto object=py::reinterpret_borrow<py::object>(child);
                if (!py::hasattr(object,"DXFData")) continue;
                const auto layer=parseEntity(object.attr("DXFData").cast<std::string>(),
                    py::hasattr(object,"DXFContext") ? object.attr("DXFContext").cast<std::string>() : "")->layer();
                any=any || layer==name;
                all=all && layer==name;
            }
            if (any && !all) throw py::value_error("Legacy layer group contains mixed native layers");
            matches=any && all;
        }
        if (matches) {
            if (!found.is_none()) throw py::value_error("Drawing has duplicate layer groups for "+name);
            found=group;
        }
    }
    return found;
}

py::object materializeLayerGroup(py::object root,const std::string& name,py::object group)
{
    if (group.is_none()) {
        group=root.attr("Document").attr("addObject")("App::DocumentObjectGroup","DXFLayer");
        group.attr("Label")=name;
        root.attr("addObject")(group);
    }
    if (!py::hasattr(group,"DXFLayerName")) {
        group.attr("addProperty")("App::PropertyString","DXFLayerName","DXF");
        group.attr("DXFLayerName")=name;
        group.attr("setEditorMode")("DXFLayerName",1);
    }
    return group;
}

Drawing layerSource(const Drawing& source,const std::string& name,int color,bool visible,bool edit)
{
    // Existing source layer properties survive an ensure/assignment operation.
    if (!edit && source.layers.count(name)) return source;
    return Drawing::parse(Drafting::setLayer(source.serialize(),name,color,visible));
}

void retainSource(py::object root,const Drawing& source)
{
    setDrawingSource(root,source,py::hasattr(root,"SourceFile") ? root.attr("SourceFile").cast<std::string>() : "");
}

// Call after ownership validation, or for fresh objects in a fresh population.
void setEntityOwnership(py::object object,py::object root,int index)
{
    if (!py::hasattr(object,"DXFSourceRoot"))
        object.attr("addProperty")("App::PropertyLinkHidden","DXFSourceRoot","DXF");
    if (!py::hasattr(object,"DXFSourceIndex"))
        object.attr("addProperty")("App::PropertyInteger","DXFSourceIndex","DXF");
    object.attr("DXFSourceRoot")=root;
    object.attr("DXFSourceIndex")=index;
    object.attr("setEditorMode")("DXFSourceRoot",2);
    object.attr("setEditorMode")("DXFSourceIndex",2);
}

py::object createOwnedEntity(py::object root,const std::shared_ptr<DXFEntity>& entity,
                             int index,const std::string& font,py::object group)
{
    py::object document=root.attr("Document");
    py::object object=py::module_::import("DXFCAD").attr("create_feature")(document,py::cast(entity),font);
    if (!document.equal(py::object(object.attr("Document"))) || !py::hasattr(object,"DXFData")
        || !py::getattr(object,"DXFSourceRoot",py::none()).is_none())
        throw py::value_error("Entity factory must create a fresh DXF object in the target document");
    setEntityOwnership(object,root,index);
    group.attr("addObject")(object);
    return object;
}

py::object createEntity(py::object document,const std::string& raw,py::object root,const std::string& font)
{
    if (root.is_none()) {
        for (auto handle : document.attr("Objects")) {
            auto object=py::reinterpret_borrow<py::object>(handle);
            if (!py::hasattr(object,"DXFSourceData")) continue;
            if (!root.is_none())
                throw py::value_error("Select a DXF drawing before creating geometry in a document with multiple drawings");
            root=object;
        }
    }
    if (!root.is_none() && !document.equal(py::object(root.attr("Document"))))
        throw py::value_error("Choose a DXFCAD drawing in the active document");
    const auto source=root.is_none() ? Drawing::empty(4) : drawingSource(root);
    const auto entity=source.resolveEntity(raw);
    const auto name=entity->layer();
    const auto info=source.layers.count(name) ? source.layers.at(name) : source.layers.at("0");
    const auto updated=layerSource(source,name,info.color,info.visible,false);
    py::object group=py::none();
    std::vector<std::pair<int,std::string>> proposed;
    if (!root.is_none()) {
        currentDrawingBytes(root); // Validate existing ownership and placements too.
        group=findLayerGroup(root,name);
        for (auto handle : document.attr("Objects")) {
            auto object=py::reinterpret_borrow<py::object>(handle);
            if (root.equal(py::getattr(object,"DXFSourceRoot",py::none())) && py::hasattr(object,"DXFData"))
                proposed.emplace_back(object.attr("DXFSourceIndex").cast<int>(),object.attr("DXFData").cast<std::string>());
        }
    }
    proposed.emplace_back(-1,raw);
    updated.serializeCurrent(proposed); // Finish all record/layer/version validation before mutation.
    if (root.is_none()) root=py::module_::import("DXFCAD").attr("create_drawing")(document,py::cast(updated));
    else if (!source.layers.count(name)) retainSource(root,updated);
    group=materializeLayerGroup(root,name,group);
    return createOwnedEntity(root,entity,-1,font,group);
}

py::object ensureLayer(py::object root,const std::string& name,int color,bool visible,bool edit)
{
    const auto source=drawingSource(root);
    const auto updated=layerSource(source,name,color,visible,edit);
    auto group=findLayerGroup(root,name); // Validate ambiguity before mutation.
    group=materializeLayerGroup(root,name,group);
    if (edit || !source.layers.count(name)) retainSource(root,updated);
    return group;
}

py::object cornerRoot(const std::vector<py::object>& objects)
{
    if (objects.size()!=2 || objects[0].equal(objects[1]))
        throw py::value_error("Corner editing requires two distinct native LINE objects");
    auto root=py::getattr(objects[0],"DXFSourceRoot",py::none());
    if (root.is_none() || !root.equal(py::getattr(objects[1],"DXFSourceRoot",py::none()))
        || !py::object(objects[0].attr("Document")).equal(py::object(objects[1].attr("Document"))))
        throw py::value_error("Corner lines must belong to the same DXFCAD drawing");
    for (auto object : objects)
        if (!py::hasattr(object,"DXFData") || parseEntity(object.attr("DXFData").cast<std::string>())->type()!="LINE")
            throw py::value_error("Corner editing requires two native LINE objects");
    return root;
}

} // namespace DXFCAD::FreeCAD
