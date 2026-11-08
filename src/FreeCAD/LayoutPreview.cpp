#include "LayoutPreview.h"
#include "ShapeGeometry.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace DXFCAD::FreeCAD {
namespace {
std::vector<Vector3D> discretize(py::object curve,double tolerance) {
    std::vector<Vector3D> result;
    for (const auto p : curve.attr("discretize")(py::arg("Deflection")=tolerance))
        result.push_back({p.attr("x").cast<double>(),p.attr("y").cast<double>(),p.attr("z").cast<double>()});
    return result;
}
std::vector<LayoutPath> sampleShape(py::object geometry,const std::vector<double>& color,double tolerance) {
    std::vector<LayoutPath> result;
    std::vector<py::object> faceEdges;
    for (const auto item : geometry.attr("Faces")) {
        const auto face=py::reinterpret_borrow<py::object>(item);
        LayoutPath path{{},color,true};
        for (const auto wire : face.attr("Wires"))
            path.contours.push_back(discretize(py::reinterpret_borrow<py::object>(wire),tolerance));
        result.push_back(std::move(path));
        for (const auto edge : face.attr("Edges")) faceEdges.push_back(py::reinterpret_borrow<py::object>(edge));
    }
    for (const auto item : geometry.attr("Edges")) {
        const auto edge=py::reinterpret_borrow<py::object>(item);
        if (std::any_of(faceEdges.begin(),faceEdges.end(),[&](const py::object& boundary) {
            return edge.attr("isSame")(boundary).cast<bool>();
        })) continue;
        result.push_back({{discretize(edge,tolerance)},color,false});
    }
    return result;
}
std::vector<LayoutPath> sample(const DXFEntity& entity,const Drawing& drawing,const std::string& font,double tolerance) {
    const auto layer=drawing.layers.find(entity.layer());
    const auto& style=layer==drawing.layers.end() ? drawing.layers.at("0") : layer->second;
    if (!style.visible || !entityVisible(entity)) return {};
    const auto geometry=shape(entity,font,true);
    if (entity.type()!="INSERT") return sampleShape(geometry,entityColor(entity,style),tolerance);
    std::vector<LayoutPath> result;
    for (const auto& [child,color] : blockShapes(entity,geometry,font)) {
        auto paths=sampleShape(child,color,tolerance);
        result.insert(result.end(),paths.begin(),paths.end());
    }
    return result;
}
}
LayoutPreview::LayoutPreview(const Drawing& drawing,const std::string& name,const std::string& font,double tolerance) {
    if (!std::isfinite(tolerance) || tolerance<=0)
        throw std::invalid_argument("Layout curve tolerance must be positive and finite");
    const auto found=std::find_if(drawing.layouts.begin(),drawing.layouts.end(),[&](const DXFLayout& layout) {
        return layout.name()==name;
    });
    if (found==drawing.layouts.end()) throw std::invalid_argument("Drawing has no layout named "+name);
    limits=found->limits();
    // Include parser diagnostics so unsupported sheet entities cannot disappear
    // silently. The source records remain authoritative and exportable.
    warnings=drawing.warnings;
    double maxScale=0;
    for (const auto& viewport : found->viewports())
        if (viewport.navigationIssue().empty()) maxScale=std::max(maxScale,viewport.scale());
    std::vector<std::pair<std::string,std::vector<LayoutPath>>> model;
    if (maxScale>0) for (const auto& entity : drawing.entities) {
        try { model.emplace_back(entity->layer(),sample(*entity,drawing,font,tolerance/maxScale)); }
        catch (const std::exception& error) {
            warnings.push_back(entity->type()+" "+entity->handle()+": "+error.what());
        }
    }
    for (const auto& viewport : found->viewports()) {
        if (viewport.id()==1) continue; // Overall paper-space view, not a model window.
        const auto issue=viewport.navigationIssue();
        if (!issue.empty()) continue; // Parser already reported the reason.
        // The viewport's own layer can be off to hide its border while its
        // model window remains visible. Its display status governs content.
        const auto bounds=viewport.paperBounds();
        viewports.push_back(bounds);
        const PaperClip clip(bounds);
        const ViewportProjection projection(viewport);
        for (const auto& entry : model) {
            if (viewport.freezesLayer(entry.first)) continue;
            for (const auto& path : entry.second) {
                LayoutPath paper{{},path.color,path.filled};
                for (const auto& contour : path.contours) {
                    std::vector<Vector3D> points;
                    for (const auto& p : contour) points.push_back(projection.project(p));
                    if (path.filled) {
                        auto polygon=clip.polygon(points);
                        if (polygon.size()>2) paper.contours.push_back(std::move(polygon));
                    } else {
                        auto segments=clip.polyline(points);
                        paper.contours.insert(paper.contours.end(),segments.begin(),segments.end());
                    }
                }
                if (!paper.contours.empty()) paths.push_back(std::move(paper));
            }
        }
    }
    // Paper-space annotations overlay the model windows at their native size.
    for (const auto& entity : found->entities()) {
        try {
            auto sampled=sample(*entity,drawing,font,tolerance);
            for (auto& path : sampled) {
                for (auto& contour : path.contours) for (auto& p : contour) p.z=0;
                paths.push_back(std::move(path));
            }
        } catch (const std::exception& error) {
            warnings.push_back(entity->type()+" "+entity->handle()+": "+error.what());
        }
    }
}
} // namespace DXFCAD::FreeCAD
