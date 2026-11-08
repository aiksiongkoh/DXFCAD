#pragma once

#include <DXFCAD/DXFEntity.h>

namespace DXFCAD {

// A native LWPOLYLINE entity: its DXF record is authoritative. Decoded values
// are private snapshots for geometry; edits return a new entity and preserve
// unrelated native fields, XDATA, and surviving vertex identities.
class DXFLWPolyline final : public DXFEntity {
public:
    // An empty polyline is allowed as the initial state of an editing workflow.
    DXFLWPolyline();
    explicit DXFLWPolyline(GroupSequence record);
    static DXFLWPolyline fromRaw(const std::string& raw);
    explicit DXFLWPolyline(std::vector<LWPolylineVertex> vertices,
                              bool closed = false,
                              double constantWidth = 0.0,
                              double elevation = 0.0,
                              Vector3D extrusion = {0, 0, 1});

    const std::vector<LWPolylineVertex>& vertices() const noexcept;
    bool closed() const noexcept;
    double constantWidth() const noexcept;
    double elevation() const noexcept;
    const Vector3D& extrusion() const noexcept;

    // Editing returns a validated value; the original remains unchanged on failure.
    DXFLWPolyline withVertex(std::size_t index, LWPolylineVertex vertex) const;
    DXFLWPolyline insertedVertex(std::size_t index, LWPolylineVertex vertex) const;
    DXFLWPolyline removedVertex(std::size_t index) const;
    DXFLWPolyline withProperties(bool closed, double constantWidth, double elevation,
                                    Vector3D extrusion) const;
    DXFLWPolyline moved(Vector3D offset) const; // WCS displacement
    DXFLWPolyline rotatedInPlane(double radians, Point2D pivot = {0, 0}) const; // OCS plane
    DXFLWPolyline scaledInPlane(double factor, Point2D pivot = {0, 0}) const; // uniform in OCS
    DXFLWPolyline mirroredInPlane(Point2D lineStart, Point2D lineEnd) const; // OCS line
    std::vector<Vector3D> worldVertices() const;
    // Project a viewport line onto this entity's plane, preserving all outgoing
    // segment attributes. These interactive edits also validate derived geometry.
    DXFLWPolyline withVertexOnLine(std::size_t index, Vector3D start, Vector3D end) const;
    DXFLWPolyline withVertexAtVertex(std::size_t index, std::size_t target) const;
    DXFLWPolyline withVertexAtWorldPoint(std::size_t index, Vector3D point,
                                          double planeTolerance = 1e-7) const;

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

    // Flat-capped width regions with bounded miter joins, in world coordinates. Curved
    // boundaries are approximated to tolerance in source units. Regions may
    // overlap and are to be united by the display geometry kernel. Miters
    // longer than ten half-widths use bevels. Zero-width/zero-length segments
    // produce no regions. Native values are never changed.
    std::vector<LWPolylineWidthRegion> widthRegions(double tolerance = 0.0001) const override;
    EntityGeometry geometry() const override;
    std::vector<SnapPoint> snapPoints() const override;
    std::string translated(const Vector3D& offset) const override;

    // Apply geometry to this record with explicit old-vertex origins when needed.
    // Validation of derived geometry remains an explicit caller decision.
    DXFLWPolyline withGeometry(const DXFLWPolyline& geometry,
                                 const std::vector<int>& origins = {}) const;

private:
    void validate() const;
    GroupSequence geometryRecord(const DXFLWPolyline& geometry,
                            const std::vector<int>& origins) const;
    std::vector<LWPolylineVertex> vertices_;
    bool closed_ = false;
    double constantWidth_ = 0.0;
    double elevation_ = 0.0;
    Vector3D extrusion_;
};

} // namespace DXFCAD
