#pragma once

#include <DXFCAD/DXFEntity.h>

namespace DXFCAD::Detail {

// The retained endpoints select the corner sector.
class LineCorner {
public:
    std::shared_ptr<DXFEntity> first,second;
    Vector3D corner,u,v;
    double firstAvailable,secondAvailable;
    LineCorner(const std::string& a,const std::string& b,bool keepFirstStart,bool keepSecondStart);
    void requireFit(double a,double b,const char* message) const;
    GroupSequence connectorRecord(const std::string& type) const;
};

} // namespace DXFCAD::Detail
