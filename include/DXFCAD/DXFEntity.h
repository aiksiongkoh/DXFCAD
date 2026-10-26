#pragma once

#include <DXFCAD/LWPolylineGeometry.h>
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

} // namespace DXFCAD
