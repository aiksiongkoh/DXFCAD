#pragma once

#include <DXFCAD/Vector3D.h>
#include <vector>

namespace DXFCAD {
class DXFViewport;

// Derived sheet geometry, in native paper-space coordinates (not plot pixels).
struct PaperRectangle {
    double minX, minY, maxX, maxY;
    bool contains(const Vector3D& point) const;
};

class ViewportProjection {
public:
    explicit ViewportProjection(const DXFViewport& viewport);
    Vector3D project(const Vector3D& point) const;
private:
    Vector3D modelCenter_,paperCenter_,xAxis_,yAxis_;
    double scale_;
};

class PaperClip {
public:
    explicit PaperClip(PaperRectangle rectangle);
    std::vector<std::vector<Vector3D>> polyline(const std::vector<Vector3D>& points) const;
    std::vector<Vector3D> polygon(const std::vector<Vector3D>& points) const;
private:
    PaperRectangle rectangle_;
};

} // namespace DXFCAD
