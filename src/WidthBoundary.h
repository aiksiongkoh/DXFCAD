#pragma once

#include <DXFCAD/LWPolylineGeometry.h>
#include <algorithm>
#include <cmath>

namespace DXFCAD::Detail {

// A display kernel cannot form a face when every boundary point collapses
// onto one vertex at its linear tolerance. Native width geometry is retained.
inline bool collapsedWidthBoundary(const LWPolylineWidthRegion& region, double tolerance)
{
    if (region.points.empty()) return true;
    const auto& first = region.points.front();
    return std::all_of(region.points.begin(), region.points.end(), [&](const auto& point) {
        return std::hypot(point.x-first.x, point.y-first.y, point.z-first.z) <= tolerance;
    });
}

} // namespace DXFCAD::Detail
