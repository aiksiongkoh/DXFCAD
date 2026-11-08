#pragma once
#include <DXFCAD/DXFEntity.h>

namespace DXFCAD {

class DXFLeader final : public DXFEntity {
public:
    DXFLeader(GroupSequence record, std::string context = {});
    std::string context() const override { return context_; }
    EntityGeometry geometry() const override;
    std::vector<Vector3D> vertices() const;
    int colorIndex() const;
    std::string translated(const Vector3D&) const override;
private:
    std::string context_;
};

} // namespace DXFCAD
