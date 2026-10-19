#pragma once

#include <DXFCAD/LWPolyline.h>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace DXFCAD {

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
struct TextGeometry {
    std::string text;
    Vector3D origin, xAxis, yAxis, normal;
    double height, widthFactor;
};
struct EntityGeometry {
    std::vector<LWPolylineSegment3D> segments;
    std::vector<CircleGeometry> circles;
    std::optional<TextGeometry> text;
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
    virtual std::vector<LWPolylineWidthRegion> widthRegions(double = 0.0001) const { return {}; }
protected:
    DXFRecord record_;
};

struct DXFLayer {
    std::string name;
    int color = 7;
    int trueColor = -1;
    bool visible = true;
};

class DXFDocument {
public:
    std::vector<std::shared_ptr<DXFEntity>> entities;
    std::map<std::string, DXFLayer> layers;
    std::vector<std::string> warnings;
    int insertionUnits = 0; // No implicit scaling when units are absent.
    static DXFDocument read(const std::string& path);
    static DXFDocument parse(const std::string& ascii);
};

std::shared_ptr<DXFEntity> parseEntity(const std::string& raw);
// Resolve ACI/true color and layer visibility in the domain layer.
std::vector<double> entityColor(const DXFEntity& entity, const DXFLayer& layer);
bool entityVisible(const DXFEntity& entity);

} // namespace DXFCAD
