#include <DXFCAD/Drafting.h>
#include "PlanarCurves.h"

namespace DXFCAD {
namespace {
Vector3D sub3(Vector3D a,Vector3D b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vector3D mul3(Vector3D a,double t) { return {a.x*t,a.y*t,a.z*t}; }
Vector3D cross3(Vector3D a,Vector3D b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
double dot3(Vector3D a,Vector3D b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
double length3(Vector3D p) { return std::hypot(p.x,p.y,p.z); }
void finite(Vector3D p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
        throw std::invalid_argument("ARC coordinates must be finite");
}
std::string record(Vector3D center,double radius,Vector3D startDirection,Vector3D endDirection,
                   Vector3D normal,const std::string& layer) {
    finite(center);
    if (!std::isfinite(radius) || radius<=0) throw std::invalid_argument("ARC radius must be positive and finite");
    if (layer.empty() || layer.find_first_of("\r\n")!=std::string::npos) throw std::invalid_argument("Invalid ARC layer name");
    const OCSFrame frame({normal.x,normal.y,normal.z});
    const auto c=frame.fromWorld(center), a=frame.fromWorld(startDirection), b=frame.fromWorld(endDirection);
    auto degrees=[](Vector3D v) { return Detail::wrap(std::atan2(v.y,v.x))*180/Detail::pi; };
    DXFRecord raw{{{0,"ARC"},{100,"AcDbEntity"},{8,layer},{100,"AcDbCircle"},
                   {10,"0"},{20,"0"},{30,"0"},{40,"1"},{210,"0"},{220,"0"},{230,"1"},
                   {100,"AcDbArc"},{50,"0"},{51,"90"}}};
    const auto result=patchEntityFields(raw,{{10,c.x},{20,c.y},{30,c.z},{40,radius},
                                            {210,normal.x},{220,normal.y},{230,normal.z},
                                            {50,degrees(a)},{51,degrees(b)}});
    const auto edge=parseEntity(result)->geometry().segments.at(0);
    const double chord=length3(sub3(edge.end,edge.start));
    if (!std::isfinite(chord) || chord==0) throw std::invalid_argument("ARC endpoints must be distinct and representable; use CIRCLE for a full turn");
    return result;
}
}
std::string createArc(const Vector3D& center,double radius,double startDegrees,double endDegrees,
                       bool clockwise,const std::string& layer) {
    if (!std::isfinite(startDegrees) || !std::isfinite(endDegrees))
        throw std::invalid_argument("ARC angles must be finite");
    auto normalized=[](double d) { d=std::fmod(d,360.0); return d<0 ? d+360 : d; };
    const double a=normalized(startDegrees), b=normalized(endDegrees);
    const double start=a*Detail::pi/180, end=b*Detail::pi/180;
    if (a==b)
        throw std::invalid_argument("ARC angles must be distinct; use CIRCLE for a full turn");
    return record(center,radius,{std::cos(start),std::sin(start),0},{std::cos(end),std::sin(end),0},
                  {0,0,clockwise ? -1.0 : 1.0},layer);
}
std::string createArcThroughPoints(const Vector3D& start,const Vector3D& through,const Vector3D& end,
                                    const std::string& layer) {
    for (auto p : {start,through,end}) finite(p);
    const auto a=sub3(through,start), b=sub3(end,start);
    const double la=length3(a), lb=length3(b), lc=length3(sub3(end,through));
    if (!std::isfinite(la) || !std::isfinite(lb) || !std::isfinite(lc))
        throw std::overflow_error("ARC point separation exceeds finite range");
    if (la==0 || lb==0 || lc==0) throw std::invalid_argument("ARC needs three distinct points");
    const Vector3D x{a.x/la,a.y/la,a.z/la}, direction{b.x/lb,b.y/lb,b.z/lb};
    const auto cross=cross3(x,direction); const double sine=length3(cross);
    if (sine<1e-12) throw std::invalid_argument("ARC points are collinear or too nearly collinear");
    const auto normal=mul3(cross,1/sine), y=cross3(normal,x);
    // Solve in a local orthonormal frame without squaring the point distances.
    const double cx=la/2, cy=(lb/2-cx*dot3(direction,x))/dot3(direction,y);
    const Vector3D center{start.x+x.x*cx+y.x*cy,start.y+x.y*cx+y.y*cy,start.z+x.z*cx+y.z*cy};
    const double radius=std::hypot(cx,cy);
    const auto raw=record(center,radius,sub3(start,center),sub3(end,center),normal,layer);
    const auto edge=parseEntity(raw)->geometry().segments.at(0);
    const OCSFrame frame({normal.x,normal.y,normal.z});
    const auto p=frame.fromWorld(sub3(through,center));
    const auto fields=nativeEntityFields(parseEntity(raw)->record());
    const double middle=Detail::wrap(std::atan2(p.y,p.x)-fields.number(50)*Detail::pi/180);
    if (length3(sub3(edge.start,start))>1e-7 || length3(sub3(edge.end,end))>1e-7
        || std::abs(length3(sub3(through,center))-radius)>1e-7 || middle<=0 || middle>=edge.arc->sweep)
        throw std::invalid_argument("ARC coordinates cannot represent a stable three-point arc");
    return raw;
}
}
