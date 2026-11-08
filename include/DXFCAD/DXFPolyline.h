#pragma once
#include <DXFCAD/Drawing.h>

namespace DXFCAD {
// POLYLINE / VERTEX / SEQEND form one native document entity and one slot.
class DXFPolyline final : public DXFEntity {
public:
    explicit DXFPolyline(GroupSequence record);
    const GroupSequence& headerRecord() const { return sequence_.front(); }
    EntityGeometry geometry() const override;
    std::string translated(const Vector3D& offset) const override;
    std::vector<LWPolylineWidthRegion> widthRegions(double tolerance=0.0001) const override;
private:
    std::vector<GroupSequence> sequence_;
    bool threeDimensional() const;
    DXFLWPolyline planarPolyline() const;
};
}
