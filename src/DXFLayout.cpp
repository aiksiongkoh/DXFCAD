#include <DXFCAD/Drawing.h>
#include <DXFCAD/Drafting.h>
#include <cmath>
#include <stdexcept>

namespace DXFCAD {
namespace {
GroupSequence layoutFields(const GroupSequence& record) {
    GroupSequence result;
    bool active=false;
    for (const auto& group : nativeEntityFields(record).groups) {
        if (group.first==100) active=group.second=="AcDbLayout";
        else if (active) result.groups.push_back(group);
    }
    return result;
}
}
DXFLayout::DXFLayout(GroupSequence record,std::string fallbackName)
    : record_(std::move(record)),name_(std::move(fallbackName)) {
    if (!record_.groups.empty()) {
        if (record_.value(0)!="LAYOUT") throw std::invalid_argument("Expected a LAYOUT object");
        const auto fields=layoutFields(record_);
        name_=fields.value(1,name_);
        tabOrder_=fields.integer(71);
        if (tabOrder_<0) throw std::invalid_argument("Invalid LAYOUT tab order");
    }
    if (name_.empty()) throw std::invalid_argument("Layout name must not be empty");
}
std::optional<PaperRectangle> DXFLayout::limits() const {
    const auto fields=layoutFields(record_);
    if (fields.value(10).empty() || fields.value(20).empty() || fields.value(11).empty() || fields.value(21).empty())
        return std::nullopt;
    PaperRectangle result{fields.number(10),fields.number(20),fields.number(11),fields.number(21)};
    if (result.minX>=result.maxX || result.minY>=result.maxY) return std::nullopt;
    return result;
}
Vector3D DXFViewport::paperCenter() const {
    const auto fields=nativeEntityFields(record_);
    return {fields.number(10),fields.number(20),0};
}
PaperRectangle DXFViewport::paperBounds() const {
    const auto center=paperCenter();
    return {center.x-paperWidth()/2,center.y-paperHeight()/2,
            center.x+paperWidth()/2,center.y+paperHeight()/2};
}
Vector3D DXFViewport::modelToPaper(const Vector3D& point) const {
    return ViewportProjection(*this).project(point);
}
ViewportProjection::ViewportProjection(const DXFViewport& viewport) {
    const auto frame=viewport.modelView();
    modelCenter_=frame.center; xAxis_=frame.xAxis; yAxis_=frame.yAxis;
    paperCenter_=viewport.paperCenter(); scale_=viewport.scale();
}
Vector3D ViewportProjection::project(const Vector3D& point) const {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
        throw std::invalid_argument("Model coordinates must be finite");
    const double dx=point.x-modelCenter_.x,dy=point.y-modelCenter_.y;
    const Vector3D result{paperCenter_.x+scale_*(dx*xAxis_.x+dy*xAxis_.y),
                          paperCenter_.y+scale_*(dx*yAxis_.x+dy*yAxis_.y),0};
    if (!std::isfinite(result.x) || !std::isfinite(result.y))
        throw std::overflow_error("Viewport projection exceeds finite range");
    return result;
}
} // namespace DXFCAD
