#pragma once

#include <DXFCAD/DXFEntity.h>

namespace DXFCAD::Detail {

class DXFMText final : public DXFEntity {
public:
    using DXFEntity::DXFEntity;
    std::string translated(const Vector3D& offset) const override;
    EntityGeometry geometry() const override;
};

} // namespace DXFCAD::Detail
