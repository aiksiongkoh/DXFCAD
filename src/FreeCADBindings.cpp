#include <DXFCAD/DXFDocument.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <filesystem>
#include <set>

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
    if (geometry.text && !geometry.text->text.empty()) {
        const auto& t = *geometry.text;
        const auto font = std::filesystem::u8path(fontFile);
        if (!std::filesystem::is_regular_file(font)) throw std::runtime_error("Select an existing TrueType font for TEXT display");
        const auto letters = part.attr("makeWireString")(t.text, utf8Path(font.parent_path())+"/", utf8Path(font.filename()), t.height);
        py::list wires;
        for (const auto letter : letters) for (const auto wire : letter) wires.append(wire);
        if (py::len(wires)) {
            py::object textShape = part.attr("makeCompound")(wires);
            const auto app = py::module_::import("FreeCAD");
            if (t.widthFactor != 1) {
                auto scale = app.attr("Matrix")(); scale.attr("A11") = t.widthFactor;
                textShape = textShape.attr("transformGeometry")(scale);
            }
            const auto rotation = app.attr("Rotation")(vector(t.xAxis), vector(t.yAxis), vector(t.normal), "ZXY");
            textShape.attr("Placement") = app.attr("Placement")(vector(t.origin), rotation);
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
    if (object.attr("DXFType").cast<std::string>() == "LWPOLYLINE" && !py::hasattr(object, "ShowWidths")) {
        object.attr("addProperty")("App::PropertyBool", "ShowWidths", "Display",
            "Display butt-capped width regions; disable to show the centerline");
        object.attr("ShowWidths") = true;
    }
}
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
    py::class_<DXFLayer>(module, "DXFLayer")
        .def_readonly("name", &DXFLayer::name).def_readonly("visible", &DXFLayer::visible)
        .def_readonly("color", &DXFLayer::color);
    py::class_<DXFEntity, std::shared_ptr<DXFEntity>>(module, "DXFEntity")
        .def_property_readonly("type", &DXFEntity::type)
        .def_property_readonly("layer", &DXFEntity::layer)
        .def_property_readonly("handle", &DXFEntity::handle)
        .def_property_readonly("raw", &DXFEntity::raw)
        .def_property_readonly("visible", &entityVisible)
        .def("color", &entityColor)
        .def("width_regions", &DXFEntity::widthRegions, py::arg("tolerance") = 0.0001);
    py::class_<DXFDocument>(module, "DXFDocument")
        .def_static("read", &DXFDocument::read)
        .def_static("parse", &DXFDocument::parse)
        .def_readonly("entities", &DXFDocument::entities)
        .def_readonly("layers", &DXFDocument::layers)
        .def_readonly("warnings", &DXFDocument::warnings)
        .def_readonly("insertion_units", &DXFDocument::insertionUnits);
    module.def("parse_entity", &parseEntity);
    module.def("create_feature", [](py::object document, const DXFEntity& entity, const std::string& font) {
#ifdef DXFCAD_NATIVE_OBJECTS
        const auto type = entity.type() == "LINE" ? "DXFCAD::LineObject" :
            entity.type() == "ARC" ? "DXFCAD::ArcObject" :
            entity.type() == "CIRCLE" ? "DXFCAD::CircleObject" :
            entity.type() == "TEXT" ? "DXFCAD::TextObject" :
            entity.type() == "LWPOLYLINE" ? "DXFCAD::LWPolylineObject" : nullptr;
        if (type) {
            auto object = document.attr("addObject")(type, "DXF"+entity.type());
            object.attr("Label") = entity.type()+" "+entity.handle();
            object.attr("DXFData") = entity.raw();
            if (entity.type() == "TEXT") object.attr("TextFont") = font;
            return object;
        }
#endif
        auto object = document.attr("addObject")("Part::FeaturePython", "DXF"+entity.type());
        object.attr("Label") = entity.type()+" "+entity.handle();
        property(object, "DXFType", py::str(entity.type()));
        property(object, "DXFLayer", py::str(entity.layer()));
        property(object, "DXFHandle", py::str(entity.handle()));
        property(object, "DXFData", py::str(entity.raw()));
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
        if (object.attr("isDerivedFrom")("DXFCAD::DXFEntityObject").cast<bool>() ||
            object.attr("TypeId").cast<std::string>() == "DXFCAD::LWPolylineObject") {
            object.attr("recompute")();
            return;
        }
#endif
        try {
            const auto entity = parseEntity(object.attr("DXFData").cast<std::string>());
            ensureDisplayProperties(object);
            const bool showWidths = py::hasattr(object, "ShowWidths") && object.attr("ShowWidths").cast<bool>();
            object.attr("Shape") = shape(*entity, object.attr("TextFont").cast<std::string>(), showWidths);
            object.attr("DisplayStatus") = "Centerline";
            if (showWidths) object.attr("DisplayStatus") = "Widths (curved boundaries approximated)";
            if (entity->type() == "TEXT") object.attr("DisplayStatus") = "Text outlines (substitute font)";
        } catch (const std::exception& error) {
            object.attr("Shape") = py::module_::import("Part").attr("Shape")();
            object.attr("DisplayStatus") = std::string("Error: ")+error.what();
            throw;
        }
    });
}
