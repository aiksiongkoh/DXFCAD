#include "LineCorner.h"
#include "CornerUtilities.h"

namespace DXFCAD::Detail {

LineCorner::LineCorner(const std::string& a,const std::string& b,bool keepFirstStart,bool keepSecondStart)
    : first(parseEntity(a)),second(parseEntity(b)) {
    if (first->type()!="LINE" || second->type()!="LINE")
        throw std::invalid_argument("Corner editing requires two LINE entities");
    const auto ga=first->geometry(), gb=second->geometry();
    const auto& sa=ga.segments.at(0); const auto& sb=gb.segments.at(0);
    const auto directionA=unit(minus(sa.end,sa.start)), directionB=unit(minus(sb.end,sb.start));
    const auto normal=cross3(directionA,directionB);
    if (size(normal)<1e-12) throw std::invalid_argument("Corner lines must not be parallel or collinear");
    const Plane plane(normal,sa.start);
    const auto ca=plane.curves(ga).front(), cb=plane.curves(gb).front();
    const auto hits=intersections(ca,cb);
    if (hits.size()!=1) throw std::invalid_argument("Lines have no unique corner");
    corner=plane.frame.toWorld(hits.front(),plane.elevation);
    const auto rayA=minus(keepFirstStart ? sa.start : sa.end,corner);
    const auto rayB=minus(keepSecondStart ? sb.start : sb.end,corner);
    u=unit(rayA); v=unit(rayB);
    firstAvailable=size(rayA); secondAvailable=size(rayB);
}

void LineCorner::requireFit(double a,double b,const char* message) const {
    if (!std::isfinite(a) || !std::isfinite(b) || a<=tolerance || b<=tolerance
        || a>=firstAvailable-tolerance || b>=secondAvailable-tolerance)
        throw std::invalid_argument(message);
}

GroupSequence LineCorner::connectorRecord(const std::string& type) const {
    GroupSequence record;
    record.groups={{0,type},{100,"AcDbEntity"},{8,first->layer()}};
    const auto fields=nativeEntityFields(first->record());
    for (int code : {6,48,60,62,370,420,430,440})
        for (const auto& group : fields.groups) if (group.first==code) { record.groups.push_back(group); break; }
    return record;
}

} // namespace DXFCAD::Detail
