#include <DXFCAD/Drafting.h>
#include "PlanarCurves.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace DXFCAD {
using namespace Detail;

std::string Drafting::trim(const std::string& raw,const std::string& boundary,bool start,bool extend) {
    const auto target=parseEntity(raw), cutter=parseEntity(boundary);
    const auto type=target->type(), boundaryType=cutter->type();
    if (type!="LINE" && type!="ARC" && type!="LWPOLYLINE")
        throw std::invalid_argument("Trim/Extend target must be LINE, ARC or open LWPOLYLINE");
    if (boundaryType!="LINE" && boundaryType!="ARC" && boundaryType!="CIRCLE" && boundaryType!="LWPOLYLINE")
        throw std::invalid_argument("Boundary must be LINE, ARC, CIRCLE or LWPOLYLINE");
    if (type=="LWPOLYLINE" && readLWPolyline(raw).closed())
        throw std::invalid_argument("Endpoint Trim/Extend requires an open polyline target");
    const auto geometry=target->geometry(), edge=cutter->geometry();
    if (geometry.segments.empty()) throw std::invalid_argument("Target has no segments");
    Vector3D n=normal(geometry);
    if (type=="LWPOLYLINE") n=OCSFrame(readLWPolyline(raw).extrusion()).normal();
    else if (type=="LINE") {
        n=normal(edge);
        if (boundaryType=="LWPOLYLINE") n=OCSFrame(readLWPolyline(boundary).extrusion()).normal();
        else if (boundaryType=="LINE" && !edge.segments.empty()) {
            const auto& a=geometry.segments.front(); const auto& b=edge.segments.front();
            auto direction=[](const LWPolylineSegment3D& s) {
                const Vector3D d{s.end.x-s.start.x,s.end.y-s.start.y,s.end.z-s.start.z};
                const double magnitude=std::hypot(d.x,d.y,d.z);
                if (!std::isfinite(magnitude) || magnitude<=tolerance)
                    throw std::invalid_argument("Trim/Extend requires finite nonzero lines");
                return Vector3D{d.x/magnitude,d.y/magnitude,d.z/magnitude};
            };
            const auto u=direction(a), v=direction(b);
            n={u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
            if (std::hypot(n.x,n.y,n.z)<1e-12)
                throw std::invalid_argument("Trim/Extend lines are parallel");
        }
    }
    const Plane plane(n,geometry.segments.front().start);
    const auto curves=plane.curves(geometry), boundaries=plane.curves(edge);
    double best=std::numeric_limits<double>::infinity(), chosen=0;
    std::size_t index=0;
    for (std::size_t i=0; i<curves.size(); ++i) {
        if (extend && i!=(start ? 0 : curves.size()-1)) continue;
        const auto& curve=curves[i]; const double eps=tolerance/curve.extent();
        for (const auto& cut : boundaries) for (auto p : intersections(curve,cut)) {
            const double u=cut.parameter(p);
            if (!std::isfinite(u) || u < -tolerance/cut.extent() || u > 1+tolerance/cut.extent()) continue;
            double t=curve.parameter(p);
            if (extend && start && curve.circular()) t-=turn/std::abs(curve.sweep);
            if (!std::isfinite(t)) throw std::overflow_error("Trim/Extend intersection exceeds finite range");
            if (extend) {
                if (start ? t>=-eps : t<=1+eps) continue;
                if (curve.circular() && std::abs(curve.sweep)*(start ? 1-t : t)>=turn-1e-12) continue;
            } else {
                if (t < -eps || t > 1+eps) continue;
                t=std::clamp(t,0.0,1.0);
                if (t<=eps) t=0;
                else if (t>=1-eps) t=1;
                if ((i==0 && t<=eps) || (i==curves.size()-1 && t>=1-eps)) continue;
            }
            const double distance=extend ? (start ? -t : t-1)*curve.extent()
                                         : (start ? i+t : curves.size()-i-t);
            if (distance<best) { best=distance; index=i; chosen=t; }
        }
    }
    if (!std::isfinite(best)) throw std::invalid_argument(extend ? "No boundary intersection extends the chosen endpoint" : "No boundary intersection inside the target");
    std::string result;
    if (type=="LINE") {
        const auto p=plane.frame.toWorld(curves[0].at(chosen),plane.elevation);
        const int code=start ? 10 : 11;
        result=patchEntityFields(target->record(),{{code,p.x},{code+10,p.y},{code+20,p.z}});
    } else if (type=="ARC") {
        const auto fields=nativeEntityFields(target->record());
        const double angle=fields.number(50)+geometry.segments[0].arc->sweep*chosen*180/pi;
        result=patchEntityFields(target->record(),{{start ? 50 : 51,wrap(angle*pi/180)*180/pi}});
    } else {
        const auto model=readLWPolyline(raw);
        const auto original=model.vertices();
        const auto segments=model.segments();
        std::vector<LWPolylineVertex> vertices;
        std::vector<int> origins;
        // Snap a vertex intersection to the segment that remains, avoiding a
        // duplicate endpoint and preserving that vertex's outgoing attributes.
        if (!extend && start && chosen==1) { ++index; chosen=0; }
        if (!extend && !start && chosen==0) { --index; chosen=1; }
        const auto& segment=segments[index];
        const double a=start ? chosen : 0, b=start ? 1 : chosen;
        auto first=original[index], last=original[index+1];
        auto point=[&](double t) {
            if (t==0) return segment.start;
            if (t==1) return segment.end;
            if (!segment.arc) return add(segment.start,mul(sub(segment.end,segment.start),t));
            const auto& arc=*segment.arc;
            const double angle=std::atan2(segment.start.y-arc.center.y,segment.start.x-arc.center.x)+arc.sweep*t;
            return Point2D{arc.center.x+arc.radius*std::cos(angle),arc.center.y+arc.radius*std::sin(angle)};
        };
        const auto pa=point(a), pb=point(b);
        first.x=pa.x; first.y=pa.y; last.x=pb.x; last.y=pb.y;
        first.bulge=segment.arc ? std::tan(segment.arc->sweep*(b-a)/4) : 0;
        if (first.hasWidths || first.startWidth!=0 || first.endWidth!=0) {
            const double w=first.startWidth, delta=first.endWidth-w;
            first.startWidth=w+delta*a; first.endWidth=w+delta*b;
        }
        const std::size_t begin=start ? index : 0, end=start ? original.size() : index+2;
        for (std::size_t j=begin; j<end; ++j) {
            vertices.push_back(j==index ? first : j==index+1 ? last : original[j]);
            origins.push_back(static_cast<int>(j));
        }
        result=updateLWPolyline(raw,LWPolyline(vertices,false,model.constantWidth(),model.elevation(),model.extrusion()),origins);
    }
    parseEntity(result)->geometry();
    return result;
}
}
