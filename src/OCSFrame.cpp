#include <DXFCAD/OCSFrame.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace DXFCAD {
namespace {
Vector3D unit(Vector3D v)
{
    const double scale = std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)});
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || scale == 0) {
        throw std::invalid_argument("DXF extrusion must be finite and nonzero");
    }
    v = {v.x / scale, v.y / scale, v.z / scale};
    const double length = std::hypot(v.x, v.y, v.z);
    return {v.x / length, v.y / length, v.z / length};
}
Vector3D cross(const Vector3D& a, const Vector3D& b)
{
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
double coordinate(double x, double y, double z)
{
    const double scale = std::max({std::abs(x), std::abs(y), std::abs(z)});
    const double result = scale == 0 ? 0 : ((x/scale + y/scale) + z/scale)*scale;
    if (!std::isfinite(result)) throw std::overflow_error("DXF world coordinate is not representable");
    return result;
}
}

OCSFrame::OCSFrame(const ExtrusionDirection& extrusion)
    : normal_(unit({extrusion.x, extrusion.y, extrusion.z}))
{
    constexpr double threshold = 1.0 / 64.0;
    const Vector3D reference = std::abs(normal_.x) < threshold && std::abs(normal_.y) < threshold
        ? Vector3D{0, 1, 0} : Vector3D{0, 0, 1};
    xAxis_ = unit(cross(reference, normal_));
    yAxis_ = unit(cross(normal_, xAxis_));
}
Vector3D OCSFrame::toWorld(const Point2D& p, double elevation) const
{
    return {coordinate(xAxis_.x*p.x, yAxis_.x*p.y, normal_.x*elevation),
            coordinate(xAxis_.y*p.x, yAxis_.y*p.y, normal_.y*elevation),
            coordinate(xAxis_.z*p.x, yAxis_.z*p.y, normal_.z*elevation)};
}
}
