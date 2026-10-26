#pragma once

#include <DXFCAD/DXFEntity.h>

namespace DXFCAD {

// A native LWPOLYLINE entity: its DXF record is authoritative. Decoded values
// are private snapshots for geometry; edits return a new entity and preserve
// unrelated native fields, XDATA, and surviving vertex identities.
class LWPolylineEntity final : public DXFEntity {
public:
    // An empty polyline is allowed as the initial state of an editing workflow.
    LWPolylineEntity();
    explicit LWPolylineEntity(DXFRecord record);
    static LWPolylineEntity fromRaw(const std::string& raw);
    explicit LWPolylineEntity(std::vector<LWPolylineVertex> vertices,
                              bool closed = false,
                              double constantWidth = 0.0,
                              double elevation = 0.0,
                              ExtrusionDirection extrusion = {});

    const std::vector<LWPolylineVertex>& vertices() const noexcept;
    bool closed() const noexcept;
    double constantWidth() const noexcept;
    double elevation() const noexcept;
    const ExtrusionDirection& extrusion() const noexcept;

    // Editing returns a validated value; the original remains unchanged on failure.
    LWPolylineEntity withVertex(std::size_t index, LWPolylineVertex vertex) const;
    LWPolylineEntity insertedVertex(std::size_t index, LWPolylineVertex vertex) const;
    LWPolylineEntity removedVertex(std::size_t index) const;
    LWPolylineEntity withProperties(bool closed, double constantWidth, double elevation,
                                    ExtrusionDirection extrusion) const;
    LWPolylineEntity moved(Vector3D offset) const; // WCS displacement
    LWPolylineEntity rotatedInPlane(double radians, Point2D pivot = {0, 0}) const; // OCS plane
    LWPolylineEntity scaledInPlane(double factor, Point2D pivot = {0, 0}) const; // uniform in OCS
    LWPolylineEntity mirroredInPlane(Point2D lineStart, Point2D lineEnd) const; // OCS line
    std::vector<Vector3D> worldVertices() const;
    // Project a viewport line onto this entity's plane, preserving all outgoing
    // segment attributes. These interactive edits also validate derived geometry.
    LWPolylineEntity withVertexOnLine(std::size_t index, Vector3D start, Vector3D end) const;
    LWPolylineEntity withVertexAtVertex(std::size_t index, std::size_t target) const;
    LWPolylineEntity withVertexAtWorldPoint(std::size_t index, Vector3D point,
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
    std::vector<DXFSnapPoint> snapPoints() const override;
    std::string translated(const Vector3D& offset) const override;

    // Apply geometry to this record with explicit old-vertex origins when needed.
    // Validation of derived geometry remains an explicit caller decision.
    LWPolylineEntity withGeometry(const LWPolylineEntity& geometry,
                                 const std::vector<int>& origins = {}) const;

private:
    void validate() const;
    DXFRecord geometryRecord(const LWPolylineEntity& geometry,
                            const std::vector<int>& origins) const;
    std::vector<LWPolylineVertex> vertices_;
    bool closed_ = false;
    double constantWidth_ = 0.0;
    double elevation_ = 0.0;
    ExtrusionDirection extrusion_;
};

} // namespace DXFCAD
