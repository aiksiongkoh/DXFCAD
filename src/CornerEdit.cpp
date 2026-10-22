#include <DXFCAD/CornerEdit.h>
#include <DXFCAD/Contour.h>
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace DXFCAD {
CornerEditPlan::CornerEditPlan(const DXFDocument& source,
    const std::vector<std::pair<int,std::string>>& live,const std::array<std::size_t,2>& selected,
    const std::vector<std::string>& updates,const std::string& connector,const std::string& connectorType)
{
    if (updates.size()!=2 || selected[0]==selected[1] || selected[0]>=live.size() || selected[1]>=live.size())
        throw std::invalid_argument("Corner editing requires two distinct native LINE objects");
    if (connectorType!="LINE" && connectorType!="ARC")
        throw std::invalid_argument("Corner connector must be LINE or ARC");
    source.serializeCurrent(live);
    auto proposed=live;
    for (std::size_t i=0;i<2;++i) {
        const auto original=parseEntity(live[selected[i]].second), updated=parseEntity(updates[i]);
        if (original->type()!="LINE" || updated->type()!="LINE")
            throw std::invalid_argument("Corner editing requires two native LINE objects");
        if (original->handle()!=updated->handle() || original->layer()!=updated->layer())
            throw std::invalid_argument("Corner edits must retain each LINE's native identity and layer");
        Contour::length(live[selected[i]].second);
        Contour::length(updates[i]);
        proposed[selected[i]].second=updates[i];
    }
    const auto entity=parseEntity(connector);
    if (entity->type()!=connectorType) throw std::invalid_argument("Unexpected corner connector type");
    if (entity->layer()!=parseEntity(live[selected[0]].second)->layer())
        throw std::invalid_argument("Corner connector must use the first LINE's layer");
    Contour::length(connector);
    const auto a=parseEntity(updates[0])->geometry().segments.at(0);
    const auto b=parseEntity(updates[1])->geometry().segments.at(0);
    const auto c=entity->geometry().segments.at(0);
    const double tolerance=1e-7*std::max({1.0,Contour::length(updates[0]),Contour::length(updates[1]),Contour::length(connector)});
    auto near=[&](Vector3D p,Vector3D q) { return std::hypot(p.x-q.x,p.y-q.y,p.z-q.z)<=tolerance; };
    auto endpoint=[&](const auto& line,Vector3D point) { return near(line.start,point) || near(line.end,point); };
    if (!(endpoint(a,c.start) && endpoint(b,c.end)) && !(endpoint(a,c.end) && endpoint(b,c.start)))
        throw std::invalid_argument("Corner connector must join endpoints of both updated LINE objects");
    connector_=entity->copied({0,0,0});
    proposed.emplace_back(-1,connector_);
    drawingBytes_=source.serializeCurrent(proposed);
    updates_=updates;
}
}
