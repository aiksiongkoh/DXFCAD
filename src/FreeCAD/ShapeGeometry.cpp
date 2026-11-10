#include "ShapeGeometry.h"
#include "../WidthBoundary.h"
#include <DXFCAD/CartesianFrame.h>
#include <DXFCAD/ParametricCurves.h>

#include <cmath>
#include <filesystem>
#include <set>
#include <stdexcept>

namespace DXFCAD::FreeCAD {

namespace {
std::string utf8Path(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
} // namespace

py::object vector(const Vector3D& p) {
    return py::module_::import("FreeCAD").attr("Vector")(p.x, p.y, p.z);
}
std::vector<std::pair<py::object,std::vector<double>>> blockShapes(
    const DXFEntity& entity,py::object geometry,const std::string& fontFile)
{
    std::vector<std::pair<py::object,std::vector<double>>> result;
    if (geometry.attr("isNull")().cast<bool>()) return result;
    const py::list children=geometry.attr("childShapes")();
    std::size_t index=0;
    for (const auto& part : entity.blockParts()) {
        if (!part.visible || shape(*part.entity,fontFile,true).attr("isNull")().cast<bool>()) continue;
        if (index>=static_cast<std::size_t>(py::len(children)))
            throw std::runtime_error("INSERT display parts do not match the derived shape");
        result.emplace_back(py::reinterpret_borrow<py::object>(children[index++]),part.color);
    }
    if (index!=static_cast<std::size_t>(py::len(children)))
        throw std::runtime_error("INSERT display parts do not match the derived shape");
    return result;
}
py::object shape(const DXFEntity& entity, const std::string& fontFile, bool showWidths)
{
    const auto geometry = entity.geometry();
    const auto part = py::module_::import("Part");
    if (entity.type() == "ARC") {
        // Native center/radius/angles remain well-defined when the endpoints
        // nearly coincide; fitting a circle through three points does not.
        const auto& arc = *geometry.segments.at(0).arc;
        const CartesianFrame frame(arc.normal);
        const auto curve = part.attr("Circle")(vector(arc.center), vector(arc.normal), arc.radius);
        curve.attr("XAxis") = vector(frame.xAxis());
        const double start = entity.record().number(50) * std::acos(-1.0) / 180.0;
        return curve.attr("toShape")(start, start + arc.sweep);
    }
    if (const auto* ellipse=dynamic_cast<const DXFEllipse*>(&entity)) {
        const auto c=ellipse->center(),a=ellipse->majorAxis(),b=ellipse->minorAxis();
        const auto curve=part.attr("Ellipse")(vector({c.x+a.x,c.y+a.y,c.z+a.z}),vector({c.x+b.x,c.y+b.y,c.z+b.z}),vector(c));
        return curve.attr("toShape")(ellipse->startParameter(),ellipse->endParameter());
    }
    if (const auto* spline=dynamic_cast<const DXFSpline*>(&entity)) {
        py::list poles,knots,multiplicities,weights;
        for (const auto p : spline->controlPoints()) poles.append(vector(p));
        for (const auto w : spline->weights()) weights.append(w);
        std::vector<double> unique; std::vector<int> counts;
        for (const auto k : spline->knots()) {
            if (unique.empty() || unique.back()!=k) { unique.push_back(k); counts.push_back(1); }
            else ++counts.back();
        }
        for (const auto k : unique) knots.append(k);
        for (const auto m : counts) multiplicities.append(m);
        const auto curve=part.attr("BSplineCurve")();
        // DXF supplies the full knot vector (pole count + degree + 1),
        // including expanded periodic control nets. OCC's periodic
        // constructor expects a compact cyclic representation instead.
        curve.attr("buildFromPolesMultsKnots")(poles,multiplicities,knots,false,spline->degree(),weights);
        if (spline->periodic()) curve.attr("setPeriodic")();
        return curve.attr("toShape")(spline->startParameter(),spline->endParameter());
    }
    py::list edges;
    for (const auto& instance : entity.blockParts()) {
        if (!instance.visible) continue;
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
        const double tolerance = part.attr("Precision").attr("confusion")().cast<double>();
        for (const auto& region : entity.widthRegions()) {
            if (Detail::collapsedWidthBoundary(region, tolerance)) {
                covered.insert(region.segmentIndex);
                continue;
            }
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

} // namespace DXFCAD::FreeCAD
