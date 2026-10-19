#pragma once

#include <DXFCAD/LWPolyline.h>

namespace DXFCAD {

// DXF's arbitrary-axis frame, shared by native entity geometry consumers.
class OCSFrame {
public:
    explicit OCSFrame(const ExtrusionDirection& extrusion);
    const Vector3D& normal() const noexcept { return normal_; }
    const Vector3D& xAxis() const noexcept { return xAxis_; }
    const Vector3D& yAxis() const noexcept { return yAxis_; }
    Vector3D toWorld(const Point2D& point, double elevation) const;

private:
    Vector3D normal_;
    Vector3D xAxis_;
    Vector3D yAxis_;
};

} // namespace DXFCAD
