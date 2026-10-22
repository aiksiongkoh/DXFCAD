#include <DXFCAD/Drafting.h>
#include "PlanarCurves.h"
#include <limits>

namespace DXFCAD {
namespace {
using namespace Detail;
constexpr double joinTolerance=1e-8;
bool close(Point2D a,Point2D b) { return length(sub(a,b))<=joinTolerance; }
bool contains(const Curve& c,Point2D p) {
    const double t=c.parameter(p), eps=joinTolerance/c.extent();
    return std::isfinite(t) && t>=-eps && t<=1+eps;
}
std::vector<Curve> curves(const LWPolyline& model) {
    std::vector<Curve> result;
    for (const auto& s : model.segments()) {
        Curve c{s.start,s.end,{0,0}};
        if (s.arc) { c.c=s.arc->center; c.radius=s.arc->radius; c.sweep=s.arc->sweep; }
        if (!std::isfinite(c.extent()) || c.extent()<=tolerance)
            throw std::invalid_argument("Offset polyline has a zero-length or unbounded segment");
        result.push_back(c);
    }
    return result;
}
// Coincident supports need separate overlap checks because analytic support
// intersections deliberately return no isolated point in that case.
bool circularOverlap(const Curve& a,const Curve& b) {
    if (!a.circular() || !b.circular() || !close(a.c,b.c) || std::abs(a.radius-b.radius)>joinTolerance) return false;
    auto interval=[](const Curve& c) {
        const auto p=c.sweep>0 ? c.a : c.b;
        const double start=wrap(std::atan2(p.y-c.c.y,p.x-c.c.x));
        return std::pair<double,double>{start,start+std::abs(c.sweep)};
    };
    const auto x=interval(a), y=interval(b);
    for (double shift : {-turn,0.0,turn})
        if ((std::min(x.second,y.second+shift)-std::max(x.first,y.first+shift))*a.radius>joinTolerance) return true;
    return false;
}
void simple(const LWPolyline& model) {
    const auto edges=curves(model); const auto count=edges.size();
    for (std::size_t i=0; i<count; ++i) for (std::size_t j=i+1; j<count; ++j) {
        const auto& a=edges[i]; const auto& b=edges[j];
        auto allowed=[&](Point2D p) {
            return (j==i+1 && close(p,a.b) && close(p,b.a))
                || (model.closed() && i==0 && j==count-1 && close(p,a.a) && close(p,b.b));
        };
        if (circularOverlap(a,b)) throw std::invalid_argument("Offset polyline has overlapping arcs");
        auto hits=intersections(a,b);
        if (!a.circular() && !b.circular()) {
            const auto u=mul(sub(a.b,a.a),1/a.extent());
            const auto v=mul(sub(b.b,b.a),1/b.extent());
            if (std::abs(cross(u,v))<1e-12 && std::abs(cross(sub(b.a,a.a),u))<=joinTolerance) {
                const double t=dot(sub(b.a,a.a),u), w=dot(sub(b.b,a.a),u);
                const double low=std::max(0.0,std::min(t,w)), high=std::min(a.extent(),std::max(t,w));
                if (high-low>joinTolerance) throw std::invalid_argument("Offset polyline has overlapping lines");
                if (high>=low-joinTolerance) hits.push_back(add(a.a,mul(u,low)));
            }
        }
        // Include coincident arc endpoints; disjoint arc intervals can still
        // touch at a nonadjacent vertex, which is a self-touching result.
        for (auto p : {a.a,a.b}) if ((close(p,b.a) || close(p,b.b))) hits.push_back(p);
        for (auto p : hits) if (contains(a,p) && contains(b,p) && !allowed(p))
            throw std::invalid_argument("Offset polyline would self-intersect");
    }
}
Point2D tangent(const Curve& c,bool end) {
    if (!c.circular()) return mul(sub(c.b,c.a),1/c.extent());
    const auto r=mul(sub(end ? c.b : c.a,c.c),1/c.radius);
    return mul({-r.y,r.x},c.sweep>0 ? 1 : -1);
}
struct OffsetEdge {
    Curve original, shifted;
    double originalBulge;
    OffsetEdge(const Curve& curve,double bulge,double distance)
        : original(curve), shifted(curve), originalBulge(bulge) {
        if (curve.circular()) {
            shifted.radius=curve.radius-(curve.sweep>0 ? distance : -distance);
            if (!std::isfinite(shifted.radius) || shifted.radius<=tolerance)
                throw std::invalid_argument("Offset arc radius must remain positive and finite");
            shifted.a=add(curve.c,mul(sub(curve.a,curve.c),shifted.radius/curve.radius));
            shifted.b=add(curve.c,mul(sub(curve.b,curve.c),shifted.radius/curve.radius));
        } else {
            const auto u=tangent(curve,false);
            const Point2D shift{-u.y*distance,u.x*distance};
            shifted.a=add(curve.a,shift); shifted.b=add(curve.b,shift);
        }
    }
    double bulge() const {
        if (!original.circular()) {
            if (dot(sub(shifted.b,shifted.a),tangent(original,false))<=tolerance)
                throw std::invalid_argument("Offset would collapse or reverse a segment");
            return originalBulge;
        }
        auto angle=[&](Point2D a,Point2D b) {
            const auto u=mul(sub(a,original.c),1/original.radius), v=mul(sub(b,original.c),1/shifted.radius);
            return std::atan2(cross(u,v),dot(u,v));
        };
        const double sweep=original.sweep+angle(original.b,shifted.b)-angle(original.a,shifted.a);
        if (!std::isfinite(sweep) || sweep*original.sweep<=0 || std::abs(sweep)*shifted.radius<=tolerance || std::abs(sweep)>=turn-1e-12)
            throw std::invalid_argument("Offset would collapse, reverse or wrap an arc");
        return std::abs(sweep-original.sweep)<1e-12 ? originalBulge : std::tan(sweep/4);
    }
};
struct Join { bool round=false; double bulge=0; };
class PolylineOffset {
public:
    PolylineOffset(const std::string& raw,double distance)
        : raw_(raw), model_(readLWPolyline(raw)), distance_(distance), joins_(model_.vertices().size()) {
        if (model_.vertices().size()<2) throw std::invalid_argument("Offset polyline needs more vertices");
        model_.worldSegments();
        simple(model_);
        const auto original=curves(model_);
        for (std::size_t i=0; i<original.size(); ++i) edges_.emplace_back(original[i],model_.vertices()[i].bulge,distance);
    }
    std::string result() {
        const auto n=model_.vertices().size();
        for (std::size_t i=model_.closed() ? 0 : 1; i<(model_.closed() ? n : n-1); ++i) join(i);
        std::vector<LWPolylineVertex> vertices;
        std::vector<int> origins;
        for (std::size_t i=0; i<n; ++i) {
            auto v=model_.vertices()[i];
            const auto previous=(i+edges_.size()-1)%edges_.size();
            if (joins_[i].round) {
                const auto p=edges_[previous].shifted.b;
                LWPolylineVertex bridge{p.x,p.y,joins_[i].bulge};
                const auto& incoming=model_.vertices()[previous];
                auto variable=[](const LWPolylineVertex& w) { return w.hasWidths || w.startWidth!=0 || w.endWidth!=0; };
                bridge.hasWidths=variable(incoming) || variable(v);
                if (bridge.hasWidths) {
                    bridge.startWidth=variable(incoming) ? incoming.endWidth : model_.constantWidth();
                    bridge.endWidth=variable(v) ? v.startWidth : model_.constantWidth();
                }
                vertices.push_back(bridge); origins.push_back(-1);
            }
            const auto p=i<edges_.size() ? edges_[i].shifted.a : edges_.back().shifted.b;
            v.x=p.x; v.y=p.y;
            if (i<edges_.size()) v.bulge=edges_[i].bulge();
            vertices.push_back(v); origins.push_back(static_cast<int>(i));
        }
        const LWPolyline output(vertices,model_.closed(),model_.constantWidth(),model_.elevation(),model_.extrusion());
        output.worldSegments();
        simple(output);
        return updateLWPolyline(raw_,output,origins);
    }
private:
    void join(std::size_t index) {
        auto& previous=edges_[(index+edges_.size()-1)%edges_.size()];
        auto& next=edges_[index];
        if (close(previous.shifted.b,next.shifted.a)) {
            const auto p=Point2D{previous.shifted.b.x/2+next.shifted.a.x/2,previous.shifted.b.y/2+next.shifted.a.y/2};
            previous.shifted.b=next.shifted.a=p; return;
        }
        const auto u=tangent(previous.original,true), v=tangent(next.original,false);
        if (dot(u,v)<-1+1e-12) throw std::invalid_argument("Offset reverses at a polyline corner");
        const auto candidates=intersections(previous.shifted,next.shifted);
        double best=std::numeric_limits<double>::infinity(); Point2D chosen{};
        for (auto p : candidates) {
            const double score=length(sub(p,previous.shifted.b))+length(sub(p,next.shifted.a));
            if (std::isfinite(score) && score<best) { best=score; chosen=p; }
        }
        if (std::isfinite(best)) {
            const auto& vertex=model_.vertices()[index];
            if (length(sub(chosen,{vertex.x,vertex.y}))>100*std::abs(distance_))
                throw std::invalid_argument("Offset corner miter is too long");
            previous.shifted.b=next.shifted.a=chosen;
        } else if (cross(u,v)*distance_<0) {
            // Disjoint outer supports are joined by a tangent offset of the
            // original corner point, retaining exact circular geometry.
            const auto& vertex=model_.vertices()[index]; const Point2D center{vertex.x,vertex.y};
            const auto a=sub(previous.shifted.b,center), b=sub(next.shifted.a,center);
            const double sign=distance_>0 ? -1 : 1;
            const double sweep=sign*wrap(sign*std::atan2(cross(a,b),dot(a,b)));
            if (std::abs(sweep)>=pi || std::abs(sweep)*std::abs(distance_)<=tolerance)
                throw std::invalid_argument("Offset corner cannot be joined without reversing");
            joins_[index]={true,std::tan(sweep/4)};
        } else throw std::invalid_argument("Offset segments do not meet; use a smaller distance");
    }
    std::string raw_;
    LWPolyline model_;
    double distance_;
    std::vector<OffsetEdge> edges_;
    std::vector<Join> joins_;
};
}
std::string Drafting::offsetPolyline(const std::string& raw,double distance) {
    return PolylineOffset(raw,distance).result();
}
}
