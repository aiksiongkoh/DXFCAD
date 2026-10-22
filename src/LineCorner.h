#pragma once
#include <DXFCAD/Drafting.h>
#include "PlanarCurves.h"

namespace DXFCAD::Detail {
inline Vector3D minus(Vector3D a,Vector3D b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vector3D plus(Vector3D a,Vector3D b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vector3D times(Vector3D a,double t) { return {a.x*t,a.y*t,a.z*t}; }
inline Vector3D cross3(Vector3D a,Vector3D b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline double dot3(Vector3D a,Vector3D b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline double size(Vector3D p) { return std::hypot(p.x,p.y,p.z); }
inline Vector3D unit(Vector3D p) {
    const double length=size(p);
    if (!std::isfinite(length) || length<=tolerance)
        throw std::invalid_argument("Corner editing needs finite nonzero lines and retained rays");
    return times(p,1/length);
}
inline std::string endpoint(const DXFEntity& entity,bool keepStart,Vector3D p) {
    const int code=keepStart ? 11 : 10;
    const auto raw=patchEntityFields(entity.record(),{{code,p.x},{code+10,p.y},{code+20,p.z}});
    parseEntity(raw)->geometry();
    return raw;
}

// The two retained endpoints define one of the four possible corner sectors.
// Distances are measured from the infinite LINE support intersection.
class LineCorner {
public:
    std::shared_ptr<DXFEntity> first,second;
    Vector3D corner,u,v;
    double firstAvailable,secondAvailable;
    LineCorner(const std::string& a,const std::string& b,bool keepFirstStart,bool keepSecondStart)
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
    void requireFit(double a,double b,const char* message) const {
        if (!std::isfinite(a) || !std::isfinite(b) || a<=tolerance || b<=tolerance
            || a>=firstAvailable-tolerance || b>=secondAvailable-tolerance)
            throw std::invalid_argument(message);
    }
    DXFRecord connectorRecord(const std::string& type) const {
        DXFRecord record;
        record.groups={{0,type},{100,"AcDbEntity"},{8,first->layer()}};
        const auto fields=nativeEntityFields(first->record());
        for (int code : {6,48,60,62,370,420,430,440})
            for (const auto& group : fields.groups) if (group.first==code) { record.groups.push_back(group); break; }
        return record;
    }
};
}
