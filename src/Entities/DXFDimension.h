#pragma once

#include <DXFCAD/DXFEntity.h>

namespace DXFCAD::Detail {

class DXFDimension final : public DXFEntity {
public:
    DXFDimension(GroupSequence record, std::string context);
    std::string context() const override;
    EntityGeometry geometry() const override;
private:
    std::string context_;
};

} // namespace DXFCAD::Detail
