#pragma once

#include <DXFCAD/LWPolyline.h>
#include <map>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace DXFCAD {

class DXFEntity;
// Columns of a full affine block-to-world transform. Curves stay analytic in
// their original coordinates until the FreeCAD adapter applies this transform.
struct BlockPart {
    std::shared_ptr<DXFEntity> entity;
    Vector3D origin, xAxis, yAxis, zAxis;
    Vector3D transformPoint(const Vector3D& point) const;
};

// All entity group codes, including unknown codes and XDATA, remain authoritative.
class DXFRecord {
public:
    std::vector<std::pair<int, std::string>> groups;
    std::string value(int code, const std::string& fallback = "") const;
    double number(int code, double fallback = 0) const;
    int integer(int code, int fallback = 0) const;
    std::string raw() const;
};

struct CircleGeometry { Vector3D center; Vector3D normal; double radius; };
struct TextBounds { double minX = 0, minY = 0, maxX = 0, maxY = 0; };
struct TextGeometry {
    std::string text;
    Vector3D origin, xAxis, yAxis, normal;
    double height, widthFactor;
    int horizontalAlignment = 0, verticalAlignment = 0;
    bool multiline = false;
    double referenceWidth = 0, lineSpacing = 1;
    int attachment = 1;
    // Align substitute-font outline bounds locally before width/OCS transforms.
    Vector3D alignedOrigin(double minX, double minY, double maxX, double maxY) const;
    // Font measurement belongs to the rendering adapter; wrapping/placement
    // belong to the C++ domain. Returned runs have absolute WCS origins.
    std::vector<TextGeometry> layout(const std::function<TextBounds(const std::string&)>& measure) const;
};
struct EntityGeometry {
    std::vector<LWPolylineSegment3D> segments;
    std::vector<CircleGeometry> circles;
    std::optional<TextGeometry> text;
    std::vector<TextGeometry> texts;
    std::vector<std::vector<Vector3D>> faces;
    std::vector<Vector3D> bounds;
};
struct DXFSnapPoint {
    Vector3D position;
    std::string kind;
};

class DXFEntity {
public:
    explicit DXFEntity(DXFRecord record) : record_(std::move(record)) {}
    virtual ~DXFEntity() = default;
    std::string type() const { return record_.value(0); }
    std::string layer() const { return record_.value(8, "0"); }
    std::string handle() const { return record_.value(5); }
    std::string raw() const { return record_.raw(); }
    const DXFRecord& record() const noexcept { return record_; }
    virtual EntityGeometry geometry() const = 0;
    std::string mirrored(double lineDegrees, const Vector3D& linePoint) const;
    std::string scaled(double factor, const Vector3D& pivot) const;
    std::string rotated(double degrees, const Vector3D& pivot) const;
    std::string copied(const Vector3D& offset) const;
    virtual std::string translated(const Vector3D& offset) const;
    virtual std::string context() const { return {}; }
    virtual std::vector<BlockPart> blockParts() const { return {}; }
    virtual std::vector<DXFSnapPoint> snapPoints() const;
    virtual std::vector<LWPolylineWidthRegion> widthRegions(double = 0.0001) const { return {}; }
protected:
    std::string translatedPoints(const Vector3D& offset, const std::vector<int>& codes, bool ocs) const;
    DXFRecord record_;
};

struct DXFLayer {
    std::string name;
    int color = 7;
    int trueColor = -1;
    bool visible = true;
};

struct DXFViewportView {
    Vector3D center, xAxis, yAxis, normal;
    double height;
};

