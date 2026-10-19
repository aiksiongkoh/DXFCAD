#pragma once

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

struct ExtrusionDirection {
    double x = 0.0;
    double y = 0.0;
    double z = 1.0;
};

struct Point2D {
    double x;
    double y;
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

struct Vector3D {
    double x;
    double y;
    double z;
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

// Native values are authoritative. Construction validates but never normalizes
// coordinates, bulges, widths, or the extrusion direction. Display geometry and
// DXF file serialization will be separate consumers of this data.
class LWPolyline {
public:
    // An empty polyline is allowed as the initial state of an editing workflow.
    LWPolyline() = default;
    explicit LWPolyline(std::vector<LWPolylineVertex> vertices,
                        bool closed = false,
                        double constantWidth = 0.0,
                        double elevation = 0.0,
                        ExtrusionDirection extrusion = {});

    const std::vector<LWPolylineVertex>& vertices() const noexcept;
    bool closed() const noexcept;
    double constantWidth() const noexcept;
    double elevation() const noexcept;
    const ExtrusionDirection& extrusion() const noexcept;

    // Derive the OCS centerline without modifying native data. Fewer than two
    // vertices produces no segments. Closed polylines include the closing edge.
    // Coincident straight endpoints are retained; a coincident bulged edge is
    // ambiguous and throws domain_error. Unrepresentable geometry throws
    // overflow_error. Elevation, extrusion, and width outlines are not applied.
    std::vector<LWPolylineSegment> segments() const;

    // Apply elevation and DXF's arbitrary-axis OCS frame to the centerline.
    // Only the derived frame is normalized; native extrusion is unchanged.
    // Uses the same topology and geometry-error policy as segments(), and
    // throws overflow_error if a transformed coordinate is not representable.
    std::vector<LWPolylineSegment3D> worldSegments() const;

    // Butt-capped width regions for each segment, in world coordinates. Curved
    // boundaries are approximated to tolerance in source units. Regions may
    // overlap and are to be united by the display geometry kernel. No miter or
    // round joins are added between segments. Zero-width/zero-length segments
    // produce no regions. Native values are never changed.
    std::vector<LWPolylineWidthRegion> widthRegions(double tolerance = 0.0001) const;

private:
    std::vector<LWPolylineVertex> vertices_;
    bool closed_ = false;
    double constantWidth_ = 0.0;
    double elevation_ = 0.0;
    ExtrusionDirection extrusion_;
};

} // namespace DXFCAD
