#include <DXFCAD/LWPolyline.h>

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
        throw std::invalid_argument("LWPolyline widths must be finite and nonnegative");
    }
}

} // namespace

LWPolyline::LWPolyline(std::vector<LWPolylineVertex> vertices,
                       bool closed,
                       double constantWidth,
                       double elevation,
                       ExtrusionDirection extrusion)
    : vertices_(std::move(vertices)), closed_(closed),
      constantWidth_(constantWidth), elevation_(elevation), extrusion_(extrusion)
{
    requireWidth(constantWidth_);
    requireFinite(elevation_, "LWPolyline elevation must be finite");
    requireFinite(extrusion_.x, "LWPolyline extrusion must be finite");
    requireFinite(extrusion_.y, "LWPolyline extrusion must be finite");
    requireFinite(extrusion_.z, "LWPolyline extrusion must be finite");
    if (extrusion_.x == 0.0 && extrusion_.y == 0.0 && extrusion_.z == 0.0) {
        throw std::invalid_argument("LWPolyline extrusion must be nonzero");
    }
    for (const auto& vertex : vertices_) {
        requireFinite(vertex.x, "LWPolyline vertex coordinates must be finite");
        requireFinite(vertex.y, "LWPolyline vertex coordinates must be finite");
        requireFinite(vertex.bulge, "LWPolyline bulge must be finite");
        requireWidth(vertex.startWidth);
        requireWidth(vertex.endWidth);
    }
}

const std::vector<LWPolylineVertex>& LWPolyline::vertices() const noexcept
{
    return vertices_;
}

bool LWPolyline::closed() const noexcept { return closed_; }
double LWPolyline::constantWidth() const noexcept { return constantWidth_; }
double LWPolyline::elevation() const noexcept { return elevation_; }
const ExtrusionDirection& LWPolyline::extrusion() const noexcept { return extrusion_; }

std::vector<LWPolylineSegment> LWPolyline::segments() const
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
                throw std::domain_error("Cannot derive an arc from coincident LWPolyline vertices");
            }
            if (!std::isfinite(chord)) {
                throw std::overflow_error("LWPolyline arc chord is not representable");
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
                throw std::overflow_error("LWPolyline arc geometry is not representable");
            }
            segment.arc = arc;
        }
        result.push_back(segment);
    }
    return result;
}

} // namespace DXFCAD
