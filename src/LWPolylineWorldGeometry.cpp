#include <DXFCAD/OCSFrame.h>

namespace DXFCAD {

std::vector<LWPolylineSegment3D> LWPolyline::worldSegments() const
{
    const auto local = segments();
    const OCSFrame frame(extrusion_);
    std::vector<LWPolylineSegment3D> result;
    result.reserve(local.size());
    for (const auto& segment : local) {
        LWPolylineSegment3D world{frame.toWorld(segment.start, elevation_),
                                 frame.toWorld(segment.end, elevation_), std::nullopt};
        if (segment.arc) {
            const auto& arc = *segment.arc;
            world.arc = LWPolylineArc3D{frame.toWorld(arc.center, elevation_),
                                       frame.toWorld(arc.midpoint, elevation_),
                                       frame.normal(), arc.radius, arc.sweep};
        }
        result.push_back(world);
    }
    return result;
}

} // namespace DXFCAD
