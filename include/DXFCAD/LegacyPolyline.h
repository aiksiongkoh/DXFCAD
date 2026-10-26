#pragma once
#include <DXFCAD/DXFDocument.h>

namespace DXFCAD {
// POLYLINE / VERTEX / SEQEND form one native document entity and one slot.
class LegacyPolyline final : public DXFEntity {
public:
    explicit LegacyPolyline(DXFRecord record);
    const DXFRecord& headerRecord() const { return sequence_.front(); }
    EntityGeometry geometry() const override;
    std::string translated(const Vector3D& offset) const override;
    std::vector<LWPolylineWidthRegion> widthRegions(double tolerance=0.0001) const override;
private:
    std::vector<DXFRecord> sequence_;
    bool threeDimensional() const;
    LWPolylineEntity planarPolyline() const;
};
}
