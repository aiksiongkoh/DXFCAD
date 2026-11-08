#pragma once

#include <DXFCAD/Point2D.h>
#include <DXFCAD/Vector3D.h>

namespace DXFCAD {

// Right-handed orthonormal Cartesian frame through the world origin.
// The supplied normal fixes Z. A deterministic arbitrary-axis convention fixes
// X and Y, matching existing DXF OCS mappings. The input vector is not modified.
class CartesianFrame {
public:
    explicit CartesianFrame(const Vector3D& normal);
    const Vector3D& normal() const noexcept { return normal_; }
    const Vector3D& xAxis() const noexcept { return xAxis_; }
    const Vector3D& yAxis() const noexcept { return yAxis_; }
    Vector3D toWorld(const Point2D& point, double elevation) const;
    Vector3D fromWorld(const Vector3D& point) const;
    Point2D intersectLine(const Vector3D& start, const Vector3D& end, double elevation) const;

private:
    Vector3D normal_;
    Vector3D xAxis_;
    Vector3D yAxis_;
};

} // namespace DXFCAD
