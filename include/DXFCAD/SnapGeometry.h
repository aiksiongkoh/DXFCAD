#pragma once
#include <DXFCAD/DXFDocument.h>

namespace DXFCAD {
// A derived, immutable centerline snapshot. Placement is a proper rigid frame;
// native entity data and document/display geometry are never changed.
class SnapGeometry {
public:
    explicit SnapGeometry(const DXFEntity& entity, Vector3D origin = {0,0,0},
                          Vector3D xAxis = {1,0,0}, Vector3D yAxis = {0,1,0},
                          Vector3D zAxis = {0,0,1});
    static std::vector<DXFSnapPoint> midpoints(const EntityGeometry& geometry);
    // Finite, coplanar centerline intersections; no apparent or extended hits.
    std::vector<DXFSnapPoint> intersections(const SnapGeometry& other) const;
    // Conservative world AABB corners for viewport candidate filtering.
    std::vector<Vector3D> boundPoints() const;
private:
    EntityGeometry geometry_;
};
}
