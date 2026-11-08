#pragma once

#include <DXFCAD/DXFLWPolyline.h>
#include <DXFCAD/PaperGeometry.h>
#include <map>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace DXFCAD {

struct DXFLayer : public DXFItem {
    DXFLayer(std::string nameValue = "", int colorValue = 7, int trueColorValue = -1,
             bool visibleValue = true)
        : name(std::move(nameValue)), color(colorValue), trueColor(trueColorValue), visible(visibleValue) {}

    std::string name;
    int color = 7;
    int trueColor = -1;
    bool visible = true;
};

struct ViewFrame {
    Vector3D center, xAxis, yAxis, normal;
    double height;
};

// Paper-space metadata stays separate from editable model-space entity slots.
class DXFViewport : public DXFItem {
public:
    explicit DXFViewport(GroupSequence record, std::string layout = "",
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
    ViewFrame modelView() const;
    Vector3D paperCenter() const;
    PaperRectangle paperBounds() const;
    Vector3D modelToPaper(const Vector3D& point) const;
private:
    GroupSequence record_;
    std::string layout_;
    std::vector<std::string> frozenLayers_;
    std::string frozenLayerIssue_;
};

// A native LAYOUT object, with its retained paper-space entities and viewports.
// Older files can have an inferred layout without a LAYOUT object (raw is empty).
class DXFLayout : public DXFItem {
public:
    explicit DXFLayout(GroupSequence record = {}, std::string fallbackName = "Paper space");
    const std::string& name() const noexcept { return name_; }
    std::string raw() const { return record_.raw(); }
    int tabOrder() const noexcept { return tabOrder_; }
    std::optional<PaperRectangle> limits() const;
    const std::vector<std::shared_ptr<DXFEntity>>& entities() const noexcept { return entities_; }
    const std::vector<DXFViewport>& viewports() const noexcept { return viewports_; }
private:
    friend class Drawing;
    GroupSequence record_;
    std::string name_;
    int tabOrder_ = 0;
    std::vector<std::shared_ptr<DXFEntity>> entities_;
    std::vector<DXFViewport> viewports_;
};

class Drawing {
public:
    std::vector<std::shared_ptr<DXFEntity>> entities;
    std::map<std::string, DXFLayer> layers;
    std::vector<std::string> warnings;
    std::vector<DXFViewport> viewports;
    std::vector<DXFLayout> layouts;
    int insertionUnits = 0; // No implicit scaling when units are absent.
    static Drawing read(const std::string& path);
    static Drawing parse(const std::string& ascii);
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
    static Drawing empty(int insertionUnits = 0);
    // Live source slots: -1 denotes a new entity; absent original slots are deleted.
    std::string serializeCurrent(const std::vector<std::pair<int, std::string>>& records) const;
private:
    GroupSequence source_;
    std::vector<std::pair<std::size_t, std::size_t>> slots_;
    std::size_t entitiesEnd_ = 0;
};

std::shared_ptr<DXFEntity> parseEntity(const std::string& raw, const std::string& context = "");
DXFLWPolyline readLWPolyline(const std::string& raw);
// origins maps each new vertex to its original index, or -1 for a new vertex.
// This retains native vertex IDs when inserting, deleting, or reordering.
std::string updateLWPolyline(const std::string& raw, const DXFLWPolyline& polyline,
                             const std::vector<int>& origins = {});
std::string createLWPolyline(const DXFLWPolyline& polyline, const std::string& layer = "0");
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