// Paper-space metadata stays separate from editable model-space entity slots.
class DXFViewport {
public:
    explicit DXFViewport(DXFRecord record, std::string layout = "",
                         const std::map<std::string,std::string>& layerHandles = {});
    const std::string& layout() const noexcept { return layout_; }
    std::string handle() const { return record_.value(5); }
    std::string raw() const { return record_.raw(); }
    int id() const;
    double paperWidth() const;
    double paperHeight() const;
    double viewHeight() const;
    double scale() const;
    const std::vector<std::string>& frozenLayers() const noexcept { return frozenLayers_; }
    bool freezesLayer(const std::string& name) const;
    std::string navigationIssue() const;
    DXFViewportView modelView() const;
private:
    DXFRecord record_;
    std::string layout_;
    std::vector<std::string> frozenLayers_;
    std::string frozenLayerIssue_;
};

class DXFDocument {
public:
    std::vector<std::shared_ptr<DXFEntity>> entities;
    std::map<std::string, DXFLayer> layers;
    std::vector<std::string> warnings;
    std::vector<DXFViewport> viewports;
    int insertionUnits = 0; // No implicit scaling when units are absent.
    static DXFDocument read(const std::string& path);
    static DXFDocument parse(const std::string& ascii);
    std::shared_ptr<DXFEntity> resolveEntity(const std::string& raw) const;
    // Supported entity indices are stable slots in the original drawing.
    // Unknown/invalid entities and unmodeled sections are retained verbatim as
    // group values. Serialization normalizes group-code lines and line endings.
    std::string serialize(const std::map<std::size_t, std::string>& replacements = {},
                          const std::vector<std::size_t>& removed = {},
                          const std::vector<std::string>& added = {}) const;
    void write(const std::string& path,
               const std::map<std::size_t, std::string>& replacements = {},
               const std::vector<std::size_t>& removed = {},
               const std::vector<std::string>& added = {}) const;
    static DXFDocument empty(int insertionUnits = 0);
    // Live source slots: -1 denotes a new entity; absent original slots are deleted.
    std::string serializeCurrent(const std::vector<std::pair<int, std::string>>& records) const;
private:
    DXFRecord source_;
    std::vector<std::pair<std::size_t, std::size_t>> slots_;
    std::size_t entitiesEnd_ = 0;
};

std::shared_ptr<DXFEntity> parseEntity(const std::string& raw, const std::string& context = "");
LWPolyline readLWPolyline(const std::string& raw);
// origins maps each new vertex to its original index, or -1 for a new vertex.
// This retains native vertex IDs when inserting, deleting, or reordering.
std::string updateLWPolyline(const std::string& raw, const LWPolyline& polyline,
                             const std::vector<int>& origins = {});
std::string createLWPolyline(const LWPolyline& polyline, const std::string& layer = "0");
std::string createRectangle(const Vector3D& first, const Vector3D& opposite, const std::string& layer = "0");
std::string createLine(const Vector3D& start, const Vector3D& end, const std::string& layer = "0");
std::string createCircle(const Vector3D& center, double radius, const std::string& layer = "0");
// Numeric angles are WCS XY directions in degrees; the center is WCS.
std::string createArc(const Vector3D& center, double radius, double startDegrees, double endDegrees,
                      bool clockwise = false, const std::string& layer = "0");
// Ordered WCS points define the plane and the directed arc through the middle point.
std::string createArcThroughPoints(const Vector3D& start, const Vector3D& through, const Vector3D& end,
                                   const std::string& layer = "0");
double scaleFactor(const Vector3D& center, const Vector3D& target, double referenceLength);
double rotationAngle(const Vector3D& center, const Vector3D& direction);
Vector3D displacement(const Vector3D& start, const Vector3D& end);
double circleRadius(const Vector3D& center, const Vector3D& circumference);
Vector3D pointOnXYPlane(const Vector3D& rayStart, const Vector3D& rayEnd, double elevation = 0);
Vector3D pointInXYPlane(const Vector3D& point, double elevation = 0);
// Resolve ACI/true color and layer visibility in the domain layer.
std::vector<double> entityColor(const DXFEntity& entity, const DXFLayer& layer);
bool entityVisible(const DXFEntity& entity);

} // namespace DXFCAD
