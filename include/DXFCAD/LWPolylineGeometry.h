#pragma once

#include <DXFCAD/Point2D.h>
#include <DXFCAD/Vector3D.h>

#include <optional>
#include <vector>

namespace DXFCAD {

// Coordinates are in the entity's object coordinate system (OCS).
struct LWPolylineVertex {
    double x = 0.0;
    double y = 0.0;
    double bulge = 0.0;
    // Widths and bulge describe the segment starting at this vertex.
    double startWidth = 0.0;
    double endWidth = 0.0;
    // Distinguish explicitly supplied zero variable widths from absent codes.
    bool hasWidths = false;
};

struct LWPolylineArc {
    Point2D center;
    Point2D midpoint; // A point halfway along the directed arc, including major arcs.
    double radius;
    double sweep; // Signed radians: positive is counterclockwise in OCS.
};

struct LWPolylineSegment {
    Point2D start;
    Point2D end;
    std::optional<LWPolylineArc> arc; // No arc means a straight segment.
};

struct LWPolylineArc3D {
    Vector3D center;
    Vector3D midpoint;
    Vector3D normal; // Unit extrusion; sweep remains signed relative to it.
    double radius;
    double sweep;
};

struct LWPolylineSegment3D {
    Vector3D start;
    Vector3D end;
    std::optional<LWPolylineArc3D> arc;
};

struct LWPolylineWidthRegion {
    std::size_t segmentIndex;
    std::vector<Vector3D> points; // Closed implicitly; vertices are not duplicated.
};

} // namespace DXFCAD
