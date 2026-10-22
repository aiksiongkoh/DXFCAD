#include <DXFCAD/Drafting.h>
#include "LineCorner.h"

namespace DXFCAD {
LineChamferResult Drafting::chamferLines(const std::string& first,const std::string& second,
                                        double firstDistance,double secondDistance,
                                        bool keepFirstStart,bool keepSecondStart) {
    using namespace Detail;
    if (!std::isfinite(firstDistance) || !std::isfinite(secondDistance) || firstDistance<=0 || secondDistance<=0)
        throw std::invalid_argument("Chamfer distances must be positive and finite");
    const LineCorner pair(first,second,keepFirstStart,keepSecondStart);
    pair.requireFit(firstDistance,secondDistance,"Chamfer distances must fit before both retained endpoints");
    const auto a=plus(pair.corner,times(pair.u,firstDistance)), b=plus(pair.corner,times(pair.v,secondDistance));
    if (!std::isfinite(size(minus(a,b))) || size(minus(a,b))<=tolerance)
        throw std::invalid_argument("Chamfer connector would collapse");
    auto record=pair.connectorRecord("LINE");
    const DXFRecord geometry{{{100,"AcDbLine"},{10,"0"},{20,"0"},{30,"0"},{11,"0"},{21,"0"},{31,"0"}}};
    record.groups.insert(record.groups.end(),geometry.groups.begin(),geometry.groups.end());
    const auto connector=patchEntityFields(record,{{10,a.x},{20,a.y},{30,a.z},{11,b.x},{21,b.y},{31,b.z}});
    parseEntity(connector)->geometry();
    return {endpoint(*pair.first,keepFirstStart,a),endpoint(*pair.second,keepSecondStart,b),connector};
}
}
