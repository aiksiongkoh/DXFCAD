#include <DXFCAD/SnapGeometry.h>
#include "PlanarCurves.h"
#include <limits>

namespace DXFCAD {
namespace {
Vector3D difference(Vector3D a,Vector3D b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vector3D cross3(Vector3D a,Vector3D b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
double dot3(Vector3D a,Vector3D b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
double length3(Vector3D p) { return std::hypot(p.x,p.y,p.z); }
void finite(Vector3D p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
        throw std::invalid_argument("Snap coordinates must be finite");
}
std::vector<EntityGeometry> pieces(const EntityGeometry& g) {
    std::vector<EntityGeometry> result;
    for (const auto& segment : g.segments) {
        if (!segment.arc && length3(difference(segment.end,segment.start))<=Detail::tolerance) continue;
        EntityGeometry piece; piece.segments.push_back(segment); result.push_back(std::move(piece));
    }
    for (const auto& circle : g.circles) {
        EntityGeometry piece; piece.circles.push_back(circle); result.push_back(std::move(piece));
    }
    return result;
}
Vector3D origin(const EntityGeometry& g) {
    return g.segments.empty() ? g.circles.front().center : g.segments.front().start;
}
bool straight(const EntityGeometry& g) { return !g.segments.empty() && !g.segments.front().arc; }
}

SnapGeometry::SnapGeometry(const DXFEntity& entity,Vector3D origin,Vector3D x,Vector3D y,Vector3D z)
    : geometry_(entity.geometry()) {
    for (auto p : {origin,x,y,z}) finite(p);
    if (std::abs(length3(x)-1)>1e-9 || std::abs(length3(y)-1)>1e-9 || std::abs(length3(z)-1)>1e-9
        || std::abs(dot3(x,y))>1e-9 || length3(difference(cross3(x,y),z))>1e-9)
        throw std::invalid_argument("Snap placement must be an orthonormal right-handed frame");
    auto rotate=[&](Vector3D p) {
        Vector3D q{x.x*p.x+y.x*p.y+z.x*p.z,x.y*p.x+y.y*p.y+z.y*p.z,x.z*p.x+y.z*p.y+z.z*p.z};
        finite(q); return q;
    };
    auto place=[&](Vector3D p) {
        auto q=rotate(p); q={q.x+origin.x,q.y+origin.y,q.z+origin.z}; finite(q); return q;
    };
    for (auto& s : geometry_.segments) {
        s.start=place(s.start); s.end=place(s.end);
        if (s.arc) {
            s.arc->center=place(s.arc->center); s.arc->midpoint=place(s.arc->midpoint);
            s.arc->normal=rotate(s.arc->normal);
        }
    }
    for (auto& c : geometry_.circles) { c.center=place(c.center); c.normal=rotate(c.normal); }
    for (auto& p : geometry_.bounds) p=place(p);
}

std::vector<DXFSnapPoint> SnapGeometry::midpoints(const EntityGeometry& geometry) {
    std::vector<DXFSnapPoint> result;
    for (const auto& s : geometry.segments) {
        if (!s.arc && length3(difference(s.end,s.start))==0) continue;
        const auto point=s.arc ? s.arc->midpoint : Vector3D{s.start.x/2+s.end.x/2,s.start.y/2+s.end.y/2,s.start.z/2+s.end.z/2};
        finite(point); result.push_back({point,"midpoint"});
    }
    return result;
}

std::vector<Vector3D> SnapGeometry::boundPoints() const {
    const double inf=std::numeric_limits<double>::infinity();
    Vector3D low{inf,inf,inf},high{-inf,-inf,-inf};
    auto add=[&](Vector3D p) {
        finite(p);
        low={std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};
        high={std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};
    };
    auto circle=[&](Vector3D c,double r) { add({c.x-r,c.y-r,c.z-r}); add({c.x+r,c.y+r,c.z+r}); };
    for (const auto& p : geometry_.bounds) add(p);
    for (const auto& s : geometry_.segments) {
        add(s.start); add(s.end);
        if (s.arc) circle(s.arc->center,s.arc->radius);
    }
    for (const auto& c : geometry_.circles) circle(c.center,c.radius);
    if (low.x==inf) return {};
    std::vector<Vector3D> result;
    for (double x : {low.x,high.x}) for (double y : {low.y,high.y}) for (double z : {low.z,high.z}) result.push_back({x,y,z});
    return result;
}

std::vector<DXFSnapPoint> SnapGeometry::intersections(const SnapGeometry& other) const {
    std::vector<DXFSnapPoint> result;
    const auto a=pieces(geometry_), b=pieces(other.geometry_);
    for (const auto& first : a) for (const auto& second : b) {
        Vector3D n;
        if (straight(first) && straight(second)) {
            auto direction=[](const EntityGeometry& g) {
                const auto d=difference(g.segments[0].end,g.segments[0].start);
                const double size=length3(d);
                return Vector3D{d.x/size,d.y/size,d.z/size};
            };
            n=cross3(direction(first),direction(second));
            if (length3(n)<1e-12) continue; // Overlaps have no isolated crossing.
        } else n=Detail::normal(straight(first) ? second : first);
        try {
            const Detail::Plane plane(n,origin(first));
            const auto c=plane.curves(first).front(), d=plane.curves(second).front();
            for (const auto p : Detail::intersections(c,d)) {
                const double t=c.parameter(p), u=d.parameter(p);
                const double ce=Detail::tolerance/c.extent(), de=Detail::tolerance/d.extent();
                if (!std::isfinite(t) || !std::isfinite(u) || t < -ce || t > 1+ce || u < -de || u > 1+de) continue;
                const auto world=plane.frame.toWorld(p,plane.elevation);
                if (std::none_of(result.begin(),result.end(),[&](const auto& hit) { return length3(difference(hit.position,world))<=1e-7; }))
                    result.push_back({world,"intersection"});
            }
        } catch (const std::invalid_argument&) {
            // Noncoplanar curves are deliberately not apparent intersections.
        }
    }
    return result;
}
}
