#include <DXFCAD/OCSFrame.h>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace DXFCAD {
namespace {
void finite(double v) {
    if (!std::isfinite(v)) throw std::invalid_argument("Transform values must be finite");
}
void finite(Point2D p) { finite(p.x); finite(p.y); }
}

LWPolyline LWPolyline::withVertex(std::size_t index, LWPolylineVertex vertex) const
{
    auto result = vertices_;
    result.at(index) = vertex;
    return LWPolyline(std::move(result), closed_, constantWidth_, elevation_, extrusion_);
}
LWPolyline LWPolyline::insertedVertex(std::size_t index, LWPolylineVertex vertex) const
{
    if (index > vertices_.size()) throw std::out_of_range("Vertex insertion index");
    auto result = vertices_;
    result.insert(result.begin() + index, vertex);
    return LWPolyline(std::move(result), closed_, constantWidth_, elevation_, extrusion_);
}
LWPolyline LWPolyline::removedVertex(std::size_t index) const
{
    if (index >= vertices_.size()) throw std::out_of_range("Vertex removal index");
    auto result = vertices_;
    result.erase(result.begin() + index);
    return LWPolyline(std::move(result), closed_, constantWidth_, elevation_, extrusion_);
}
LWPolyline LWPolyline::withProperties(bool closed, double width, double elevation,
                                      ExtrusionDirection extrusion) const
{
    return LWPolyline(vertices_, closed, width, elevation, extrusion);
}
LWPolyline LWPolyline::translated(Vector3D offset) const
{
    finite(offset.x); finite(offset.y); finite(offset.z);
    const OCSFrame frame(extrusion_);
    auto dot = [&](Vector3D axis) { return offset.x*axis.x + offset.y*axis.y + offset.z*axis.z; };
    const double dx = dot(frame.xAxis()), dy = dot(frame.yAxis());
    auto result = vertices_;
    for (auto& v : result) { v.x += dx; v.y += dy; }
    return LWPolyline(std::move(result), closed_, constantWidth_, elevation_+dot(frame.normal()), extrusion_);
}
LWPolyline LWPolyline::rotated(double angle, Point2D pivot) const
{
    finite(angle); finite(pivot);
    const double c = std::cos(angle), s = std::sin(angle);
    auto result = vertices_;
    for (auto& v : result) {
        const double x = v.x-pivot.x, y = v.y-pivot.y;
        v.x = pivot.x+c*x-s*y; v.y = pivot.y+s*x+c*y;
    }
    return LWPolyline(std::move(result), closed_, constantWidth_, elevation_, extrusion_);
}
LWPolyline LWPolyline::scaled(double factor, Point2D pivot) const
{
    finite(factor); finite(pivot);
    if (factor == 0) throw std::invalid_argument("Scale factor must be nonzero");
    auto result = vertices_;
    for (auto& v : result) {
        v.x = pivot.x+(v.x-pivot.x)*factor; v.y = pivot.y+(v.y-pivot.y)*factor;
        v.startWidth *= std::abs(factor); v.endWidth *= std::abs(factor);
    }
    return LWPolyline(std::move(result), closed_, constantWidth_*std::abs(factor), elevation_, extrusion_);
}
LWPolyline LWPolyline::mirrored(Point2D start, Point2D end) const
{
    finite(start); finite(end);
    const double dx = end.x-start.x, dy = end.y-start.y, length = std::hypot(dx, dy);
    if (!std::isfinite(length) || length == 0) throw std::invalid_argument("Mirror line needs two distinct finite points");
    const double ux = dx/length, uy = dy/length;
    auto result = vertices_;
    for (auto& v : result) {
        const double x = v.x-start.x, y = v.y-start.y, projection = x*ux+y*uy;
        v.x = start.x+2*projection*ux-x; v.y = start.y+2*projection*uy-y;
        v.bulge = -v.bulge;
    }
    return LWPolyline(std::move(result), closed_, constantWidth_, elevation_, extrusion_);
}
std::vector<Vector3D> LWPolyline::worldVertices() const
{
    const OCSFrame frame(extrusion_);
    std::vector<Vector3D> result;
    result.reserve(vertices_.size());
    for (const auto& v : vertices_) result.push_back(frame.toWorld({v.x,v.y}, elevation_));
    return result;
}
LWPolyline LWPolyline::withVertexOnLine(std::size_t index, Vector3D start, Vector3D end) const
{
    auto vertex = vertices_.at(index);
    const auto point = OCSFrame(extrusion_).intersectLine(start, end, elevation_);
    vertex.x = point.x; vertex.y = point.y;
    auto result = withVertex(index, vertex);
    result.worldSegments();
    return result;
}
LWPolyline LWPolyline::withVertexAtVertex(std::size_t index, std::size_t target) const
{
    auto vertex = vertices_.at(index);
    const auto& destination = vertices_.at(target);
    vertex.x = destination.x; vertex.y = destination.y;
    auto result = withVertex(index, vertex);
    result.worldSegments();
    return result;
}
LWPolyline LWPolyline::withVertexAtWorldPoint(std::size_t index, Vector3D point, double planeTolerance) const
{
    auto vertex = vertices_.at(index);
    if (!std::isfinite(planeTolerance) || planeTolerance < 0)
        throw std::invalid_argument("Snap plane tolerance must be finite and nonnegative");
    const auto local = OCSFrame(extrusion_).fromWorld(point);
    if (std::abs(local.z-elevation_) > planeTolerance)
        throw std::domain_error("Snap target is outside the polyline plane");
    vertex.x = local.x; vertex.y = local.y;
    auto result = withVertex(index, vertex);
    result.worldSegments();
    return result;
}
} // namespace DXFCAD
