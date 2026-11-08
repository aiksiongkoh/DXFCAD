#include <DXFCAD/DXFLWPolyline.h>

#include <cmath>
#include <stdexcept>
#include <utility>

namespace DXFCAD {
namespace {

void requireFinite(double value, const char* message)
{
    if (!std::isfinite(value)) {
        throw std::invalid_argument(message);
    }
}

void requireWidth(double value)
{
    if (!std::isfinite(value) || value < 0.0) {
        throw std::invalid_argument("DXFLWPolyline widths must be finite and nonnegative");
    }
}

} // namespace

DXFLWPolyline::DXFLWPolyline(std::vector<LWPolylineVertex> vertices,
                                   bool closed,
                                   double constantWidth,
                                   double elevation,
                                   Vector3D extrusion)
    : DXFEntity({}), vertices_(std::move(vertices)), closed_(closed),
      constantWidth_(constantWidth), elevation_(elevation), extrusion_(extrusion)
{
    validate();
    record_ = geometryRecord(*this, std::vector<int>(vertices_.size(), -1));
    // Nonzero widths were emitted as explicit native fields in the new record.
    for (auto& vertex : vertices_)
        if (vertex.startWidth != 0 || vertex.endWidth != 0) vertex.hasWidths = true;
}

void DXFLWPolyline::validate() const
{
    requireWidth(constantWidth_);
    requireFinite(elevation_, "DXFLWPolyline elevation must be finite");
    requireFinite(extrusion_.x, "DXFLWPolyline extrusion must be finite");
    requireFinite(extrusion_.y, "DXFLWPolyline extrusion must be finite");
    requireFinite(extrusion_.z, "DXFLWPolyline extrusion must be finite");
    if (extrusion_.x == 0.0 && extrusion_.y == 0.0 && extrusion_.z == 0.0) {
        throw std::invalid_argument("DXFLWPolyline extrusion must be nonzero");
    }
    for (const auto& vertex : vertices_) {
        requireFinite(vertex.x, "DXFLWPolyline vertex coordinates must be finite");
        requireFinite(vertex.y, "DXFLWPolyline vertex coordinates must be finite");
        requireFinite(vertex.bulge, "DXFLWPolyline bulge must be finite");
        requireWidth(vertex.startWidth);
        requireWidth(vertex.endWidth);
    }
}

const std::vector<LWPolylineVertex>& DXFLWPolyline::vertices() const noexcept
{
    return vertices_;
}

bool DXFLWPolyline::closed() const noexcept { return closed_; }
double DXFLWPolyline::constantWidth() const noexcept { return constantWidth_; }
double DXFLWPolyline::elevation() const noexcept { return elevation_; }
const Vector3D& DXFLWPolyline::extrusion() const noexcept { return extrusion_; }

std::vector<LWPolylineSegment> DXFLWPolyline::segments() const
{
    std::vector<LWPolylineSegment> result;
    if (vertices_.size() < 2) {
        return result;
    }
    const auto count = closed_ ? vertices_.size() : vertices_.size() - 1;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& start = vertices_[i];
        const auto& end = vertices_[(i + 1) % vertices_.size()];
        LWPolylineSegment segment{{start.x, start.y}, {end.x, end.y}, std::nullopt};
        if (start.bulge != 0.0) {
            const double dx = end.x - start.x;
            const double dy = end.y - start.y;
            const double chord = std::hypot(dx, dy);
            if (chord == 0.0) {
                throw std::domain_error("Cannot derive an arc from coincident DXFLWPolyline vertices");
            }
            if (!std::isfinite(chord)) {
                throw std::overflow_error("DXFLWPolyline arc chord is not representable");
            }
            const double nx = -dy / chord;
            const double ny = dx / chord;
            const double mx = start.x / 2.0 + end.x / 2.0;
            const double my = start.y / 2.0 + end.y / 2.0;
            // b = tan(sweep/4). Split the reciprocal and product terms to
            // avoid squaring b, which overflows unnecessarily for major arcs.
            const double quarterChord = chord / 4.0;
            const double reciprocal = quarterChord / start.bulge;
            const double product = quarterChord * start.bulge;
            const double offset = reciprocal - product;
            const double sagitta = 2.0 * product;
            const double radius = std::abs(reciprocal) + std::abs(product);
            const double sweep = 4.0 * std::atan(start.bulge);
            LWPolylineArc arc{{mx + nx * offset, my + ny * offset},
                              {mx - nx * sagitta, my - ny * sagitta}, radius, sweep};
            const double fullCircle = 8.0 * std::atan(1.0);
            if (!std::isfinite(arc.center.x) || !std::isfinite(arc.center.y)
                || !std::isfinite(arc.midpoint.x) || !std::isfinite(arc.midpoint.y)
                || !std::isfinite(radius) || radius <= 0.0 || sweep == 0.0
                || std::abs(sweep) >= fullCircle) {
                throw std::overflow_error("DXFLWPolyline arc geometry is not representable");
            }
            segment.arc = arc;
        }
        result.push_back(segment);
    }
    return result;
}

} // namespace DXFCAD
