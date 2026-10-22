#include <DXFCAD/DXFDocument.h>
#include <DXFCAD/Drafting.h>
#include <DXFCAD/SnapGeometry.h>
#include <DXFCAD/Contour.h>
#include <DXFCAD/BatchTransform.h>
#include <DXFCAD/CornerEdit.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <filesystem>
#include <map>
#include <set>
#include <algorithm>
#include <memory>
#include <limits>

namespace py = pybind11;
using namespace DXFCAD;

namespace {
std::string utf8Path(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
py::object vector(const Vector3D& p) {
    return py::module_::import("FreeCAD").attr("Vector")(p.x, p.y, p.z);
}
py::object shape(const DXFEntity& entity, const std::string& fontFile, bool showWidths)
{
    const auto geometry = entity.geometry();
    const auto part = py::module_::import("Part");
    py::list edges;
    for (const auto& instance : entity.blockParts()) {
        const auto child=shape(*instance.entity,fontFile,true);
        if (child.attr("isNull")().cast<bool>()) continue;
        auto matrix=py::module_::import("FreeCAD").attr("Matrix")();
        const Vector3D columns[]={instance.xAxis,instance.yAxis,instance.zAxis,instance.origin};
        for (int col=0;col<4;++col) {
            const double values[]={columns[col].x,columns[col].y,columns[col].z};
            for (int row=0;row<3;++row) {
                const auto name="A"+std::to_string(row+1)+std::to_string(col+1);
                matrix.attr(name.c_str())=values[row];
            }
        }
        edges.append(child.attr("transformGeometry")(matrix));
    }
    std::set<std::size_t> covered;
    if (showWidths) {
        py::list faces;
        for (const auto& region : entity.widthRegions()) {
            py::list points;
            for (const auto& p : region.points) points.append(vector(p));
            points.append(vector(region.points.front()));
            auto face = part.attr("Face")(part.attr("makePolygon")(points));
            if (!face.attr("isValid")().cast<bool>()) throw std::runtime_error("Invalid DXF width face");
            faces.append(face);
            covered.insert(region.segmentIndex);
        }
        if (py::len(faces)) {
            py::object merged = faces[0];
            if (py::len(faces) > 1) {
                py::list others;
                for (py::ssize_t i = 1; i < py::len(faces); ++i) others.append(faces[i]);
                merged = merged.attr("multiFuse")(others);
                // OCC refinement can invalidate a valid coplanar union near
                // the center of a tapered major arc. Keep the valid union in
                // that case; removing internal seams is optional presentation.
                const auto refined = merged.attr("removeSplitter")();
                if (refined.attr("isValid")().cast<bool>()) merged = refined;
            }
            if (!merged.attr("isValid")().cast<bool>()) throw std::runtime_error("Invalid united DXF width faces");
            edges.append(merged);
        }
    }
    for (std::size_t i = 0; i < geometry.segments.size(); ++i) {
        if (covered.count(i)) continue;
        const auto& s = geometry.segments[i];
        if (s.arc) {
            edges.append(part.attr("Arc")(vector(s.start), vector(s.arc->midpoint), vector(s.end)).attr("toShape")());
        } else if (s.start.x != s.end.x || s.start.y != s.end.y || s.start.z != s.end.z) {
            edges.append(part.attr("makeLine")(vector(s.start), vector(s.end)));
        }
    }
    for (const auto& c : geometry.circles) {
        edges.append(part.attr("makeCircle")(c.radius, vector(c.center), vector(c.normal)));
    }
    for (const auto& face : geometry.faces) {
        py::list points;
        for (const auto& p : face) points.append(vector(p));
        points.append(vector(face.front()));
        const auto built = part.attr("Face")(part.attr("makePolygon")(points));
        if (!built.attr("isValid")().cast<bool>()) throw std::runtime_error("Invalid DIMENSION arrowhead face");
        edges.append(built);
    }
    auto texts = geometry.texts;
    if (geometry.text) texts.push_back(*geometry.text);
    for (const auto& t : texts) {
        if (t.text.empty()) continue;
        const auto font = std::filesystem::u8path(fontFile);
        if (!std::filesystem::is_regular_file(font)) throw std::runtime_error("Select an existing TrueType font for TEXT display");
        std::map<std::string, std::pair<py::object, TextBounds>> cache;
        auto measure = [&](const std::string& value) {
            if (cache.count(value)) return cache.at(value).second;
            if (value.empty()) { cache[value] = {py::none(), {}}; return TextBounds{}; }
            const auto letters = part.attr("makeWireString")(value, utf8Path(font.parent_path())+"/", utf8Path(font.filename()), t.height);
            py::list wires;
            for (const auto letter : letters) for (const auto wire : letter) wires.append(wire);
            if (!py::len(wires)) {
                if (value.find_first_not_of(" \t\r\n") != std::string::npos)
                    throw std::runtime_error("Cannot build text outlines from the selected font");
                cache[value] = {py::none(), {}};
                return TextBounds{};
            }
            const auto compound = part.attr("makeCompound")(wires);
            const auto bounds = compound.attr("BoundBox");
            TextBounds b{bounds.attr("XMin").cast<double>(), bounds.attr("YMin").cast<double>(),
                         bounds.attr("XMax").cast<double>(), bounds.attr("YMax").cast<double>()};
            cache[value] = {compound, b};
            return b;
        };
        for (const auto& run : t.layout(measure)) {
            const auto b = measure(run.text);
            if (cache.at(run.text).first.is_none()) continue;
            py::object textShape = cache.at(run.text).first.attr("copy")();
            const auto origin = run.alignedOrigin(b.minX, b.minY, b.maxX, b.maxY);
            const auto app = py::module_::import("FreeCAD");
            if (run.widthFactor != 1) {
                auto scale = app.attr("Matrix")(); scale.attr("A11") = run.widthFactor;
                textShape = textShape.attr("transformGeometry")(scale);
            }
            const auto rotation = app.attr("Rotation")(vector(run.xAxis), vector(run.yAxis), vector(run.normal), "ZXY");
            textShape.attr("Placement") = app.attr("Placement")(vector(origin), rotation);
            edges.append(textShape);
        }
    }
    return py::len(edges) ? part.attr("makeCompound")(edges) : part.attr("Shape")();
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
void setDrawingSource(py::object root,const DXFDocument& drawing,const std::string& sourceFile)
{
    const auto bytes=drawing.serialize();
#ifdef DXFCAD_NATIVE_OBJECTS
    if (root.attr("isDerivedFrom")("DXFCAD::DXFDocumentObject").cast<bool>()) {
        py::module_::import("_DXFCADObjects").attr("initialize_drawing")(root,py::bytes(bytes),sourceFile);
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
}

DXFDocument drawingSource(py::object root)
{
    if (!py::hasattr(root,"DXFSourceData") ||
        !root.attr("isDerivedFrom")("App::DocumentObjectGroup").cast<bool>())
        throw py::value_error("Select a DXFCAD drawing");
#ifdef DXFCAD_NATIVE_OBJECTS
    if (root.attr("isDerivedFrom")("DXFCAD::DXFDocumentObject").cast<bool>())
        return DXFDocument::parse(py::module_::import("_DXFCADObjects").attr("drawing_bytes")(root).cast<std::string>());
#endif
    return DXFDocument::parse(py::module_::import("base64").attr("b64decode")(
        root.attr("DXFSourceData"),py::arg("validate")=true).cast<std::string>());
}

std::string currentDrawingBytes(py::object root)
{
#ifdef DXFCAD_NATIVE_OBJECTS
    if (root.attr("isDerivedFrom")("DXFCAD::DXFDocumentObject").cast<bool>())
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

DXFDocument layerSource(const DXFDocument& source,const std::string& name,int color,bool visible,bool edit)
{
    // Existing source layer properties survive an ensure/assignment operation.
    if (!edit && source.layers.count(name)) return source;
    return DXFDocument::parse(Drafting::setLayer(source.serialize(),name,color,visible));
}

void retainSource(py::object root,const DXFDocument& source)
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
    const auto source=root.is_none() ? DXFDocument::empty(4) : drawingSource(root);
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

class LayerEditPlan {
public:
    py::object document=py::none();
    std::map<std::string,py::object> roots, groups;
    std::map<std::string,DXFDocument> sources;
    std::vector<std::tuple<py::object,py::object,std::string>> records;
    std::string layer;

    LayerEditPlan(const std::vector<py::object>& objects,const std::string& name,int color,
                  bool visible,int layerColor,bool layerOn,bool editLayer,bool preserveVisibility=false) : layer(name)
    {
        if (objects.empty()) throw py::value_error("Select DXFCAD objects to edit");
        document=objects.front().attr("Document");
        std::set<std::string> seen;
        for (auto object : objects) {
            if (!document.equal(py::object(object.attr("Document"))) || !py::hasattr(object,"DXFData"))
                throw py::value_error("Layer editing requires DXFCAD objects in one document");
            if (!seen.insert(object.attr("Name").cast<std::string>()).second) continue;
            if (!object.attr("Placement").attr("isIdentity")().cast<bool>())
                throw py::value_error("Reset FreeCAD Placement before editing native DXF properties");
            if (!py::hasattr(object,"DXFSourceRoot") || object.attr("DXFSourceRoot").is_none())
                throw py::value_error("Properties require objects in a DXFCAD drawing");
            py::object root=object.attr("DXFSourceRoot");
            if (!document.equal(py::object(root.attr("Document"))))
                throw py::value_error("Entity and drawing must belong to the same document");
            const auto key=root.attr("Name").cast<std::string>();
            if (!sources.count(key)) {
                roots.emplace(key,root);
                sources.emplace(key,layerSource(drawingSource(root),name,layerColor,layerOn,editLayer));
                groups.emplace(key,findLayerGroup(root,name));
            }
            const auto context=py::hasattr(object,"DXFContext") ? object.attr("DXFContext").cast<std::string>() : "";
            const auto raw=object.attr("DXFData").cast<std::string>();
            const bool on=preserveVisibility ? entityVisible(*parseEntity(raw,context)) : visible;
            records.emplace_back(object,root,Drafting::properties(raw,name,color,on,context));
        }
    }

    py::tuple result() const { return py::make_tuple(document,roots,sources,records); }

    void apply()
    {
        for (const auto& [key,root] : roots) {
            retainSource(root,sources.at(key));
            groups.at(key)=materializeLayerGroup(root,layer,groups.at(key));
        }
        for (const auto& [object,root,raw] : records) {
            auto target=groups.at(root.attr("Name").cast<std::string>());
            py::list parents=object.attr("InList");
            py::list owned=root.attr("Group");
            for (auto item : parents) {
                auto group=py::reinterpret_borrow<py::object>(item);
                if (owned.contains(group) && !group.equal(target) && py::hasattr(group,"Group"))
                    group.attr("removeObject")(object);
            }
            if (!py::list(target.attr("Group")).contains(object)) target.attr("addObject")(object);
            object.attr("DXFData")=raw;
        }
    }
};

class ContourObjectEditPlan {
    py::object document_,root_;
    std::vector<py::object> selected_,created_;
    std::vector<std::string> names_;
    std::vector<std::pair<int,std::string>> selectedSnapshot_;
    std::string sourceSnapshot_,liveSnapshot_;
    std::unique_ptr<ContourEditPlan> domain_;
    bool applied_=false;
public:
    ContourObjectEditPlan(const std::vector<py::object>& objects,const std::vector<std::string>& results,bool preserveFirst)
        : selected_(objects)
    {
        if (objects.empty()) throw py::value_error("Select contours to replace");
        document_=objects.front().attr("Document");
        if (!py::hasattr(objects.front(),"DXFSourceRoot") || objects.front().attr("DXFSourceRoot").is_none())
            throw py::value_error("Contour cleanup requires objects in one DXFCAD drawing");
        root_=objects.front().attr("DXFSourceRoot");
        if (!document_.equal(py::object(root_.attr("Document"))))
            throw py::value_error("Contour cleanup requires objects in one DXFCAD drawing");
        const auto source=drawingSource(root_);
        sourceSnapshot_=root_.attr("DXFSourceData").cast<std::string>();
        liveSnapshot_=currentDrawingBytes(root_); // Includes placement and ownership validation.
        std::vector<py::object> liveObjects;
        std::vector<std::pair<int,std::string>> live;
        for (auto item : document_.attr("Objects")) {
            auto object=py::reinterpret_borrow<py::object>(item);
            if (py::hasattr(object,"DXFSourceRoot") && root_.equal(py::object(object.attr("DXFSourceRoot")))
                && py::hasattr(object,"DXFData")) {
                liveObjects.push_back(object);
                live.emplace_back(object.attr("DXFSourceIndex").cast<int>(),object.attr("DXFData").cast<std::string>());
            }
        }
        std::vector<std::size_t> selection;
        for (auto object : objects) {
            if (!document_.equal(py::object(object.attr("Document"))) || !py::hasattr(object,"DXFSourceRoot")
                || !root_.equal(py::object(object.attr("DXFSourceRoot"))))
                throw py::value_error("Contour cleanup requires objects in one DXFCAD drawing");
            auto found=std::find_if(liveObjects.begin(),liveObjects.end(),[&](const auto& candidate) { return candidate.equal(object); });
            if (found==liveObjects.end()) throw py::value_error("Select native DXF contours");
            selection.push_back(static_cast<std::size_t>(found-liveObjects.begin()));
            names_.push_back(object.attr("Name").cast<std::string>());
            selectedSnapshot_.emplace_back(object.attr("DXFSourceIndex").cast<int>(),object.attr("DXFData").cast<std::string>());
        }
        domain_=std::make_unique<ContourEditPlan>(source,live,selection,results,preserveFirst);
        // Resolve every target group before any mutation, including legacy ambiguity.
        for (const auto& raw : domain_->records()) findLayerGroup(root_,parseEntity(raw)->layer());
    }

    py::object document() const { return document_; }
    py::object root() const { return root_; }
    py::object templateObject() const { return selected_.front(); }
    bool preservesFirst() const { return domain_->preservesFirst(); }
    const std::vector<py::object>& createdObjects() const { return created_; }
    py::list previewRecords() const {
        py::list records;
        for (const auto& raw : domain_->records()) records.append(py::make_tuple(selected_.front(),raw));
        return records;
    }
    py::bytes drawingBytes() const { return py::bytes(domain_->drawingBytes()); }

    std::vector<py::object> apply()
    {
        if (applied_) throw py::value_error("Contour edit plan has already been applied");
        for (std::size_t i=0;i<selected_.size();++i) {
            py::object current=document_.attr("getObject")(names_[i]);
            if (current.is_none() || !current.equal(selected_[i])
                || !root_.equal(py::object(current.attr("DXFSourceRoot")))
                || current.attr("DXFSourceIndex").cast<int>()!=selectedSnapshot_[i].first
                || current.attr("DXFData").cast<std::string>()!=selectedSnapshot_[i].second)
                throw py::value_error("Selected contours changed; rebuild the contour edit plan");
        }
        if (sourceSnapshot_!=root_.attr("DXFSourceData").cast<std::string>() || liveSnapshot_!=currentDrawingBytes(root_))
            throw py::value_error("Drawing changed; rebuild the contour edit plan");
        // Group membership can change without changing DXF bytes.
        std::map<std::string,py::object> groups;
        for (const auto& raw : domain_->records()) {
            const auto layer=parseEntity(raw)->layer();
            groups.emplace(layer,findLayerGroup(root_,layer));
        }
        applied_=true; // One mutation attempt; callers roll back failures in their transaction.
        for (auto& [layer,group] : groups) group=materializeLayerGroup(root_,layer,group);
        std::vector<py::object> result;
        for (std::size_t i=0;i<domain_->records().size();++i) {
            const auto& raw=domain_->records()[i];
            const auto entity=parseEntity(raw);
            py::object object;
            if (i==0 && domain_->preservesFirst()) {
                object=selected_.front();
                object.attr("DXFData")=raw;
            } else {
                object=createOwnedEntity(root_,entity,-1,"",groups.at(entity->layer()));
                created_.push_back(object);
            }
            auto target=groups.at(entity->layer());
            py::list owned=root_.attr("Group"), parents=object.attr("InList");
            for (auto item : parents) {
                auto group=py::reinterpret_borrow<py::object>(item);
                if (owned.contains(group) && !group.equal(target) && py::hasattr(group,"Group"))
                    group.attr("removeObject")(object);
            }
            if (!py::list(target.attr("Group")).contains(object)) target.attr("addObject")(object);
            result.push_back(object);
        }
        for (std::size_t i=domain_->preservesFirst() ? 1 : 0;i<names_.size();++i)
            document_.attr("removeObject")(names_[i]);
        return result;
    }
};

class BatchObjectTransformPlan {
    struct Selected {
        py::object object,root;
        std::string name,raw,context,font;
        int index;
        bool hasIndex;
    };
    py::object document_;
    std::vector<Selected> selected_;
    std::unique_ptr<BatchTransformPlan> domain_;
    std::vector<std::string> records_;
    std::map<std::string,py::object> roots_;
    std::map<std::string,std::string> sourceSnapshots_,liveSnapshots_,drawingBytes_;
    bool applied_=false;
public:
    virtual ~BatchObjectTransformPlan() = default;
    BatchObjectTransformPlan(const std::vector<py::object>& objects,const std::string& operation,
                             Vector3D offset,double value,Vector3D pivot,bool allowIdentity,
                             const std::vector<std::string>* copyRecords=nullptr)
    {
        if (objects.empty()) throw py::value_error("Select DXFCAD objects to transform");
        if (copyRecords && copyRecords->size()!=objects.size())
            throw py::value_error("Provide one copy record per selected object");
        document_=objects.front().attr("Document");
        std::set<std::string> seen;
        std::vector<BatchTransformInput> inputs;
        for (auto object : objects) {
            if (!document_.equal(py::object(object.attr("Document"))) || !py::hasattr(object,"DXFData"))
                throw py::value_error("Transform requires DXFCAD objects in one document");
            const auto name=object.attr("Name").cast<std::string>();
            if (!seen.insert(name).second) {
                if (copyRecords) throw py::value_error("Select each object only once for a copy plan");
                continue;
            }
            if (!object.attr("Placement").attr("isIdentity")().cast<bool>())
                throw py::value_error("Reset FreeCAD Placement before using native DXFCAD transforms");
            const auto status=py::getattr(object,"DisplayStatus",py::str("")).cast<std::string>();
            if (status.rfind("Error:",0)==0) throw py::value_error(status);
            Selected item{object,py::getattr(object,"DXFSourceRoot",py::none()),name,
                object.attr("DXFData").cast<std::string>(),
                py::getattr(object,"DXFContext",py::str("")).cast<std::string>(),
                py::getattr(object,"TextFont",py::str("")).cast<std::string>(),
                py::getattr(object,"DXFSourceIndex",py::int_(-1)).cast<int>(),py::hasattr(object,"DXFSourceIndex")};
            inputs.push_back({item.raw,item.context});
            if (!item.root.is_none()) {
                if (!document_.equal(py::object(item.root.attr("Document"))))
                    throw py::value_error("Entity and drawing must belong to the same document");
                const auto key=item.root.attr("Name").cast<std::string>();
                if (!roots_.count(key)) {
                    roots_.emplace(key,item.root);
                    sourceSnapshots_[key]=item.root.attr("DXFSourceData").cast<std::string>();
                    liveSnapshots_[key]=currentDrawingBytes(item.root);
                }
            }
            selected_.push_back(std::move(item));
        }
        domain_=std::make_unique<BatchTransformPlan>(inputs,operation,offset,value,pivot,allowIdentity);
        records_=domain_->records();
        if (copyRecords) {
            if (!domain_->copies()) throw py::value_error("Copy records require a copy operation");
            for (std::size_t i=0;i<selected_.size();++i) {
                const auto original=parseEntity(selected_[i].raw,selected_[i].context);
                const auto candidate=parseEntity((*copyRecords)[i],selected_[i].context);
                candidate->geometry();
                if (candidate->type()!=original->type() || candidate->layer()!=original->layer())
                    throw py::value_error("Related copies must retain entity type and layer");
                records_[i]=candidate->copied(Vector3D{});
            }
        }
        if (domain_->copies())
            for (const auto& item : selected_)
                if (item.root.is_none()) throw py::value_error("Copy requires objects belonging to a DXFCAD drawing");
        for (const auto& [key,root] : roots_) {
            std::map<std::string,std::string> updates;
            std::vector<std::string> additions;
            for (std::size_t i=0;i<selected_.size();++i) {
                const auto& item=selected_[i];
                if (!item.root.equal(root)) continue;
                if (domain_->copies()) {
                    additions.push_back(records_[i]);
                    findLayerGroup(root,parseEntity(records_[i],item.context)->layer());
                } else updates.emplace(item.name,records_[i]);
            }
            std::vector<std::pair<int,std::string>> proposed;
            for (auto handle : document_.attr("Objects")) {
                auto object=py::reinterpret_borrow<py::object>(handle);
                if (!root.equal(py::getattr(object,"DXFSourceRoot",py::none())) || !py::hasattr(object,"DXFData")) continue;
                const auto found=updates.find(object.attr("Name").cast<std::string>());
                proposed.emplace_back(object.attr("DXFSourceIndex").cast<int>(),
                    found==updates.end() ? object.attr("DXFData").cast<std::string>() : found->second);
            }
            for (const auto& raw : additions) proposed.emplace_back(-1,raw);
            drawingBytes_[key]=drawingSource(root).serializeCurrent(proposed);
        }
    }
    py::object document() const { return document_; }
    bool copies() const { return domain_->copies(); }
    py::list objects() const {
        py::list result;
        for (const auto& item : selected_) result.append(item.object);
        return result;
    }
    py::list previewRecords() const {
        py::list result;
        for (std::size_t i=0;i<selected_.size();++i)
            result.append(py::make_tuple(selected_[i].object,records_[i]));
        return result;
    }
    py::dict drawingBytes() const {
        py::dict result;
        for (const auto& [key,bytes] : drawingBytes_) result[py::str(key)]=py::bytes(bytes);
        return result;
    }
    void validateCurrent(const std::string& kind="batch transform") const
    {
        if (applied_) throw py::value_error("Batch transform plan has already been applied");
        for (const auto& item : selected_) {
            py::object current=document_.attr("getObject")(item.name);
            if (current.is_none() || !current.equal(item.object)
                || current.attr("DXFData").cast<std::string>()!=item.raw
                || py::getattr(current,"DXFContext",py::str("")).cast<std::string>()!=item.context
                || py::getattr(current,"TextFont",py::str("")).cast<std::string>()!=item.font
                || !item.root.equal(py::getattr(current,"DXFSourceRoot",py::none()))
                || py::hasattr(current,"DXFSourceIndex")!=item.hasIndex
                || py::getattr(current,"DXFSourceIndex",py::int_(-1)).cast<int>()!=item.index)
                throw py::value_error("Selected objects changed; rebuild the "+kind+" plan");
            if (!current.attr("Placement").attr("isIdentity")().cast<bool>())
                throw py::value_error("Reset FreeCAD Placement before using native DXFCAD transforms");
        }
        for (const auto& [key,root] : roots_)
            if (sourceSnapshots_.at(key)!=root.attr("DXFSourceData").cast<std::string>()
                || liveSnapshots_.at(key)!=currentDrawingBytes(root))
                throw py::value_error("Drawing changed; rebuild the "+kind+" plan");
        if (domain_->copies())
            for (std::size_t i=0;i<selected_.size();++i)
                findLayerGroup(selected_[i].root,parseEntity(records_[i],selected_[i].context)->layer());
    }
    std::vector<py::object> apply()
    {
        validateCurrent();
        applied_=true;
        std::vector<py::object> result;
        for (std::size_t i=0;i<selected_.size();++i) {
            const auto& item=selected_[i];
            const auto& raw=records_[i];
            if (!domain_->copies()) {
                item.object.attr("DXFData")=raw;
                result.push_back(item.object);
            } else {
                const auto entity=drawingSource(item.root).resolveEntity(raw);
                auto group=materializeLayerGroup(item.root,entity->layer(),findLayerGroup(item.root,entity->layer()));
                py::object clone=createOwnedEntity(item.root,entity,-1,item.font,group);
                result.push_back(clone);
            }
        }
        return result;
    }
};

class EntityCopyPlan : public BatchObjectTransformPlan {
public:
    EntityCopyPlan(const std::vector<py::object>& objects,const std::vector<std::string>& records)
        : BatchObjectTransformPlan(objects,"copy",Vector3D{},0,Vector3D{},true,&records) {}
};

// Geometry/content replacements preserve object identity and presentation.
// Layer/style changes use LayerEditPlan, which also updates group membership.
class EntityUpdatePlan {
    std::vector<py::object> objects_;
    std::vector<std::string> records_;
    std::unique_ptr<BatchObjectTransformPlan> snapshot_;
    py::object document_;
    std::map<std::string,std::string> drawingBytes_;
    bool applied_=false;
public:
    EntityUpdatePlan(const std::vector<py::object>& objects,const std::vector<std::string>& records)
        : objects_(objects),records_(records)
    {
        if (objects.empty() || objects.size()!=records.size())
            throw py::value_error("Provide one replacement record per selected DXFCAD object");
        std::set<std::string> names;
        for (auto object : objects)
            if (!names.insert(object.attr("Name").cast<std::string>()).second)
                throw py::value_error("Select each object only once for an entity update");
        snapshot_=std::make_unique<BatchObjectTransformPlan>(objects,"move",Vector3D{},0,Vector3D{},true);
        document_=snapshot_->document();
        std::map<std::string,py::object> roots;
        std::map<std::string,std::string> updates;
        for (std::size_t i=0;i<objects.size();++i) {
            auto object=objects[i];
            const auto context=py::getattr(object,"DXFContext",py::str("")).cast<std::string>();
            const auto original=parseEntity(object.attr("DXFData").cast<std::string>(),context);
            const auto replacement=parseEntity(records[i],context);
            replacement->geometry();
            const auto before=nativeEntityFields(original->record());
            const auto after=nativeEntityFields(replacement->record());
            // Compare every occurrence, so duplicate identity/style fields cannot bypass validation.
            for (int code : {0,5,330,67,410,8,62,420,430,440,60,6,48,370}) {
                std::vector<std::string> a,b;
                for (const auto& field : before.groups) if (field.first==code) a.push_back(field.second);
                for (const auto& field : after.groups) if (field.first==code) b.push_back(field.second);
                if (a!=b) throw py::value_error("Entity updates must retain type, identity, layer and display properties");
            }
            updates.emplace(object.attr("Name").cast<std::string>(),records[i]);
            auto root=py::getattr(object,"DXFSourceRoot",py::none());
            if (!root.is_none()) roots.emplace(root.attr("Name").cast<std::string>(),root);
        }
        // Validate the complete proposed drawing, including untouched and opaque records.
        for (const auto& [name,root] : roots) {
            std::vector<std::pair<int,std::string>> proposed;
            for (auto handle : document_.attr("Objects")) {
                auto object=py::reinterpret_borrow<py::object>(handle);
                if (!root.equal(py::getattr(object,"DXFSourceRoot",py::none())) || !py::hasattr(object,"DXFData")) continue;
                const auto found=updates.find(object.attr("Name").cast<std::string>());
                proposed.emplace_back(object.attr("DXFSourceIndex").cast<int>(),
                    found==updates.end() ? object.attr("DXFData").cast<std::string>() : found->second);
            }
            drawingBytes_[name]=drawingSource(root).serializeCurrent(proposed);
        }
    }
    py::object document() const { return document_; }
    const std::vector<py::object>& objects() const { return objects_; }
    py::list previewRecords() const {
        py::list result;
        for (std::size_t i=0;i<objects_.size();++i) result.append(py::make_tuple(objects_[i],records_[i]));
        return result;
    }
    py::dict drawingBytes() const {
        py::dict result;
        for (const auto& [name,bytes] : drawingBytes_) result[py::str(name)]=py::bytes(bytes);
        return result;
    }
    std::vector<py::object> apply() {
        if (applied_) throw py::value_error("Entity update plan has already been applied");
        snapshot_->validateCurrent("entity update");
        applied_=true;
        for (std::size_t i=0;i<objects_.size();++i) objects_[i].attr("DXFData")=records_[i];
        return objects_;
    }
};

class ContourOperationPlan {
    std::unique_ptr<BatchObjectTransformPlan> dependencies_;
    std::unique_ptr<ContourObjectEditPlan> edit_;
    bool applied_=false;
public:
    ContourOperationPlan(const std::vector<py::object>& objects,const std::string& operation,
                         std::size_t targetIndex,const std::vector<double>& distances,double tolerance)
    {
        if (operation!="join" && operation!="close" && operation!="split" && operation!="break" && operation!="trim")
            throw py::value_error("Unknown contour operation");
        if (objects.empty() || targetIndex>=objects.size()) throw py::value_error("Select a valid target contour");
        if ((operation=="join" || operation=="trim") ? objects.size()<2 : objects.size()!=1)
            throw py::value_error("Select the required contours for this operation");
        for (std::size_t i=0;i<objects.size();++i)
            for (std::size_t j=0;j<i;++j)
                if (objects[i].equal(objects[j])) throw py::value_error("Select each contour only once");
        dependencies_=std::make_unique<BatchObjectTransformPlan>(objects,"move",Vector3D{},0,Vector3D{},true);
        auto target=objects[targetIndex];
        std::vector<std::string> records,raws;
        for (auto object : objects) raws.push_back(object.attr("DXFData").cast<std::string>());
        if (operation=="join" || operation=="close") {
            if (!distances.empty()) throw py::value_error("Join/close do not take cut distances");
            records={operation=="join" ? Contour::join(raws,tolerance) : Contour::close(raws[targetIndex],tolerance)};
        } else if (operation=="split") records=Contour::split(raws[targetIndex],distances);
        else if (operation=="break") {
            if (distances.size()!=2) throw py::value_error("Break requires two distances");
            records=Contour::breakBetween(raws[targetIndex],distances[0],distances[1]);
        } else {
            if (distances.size()!=1) throw py::value_error("Interior trim requires one pick distance");
            raws.erase(raws.begin()+targetIndex);
            records=Contour::trimInterior(target.attr("DXFData").cast<std::string>(),raws,distances[0]);
        }
        edit_=std::make_unique<ContourObjectEditPlan>(operation=="join" ? objects : std::vector<py::object>{target},
                                                    records,operation!="join");
    }
    py::object document() const { return edit_->document(); }
    py::object root() const { return edit_->root(); }
    py::object templateObject() const { return edit_->templateObject(); }
    bool preservesFirst() const { return edit_->preservesFirst(); }
    const std::vector<py::object>& createdObjects() const { return edit_->createdObjects(); }
    py::list previewRecords() const { return edit_->previewRecords(); }
    py::bytes drawingBytes() const { return edit_->drawingBytes(); }
    std::vector<py::object> apply() {
        if (applied_) throw py::value_error("Contour operation plan has already been applied");
        dependencies_->validateCurrent("contour operation");
        applied_=true;
        return edit_->apply();
    }
};

class TrimPlan {
    py::object target_,boundary_;
    std::unique_ptr<BatchObjectTransformPlan> dependencies_;
    std::unique_ptr<EntityUpdatePlan> update_;
    bool applied_=false;
public:
    TrimPlan(py::object target,py::object boundary,bool start,bool extend)
        : target_(target),boundary_(boundary)
    {
        if (target.equal(boundary)) throw py::value_error("Choose distinct trim target and boundary objects");
        dependencies_=std::make_unique<BatchObjectTransformPlan>(
            std::vector<py::object>{target,boundary},"move",Vector3D{},0,Vector3D{},true);
        auto raw=Drafting::trim(target.attr("DXFData").cast<std::string>(),
            boundary.attr("DXFData").cast<std::string>(),start,extend);
        update_=std::make_unique<EntityUpdatePlan>(std::vector<py::object>{target},std::vector<std::string>{raw});
    }
    py::object document() const { return update_->document(); }
    py::object target() const { return target_; }
    py::object boundary() const { return boundary_; }
    const std::vector<py::object>& objects() const { return update_->objects(); }
    py::list previewRecords() const { return update_->previewRecords(); }
    py::dict drawingBytes() const { return update_->drawingBytes(); }
    std::vector<py::object> apply() {
        if (applied_) throw py::value_error("Trim/extend plan has already been applied");
        dependencies_->validateCurrent("trim/extend");
        applied_=true;
        return update_->apply();
    }
};

class OffsetPlan {
    std::unique_ptr<EntityCopyPlan> copy_;
    std::unique_ptr<EntityUpdatePlan> update_;
public:
    OffsetPlan(const std::vector<py::object>& objects,double distance,bool keepOriginals)
    {
        // Validate and normalize the complete selection before calculating any offsets.
        BatchObjectTransformPlan selection(objects,"move",Vector3D{},0,Vector3D{},true);
        const auto selected=selection.objects().cast<std::vector<py::object>>();
        std::vector<std::string> records;
        records.reserve(selected.size());
        for (auto object : selected)
            records.push_back(Drafting::offset(object.attr("DXFData").cast<std::string>(),distance));
        if (keepOriginals) copy_=std::make_unique<EntityCopyPlan>(selected,records);
        else update_=std::make_unique<EntityUpdatePlan>(selected,records);
    }
    bool copies() const { return static_cast<bool>(copy_); }
    py::object document() const { return copy_ ? copy_->document() : update_->document(); }
    py::list objects() const {
        if (copy_) return copy_->objects();
        py::list result;
        for (auto object : update_->objects()) result.append(object);
        return result;
    }
    py::list previewRecords() const { return copy_ ? copy_->previewRecords() : update_->previewRecords(); }
    py::dict drawingBytes() const { return copy_ ? copy_->drawingBytes() : update_->drawingBytes(); }
    std::vector<py::object> apply() { return copy_ ? copy_->apply() : update_->apply(); }
};

// Shared read-only opening snapshot for native entity editors.
class EntityEditSnapshot {
    py::object object_,document_,root_;
    py::list snapshot_;
    std::string original_,name_,source_;
    py::list state() const {
        py::list result;
        for (auto handle : document_.attr("Objects")) {
            auto object=py::reinterpret_borrow<py::object>(handle);
            if (root_.is_none() ? !object.equal(object_) :
                !root_.equal(py::getattr(object,"DXFSourceRoot",py::none()))) continue;
            if (!py::hasattr(object,"DXFData")) continue;
            result.append(py::make_tuple(object,object.attr("DXFData"),
                py::getattr(object,"DXFSourceIndex",py::int_(-1)),
                py::getattr(object,"DXFContext",py::str("")),
                py::getattr(object,"TextFont",py::str("")),object.attr("Placement").attr("toMatrix")().attr("A")));
        }
        return result;
    }
public:
    explicit EntityEditSnapshot(py::object object) : object_(object)
    {
        if (!py::hasattr(object,"DXFData")) throw py::value_error("Select one native DXF entity");
        original_=object.attr("DXFData").cast<std::string>();
        if (!object.attr("Placement").attr("isIdentity")().cast<bool>())
            throw py::value_error("Reset FreeCAD Placement before editing a native DXF entity");
        document_=object.attr("Document");
        name_=object.attr("Name").cast<std::string>();
        root_=py::getattr(object,"DXFSourceRoot",py::none());
        if (!root_.is_none()) {
            drawingSource(root_);
            source_=root_.attr("DXFSourceData").cast<std::string>();
        }
        // Opening/previewing is read-only; export eligibility is checked by prepare().
        snapshot_=state();
    }
    py::object object() const { return object_; }
    const std::string& originalData() const { return original_; }
    void validateCurrent() const
    {
        auto current=document_.attr("getObject")(name_);
        if (current.is_none() || !py::object(current).equal(object_)
            || !root_.equal(py::getattr(object_,"DXFSourceRoot",py::none()))
            || (!root_.is_none() && source_!=root_.attr("DXFSourceData").cast<std::string>())
            || !snapshot_.equal(state()))
            throw py::value_error("Entity or drawing changed; close and reopen the editor");
    }
};

class PolylineEditSession {
    EntityEditSnapshot snapshot_;
public:
    explicit PolylineEditSession(py::object object) : snapshot_(object) {
        readLWPolyline(snapshot_.originalData()).worldSegments();
    }
    py::object object() const { return snapshot_.object(); }
    const std::string& originalData() const { return snapshot_.originalData(); }
    LWPolyline polyline() const { return readLWPolyline(originalData()); }
    std::unique_ptr<EntityUpdatePlan> prepare(const LWPolyline& polyline,const std::vector<int>& origins) const {
        snapshot_.validateCurrent();
        auto raw=updateLWPolyline(originalData(),polyline,origins);
        return std::make_unique<EntityUpdatePlan>(std::vector<py::object>{object()},std::vector<std::string>{raw});
    }
};

class TextEditSession {
    EntityEditSnapshot snapshot_;
public:
    explicit TextEditSession(py::object object) : snapshot_(object) {
        Drafting::textInfo(snapshot_.originalData());
    }
    py::object object() const { return snapshot_.object(); }
    const std::string& originalData() const { return snapshot_.originalData(); }
    py::tuple textInfo() const {
        const auto info=Drafting::textInfo(originalData());
        return py::make_tuple(info.text,info.height,info.referenceWidth);
    }
    std::unique_ptr<EntityUpdatePlan> prepare(const std::string& content,double height,double width) const {
        snapshot_.validateCurrent();
        auto raw=Drafting::editText(originalData(),content,height,width);
        return std::make_unique<EntityUpdatePlan>(std::vector<py::object>{object()},std::vector<std::string>{raw});
    }
};

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

class CornerObjectEditPlan {
    py::object document_,root_;
    std::vector<py::object> objects_;
    std::unique_ptr<BatchObjectTransformPlan> snapshot_;
    std::unique_ptr<CornerEditPlan> domain_;
    bool applied_=false;
public:
    CornerObjectEditPlan(const std::vector<py::object>& objects,const std::vector<std::string>& updates,
                         const std::string& connector,const std::string& connectorType) : objects_(objects)
    {
        root_=cornerRoot(objects);
        document_=objects.front().attr("Document");
        snapshot_=std::make_unique<BatchObjectTransformPlan>(objects,"move",Vector3D{},0,Vector3D{},true);
        std::vector<std::pair<int,std::string>> live;
        std::array<std::size_t,2> selected{};
        for (auto handle : document_.attr("Objects")) {
            auto object=py::reinterpret_borrow<py::object>(handle);
            if (!root_.equal(py::getattr(object,"DXFSourceRoot",py::none())) || !py::hasattr(object,"DXFData")) continue;
            for (std::size_t i=0;i<2;++i) if (object.equal(objects[i])) selected[i]=live.size();
            live.emplace_back(object.attr("DXFSourceIndex").cast<int>(),object.attr("DXFData").cast<std::string>());
        }
        domain_=std::make_unique<CornerEditPlan>(drawingSource(root_),live,selected,updates,connector,connectorType);
        findLayerGroup(root_,parseEntity(domain_->connector())->layer());
    }
    py::object document() const { return document_; }
    py::object root() const { return root_; }
    const std::vector<py::object>& objects() const { return objects_; }
    py::list previewRecords() const {
        py::list result;
        for (std::size_t i=0;i<2;++i) result.append(py::make_tuple(objects_[i],domain_->updates()[i]));
        result.append(py::make_tuple(objects_[0],domain_->connector()));
        return result;
    }
    py::bytes drawingBytes() const { return py::bytes(domain_->drawingBytes()); }
    py::object apply()
    {
        if (applied_) throw py::value_error("Corner edit plan has already been applied");
        snapshot_->validateCurrent("corner edit");
        const auto entity=drawingSource(root_).resolveEntity(domain_->connector());
        auto group=findLayerGroup(root_,entity->layer());
        applied_=true;
        group=materializeLayerGroup(root_,entity->layer(),group);
        for (std::size_t i=0;i<2;++i) objects_[i].attr("DXFData")=domain_->updates()[i];
        py::object connector=createOwnedEntity(root_,entity,-1,"",group);
        return connector;
    }
};

// Population constructs persistent domain objects. Python supplies compatibility
// lifecycle adapters and GUI presentation afterwards, in the same transaction.
class DrawingPopulation {
public:
    DXFDocument source;
    py::object root;
    std::vector<py::object> entities;
    std::map<std::string,py::object> layerGroups;

    DrawingPopulation(py::object document,const DXFDocument& drawing,const std::string& sourceFile,
                      const std::string& label,const std::string& name,const std::string& font)
        : source(DXFDocument::parse(drawing.serialize()))
    {
        // Populate exactly the retained snapshot, including resolved block
        // contexts and supported-entity slots. Never synthesize source layers.
        if (source.entities.size()>static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw py::value_error("Too many DXF entities for persistent source indices");
        auto module=py::module_::import("DXFCAD");
        root=module.attr("create_drawing")(document,py::cast(source),sourceFile,label,name);
        entities.reserve(source.entities.size());
        for (std::size_t i=0;i<source.entities.size();++i) {
            const auto& entity=source.entities[i];
            const auto layer=entity->layer();
            if (!layerGroups.count(layer))
                layerGroups.emplace(layer,materializeLayerGroup(root,layer,py::none()));
            py::object object=createOwnedEntity(root,entity,static_cast<int>(i),font,layerGroups.at(layer));
            entities.push_back(object);
        }
    }
};

}

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
    py::class_<DXFViewportView>(module, "DXFViewportView")
        .def_readonly("center", &DXFViewportView::center)
        .def_readonly("x_axis", &DXFViewportView::xAxis)
        .def_readonly("y_axis", &DXFViewportView::yAxis)
        .def_readonly("normal", &DXFViewportView::normal)
        .def_readonly("height", &DXFViewportView::height);
    py::class_<DXFViewport>(module, "DXFViewport")
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
        .def("model_view", &DXFViewport::modelView);
    py::class_<DXFLayer>(module, "DXFLayer")
        .def_readonly("name", &DXFLayer::name).def_readonly("visible", &DXFLayer::visible)
        .def_readonly("color", &DXFLayer::color);
    py::class_<DXFSnapPoint>(module, "DXFSnapPoint")
        .def_readonly("position", &DXFSnapPoint::position)
        .def_readonly("kind", &DXFSnapPoint::kind);
    py::class_<BlockPart>(module,"BlockPart")
        .def_readonly("entity",&BlockPart::entity)
        .def_readonly("origin",&BlockPart::origin)
        .def_readonly("x_axis",&BlockPart::xAxis)
        .def_readonly("y_axis",&BlockPart::yAxis)
        .def_readonly("z_axis",&BlockPart::zAxis)
        .def("transform_point",&BlockPart::transformPoint);
    py::class_<DXFEntity, std::shared_ptr<DXFEntity>>(module, "DXFEntity")
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
    py::class_<DXFDocument>(module, "DXFDocument")
        .def_static("read", &DXFDocument::read)
        .def_static("parse", &DXFDocument::parse)
        .def_static("empty", &DXFDocument::empty, py::arg("insertion_units") = 0)
        .def("resolve_entity", &DXFDocument::resolveEntity, py::arg("raw"))
        .def("serialize", &DXFDocument::serialize, py::arg("replacements") = std::map<std::size_t, std::string>{},
             py::arg("removed") = std::vector<std::size_t>{}, py::arg("added") = std::vector<std::string>{})
        .def("serialize_bytes", [](const DXFDocument& doc) { return py::bytes(doc.serialize()); })
        .def("write", &DXFDocument::write, py::arg("path"),
             py::arg("replacements") = std::map<std::size_t, std::string>{},
             py::arg("removed") = std::vector<std::size_t>{}, py::arg("added") = std::vector<std::string>{})
        .def_readonly("entities", &DXFDocument::entities)
        .def_readonly("layers", &DXFDocument::layers)
        .def_readonly("warnings", &DXFDocument::warnings)
        .def_readonly("viewports", &DXFDocument::viewports)
        .def_readonly("insertion_units", &DXFDocument::insertionUnits);
    module.def("create_drawing",[](py::object document,const DXFDocument& drawing,
                                    const std::string& sourceFile,const std::string& label,const std::string& name) {
        drawing.serialize(); // Reject uninitialized models before document mutation.
#ifdef DXFCAD_NATIVE_OBJECTS
        auto root=document.attr("addObject")("DXFCAD::DXFDocumentObject",name);
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
    module.def("populate_drawing",[](py::object document,const DXFDocument& drawing,
        const std::string& sourceFile,const std::string& label,const std::string& name,const std::string& font) {
        return std::make_unique<DrawingPopulation>(document,drawing,sourceFile,label,name,font);
    },py::arg("document"),py::arg("drawing"),py::arg("source_file")="",py::arg("label")="DXF Drawing",
       py::arg("name")="DXFDrawing",py::arg("font_file")="",
       "Create the drawing root, referenced layer groups and owned entities. Use a transaction; Python initializes presentation/compatibility proxies and recomputes before committing.");
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
    module.def("current_drawing",[](py::object root) { return DXFDocument::parse(currentDrawingBytes(root)); },
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
    module.def("set_drawing_source",[](py::object root,const DXFDocument& drawing) {
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
    py::class_<BatchObjectTransformPlan>(module,"BatchTransformPlan")
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
    py::class_<EntityUpdatePlan>(module,"EntityUpdatePlan")
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
    py::class_<TrimPlan>(module,"TrimPlan")
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
    py::class_<OffsetPlan>(module,"OffsetPlan")
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
    module.def("prepare_polyline_edit",[](py::object object,const LWPolyline& polyline,const std::vector<int>& origins) {
        return PolylineEditSession(object).prepare(polyline,origins);
    },py::arg("entity"),py::arg("polyline"),py::arg("origins")=std::vector<int>{},
       "Prepare a validated LWPolyline replacement from the current object state.");
    py::class_<CornerObjectEditPlan>(module,"CornerEditPlan")
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
    py::class_<ContourObjectEditPlan>(module,"ContourEditPlan")
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
    py::class_<ContourOperationPlan>(module,"ContourOperationPlan")
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
    module.def("polyline_preview_shape", [](const LWPolyline& polyline) {
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
            entity.type() == "LWPOLYLINE" ? "DXFCAD::LWPolylineObject" : nullptr;
        if (type) {
            auto object = document.attr("addObject")(type, "DXF"+entity.type());
            object.attr("Label") = entity.type()+" "+entity.handle();
            object.attr("DXFData") = entity.raw();
            if (entity.type() == "TEXT" || entity.type() == "MTEXT" || entity.type() == "DIMENSION" || entity.type() == "INSERT") object.attr("TextFont") = font;
            if (entity.type() == "DIMENSION" || entity.type() == "INSERT") object.attr("DXFContext") = entity.context();
            return object;
        }
#endif
        auto object = document.attr("addObject")("Part::FeaturePython", "DXF"+entity.type());
        object.attr("Label") = entity.type()+" "+entity.handle();
        property(object, "DXFType", py::str(entity.type()));
        property(object, "DXFLayer", py::str(entity.layer()));
        property(object, "DXFHandle", py::str(entity.handle()));
        property(object, "DXFData", py::str(entity.raw()));
        if (entity.type() == "DIMENSION" || entity.type() == "INSERT") {
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
    module.def("recompute_feature", [](py::object object) {
#ifdef DXFCAD_NATIVE_OBJECTS
        if (object.attr("isDerivedFrom")("DXFCAD::DXFEntityObject").cast<bool>()) {
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
