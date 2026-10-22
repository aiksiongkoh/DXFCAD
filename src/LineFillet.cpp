#include <DXFCAD/Drafting.h>
#include "LineCorner.h"

namespace DXFCAD {
LineFilletResult Drafting::filletLines(const std::string& first,const std::string& second,
                                      double radius,bool keepFirstStart,bool keepSecondStart) {
    if (!std::isfinite(radius) || radius<=0) throw std::invalid_argument("Fillet radius must be positive and finite");
    using namespace Detail;
    const LineCorner pair(first,second,keepFirstStart,keepSecondStart);
    const auto& u=pair.u; const auto& v=pair.v; const auto& corner=pair.corner;
    const double angle=std::atan2(size(cross3(u,v)),std::clamp(dot3(u,v),-1.0,1.0));
    const double distance=radius/std::tan(angle/2);
    pair.requireFit(distance,distance,"Fillet radius must fit before both retained endpoints");
    const auto tangentA=plus(corner,times(u,distance)), tangentB=plus(corner,times(v,distance));
    const auto center=plus(corner,times(unit(plus(u,v)),std::hypot(distance,radius)));
    const auto arcNormal=times(unit(cross3(u,v)),-1);
    const OCSFrame arcFrame({arcNormal.x,arcNormal.y,arcNormal.z});
    const auto centerOCS=arcFrame.fromWorld(center);
    const auto startOCS=arcFrame.fromWorld(tangentA), endOCS=arcFrame.fromWorld(tangentB);
    auto degrees=[&](Vector3D p) { return Detail::wrap(std::atan2(p.y-centerOCS.y,p.x-centerOCS.x))*180/Detail::pi; };
    // A new entity receives visual properties from the first line, but no
    // identity, ownership, application payload or LINE-specific geometry.
    auto record=pair.connectorRecord("ARC");
    const DXFRecord geometry{{{100,"AcDbCircle"},{10,"0"},{20,"0"},{30,"0"},{40,"1"},
                              {210,"0"},{220,"0"},{230,"1"},{100,"AcDbArc"},{50,"0"},{51,"90"}}};
    record.groups.insert(record.groups.end(),geometry.groups.begin(),geometry.groups.end());
    const auto arc=patchEntityFields(record,{{10,centerOCS.x},{20,centerOCS.y},{30,centerOCS.z},{40,radius},
                                           {210,arcNormal.x},{220,arcNormal.y},{230,arcNormal.z},
                                           {50,degrees(startOCS)},{51,degrees(endOCS)}});
    const auto arcSegment=parseEntity(arc)->geometry().segments.at(0);
    if (size(minus(arcSegment.start,tangentA))>1e-7 || size(minus(arcSegment.end,tangentB))>1e-7
        || std::abs(arcSegment.arc->sweep-(Detail::pi-angle))>1e-7)
        throw std::invalid_argument("Fillet coordinates cannot represent a stable tangent arc");
    return {endpoint(*pair.first,keepFirstStart,tangentA),endpoint(*pair.second,keepSecondStart,tangentB),arc};
}
}
