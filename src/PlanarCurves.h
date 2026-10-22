#pragma once
#include <DXFCAD/DXFDocument.h>
#include <DXFCAD/OCSFrame.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

// Shared exact planar centerline geometry for drafting and snapping.
namespace DXFCAD::Detail {
constexpr double tolerance = 1e-9;
inline constexpr double pi = 3.141592653589793238462643383279502884, turn = 2*pi;
inline Point2D sub(Point2D a, Point2D b) { return {a.x-b.x,a.y-b.y}; }
inline Point2D add(Point2D a, Point2D b) { return {a.x+b.x,a.y+b.y}; }
inline Point2D mul(Point2D a, double t) { return {a.x*t,a.y*t}; }
inline double dot(Point2D a, Point2D b) { return a.x*b.x+a.y*b.y; }
inline double cross(Point2D a, Point2D b) { return a.x*b.y-a.y*b.x; }
inline double length(Point2D a) { return std::hypot(a.x,a.y); }
inline double wrap(double a) { a=std::fmod(a,turn); return a<0 ? a+turn : a; }
struct Curve {
    Point2D a,b,c;
    double radius=0, sweep=0;
    bool circular() const { return radius>0; }
    double extent() const { return circular() ? radius*std::abs(sweep) : length(sub(b,a)); }
    Point2D at(double t) const {
        if (!circular()) return add(a,mul(sub(b,a),t));
        const double angle=std::atan2(a.y-c.y,a.x-c.x)+sweep*t;
        return {c.x+radius*std::cos(angle),c.y+radius*std::sin(angle)};
    }
    double parameter(Point2D p) const {
        if (!circular()) {
            const auto u=mul(sub(b,a),1/extent());
            return dot(sub(p,a),u)/extent();
        }
        const double angle=std::atan2(p.y-c.y,p.x-c.x)-std::atan2(a.y-c.y,a.x-c.x);
        double delta=wrap(sweep>0 ? angle : -angle);
        if (turn-delta<tolerance/radius) delta=0;
        return delta/std::abs(sweep);
    }
};
// Project both entities into one plane. No display tessellation participates.
class Plane {
public:
    OCSFrame frame;
    double elevation;
    Plane(Vector3D normal, Vector3D origin)
        : frame({normal.x,normal.y,normal.z}), elevation(frame.fromWorld(origin).z) {}
    Point2D point(Vector3D p) const {
        const auto q=frame.fromWorld(p);
        if (std::abs(q.z-elevation)>1e-7) throw std::invalid_argument("Trim/Extend entities must be coplanar");
        return {q.x,q.y};
    }
    double orientation(Vector3D n) const {
        const auto f=frame.fromWorld(n);
        if (std::hypot(f.x,f.y)>1e-9 || std::abs(std::abs(f.z)-1)>1e-9)
            throw std::invalid_argument("Trim/Extend curves must be coplanar");
        return f.z>0 ? 1 : -1;
    }
    std::vector<Curve> curves(const EntityGeometry& g) const {
        std::vector<Curve> result;
        for (const auto& s : g.segments) {
            Curve c{point(s.start),point(s.end),{0,0}};
            if (s.arc) {
                c.c=point(s.arc->center); c.radius=s.arc->radius;
                c.sweep=s.arc->sweep*orientation(s.arc->normal);
            }
            if (!std::isfinite(c.extent()) || c.extent()<=tolerance)
                throw std::invalid_argument("Trim/Extend requires finite nonzero segments");
            result.push_back(c);
        }
        for (const auto& c : g.circles) {
            const auto center=point(c.center);
            orientation(c.normal);
            result.push_back({{center.x+c.radius,center.y},{center.x+c.radius,center.y},center,c.radius,turn});
        }
        return result;
    }
};
inline std::vector<Point2D> intersections(const Curve& a,const Curve& b) {
    if (!a.circular() && !b.circular()) {
        const auto u=mul(sub(a.b,a.a),1/a.extent()), v=mul(sub(b.b,b.a),1/b.extent());
        const double determinant=cross(u,v);
        if (std::abs(determinant)<1e-12) return {}; // Parallel/overlapping has no unique cut.
        return {add(a.a,mul(u,cross(sub(b.a,a.a),v)/determinant))};
    }
    if (!a.circular() || !b.circular()) {
        const auto& line=a.circular() ? b : a;
        const auto& circle=a.circular() ? a : b;
        const auto u=mul(sub(line.b,line.a),1/line.extent()), d=sub(circle.c,line.a);
        const double along=dot(d,u), perpendicular=cross(d,u);
        const double gap=circle.radius-std::abs(perpendicular);
        if (gap < -tolerance) return {};
        const double h=std::sqrt(std::max(0.0,gap)*(circle.radius+std::abs(perpendicular)));
        return {add(line.a,mul(u,along-h)),add(line.a,mul(u,along+h))};
    }
    const auto d=sub(b.c,a.c); const double distance=length(d);
    if (distance<=tolerance) return {}; // Coincident circles have no unique cut.
    if (distance>a.radius+b.radius+tolerance || distance<std::abs(a.radius-b.radius)-tolerance) return {};
    const double x=(distance+(a.radius-b.radius)*(a.radius+b.radius)/distance)/2;
    const double h2=(a.radius-x)*(a.radius+x);
    if (h2 < -tolerance*std::max(1.0,a.radius)) return {};
    const auto u=mul(d,1/distance), base=add(a.c,mul(u,x));
    const double h=std::sqrt(std::max(0.0,h2));
    return {add(base,{-u.y*h,u.x*h}),add(base,{u.y*h,-u.x*h})};
}
inline Vector3D normal(const EntityGeometry& g) {
    for (const auto& s : g.segments) if (s.arc) return s.arc->normal;
    if (!g.circles.empty()) return g.circles.front().normal;
    return {0,0,1};
}
}
