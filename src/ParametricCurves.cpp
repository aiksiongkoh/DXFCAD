#include <DXFCAD/ParametricCurves.h>
#include <DXFCAD/Drafting.h>
#include <DXFCAD/OCSFrame.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <locale>
#include <limits>

namespace DXFCAD {
namespace {
constexpr double pi=3.14159265358979323846;
double dot(Vector3D a,Vector3D b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
double norm(Vector3D a) { return std::hypot(a.x,a.y,a.z); }
Vector3D cross(Vector3D a,Vector3D b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
}
EllipseCurve::EllipseCurve(DXFRecord record) : DXFEntity(std::move(record))
{
    const auto f=nativeEntityFields(record_);
    center_={f.number(10),f.number(20),f.number(30)};
    major_={f.number(11),f.number(21),f.number(31)};
    const OCSFrame frame({f.number(210),f.number(220),f.number(230,1)});
    const auto normal=frame.normal();
    const double length=norm(major_),ratio=f.number(40);
    if (length<=0 || ratio<=0 || ratio>1 || std::abs(dot(major_,normal))>1e-9*length)
        throw std::invalid_argument("Invalid native ELLIPSE axes");
    minor_=cross(normal,major_); minor_={minor_.x*ratio,minor_.y*ratio,minor_.z*ratio};
    start_=f.number(41); end_=f.number(42,2*pi);
    if (end_<=start_) end_+=2*pi;
    if (end_-start_>2*pi+1e-12) throw std::invalid_argument("ELLIPSE parameter range exceeds a turn");
}
Vector3D EllipseCurve::atParameter(double t) const
{
    return {center_.x+major_.x*std::cos(t)+minor_.x*std::sin(t),
            center_.y+major_.y*std::cos(t)+minor_.y*std::sin(t),
            center_.z+major_.z*std::cos(t)+minor_.z*std::sin(t)};
}
double EllipseCurve::radiusOfCurvature(double t) const
{
    const auto a=norm(major_),b=norm(minor_);
    const double x=-a*std::sin(t),y=b*std::cos(t);
    return std::pow(x*x+y*y,1.5)/(a*b);
}
EntityGeometry EllipseCurve::geometry() const
{
    // Conservative exact bounds; consumers requiring a curve use this native
    // parametric model. Never masquerade the ellipse as circular segments.
    EntityGeometry result;
    const Vector3D radius{std::hypot(major_.x,minor_.x),std::hypot(major_.y,minor_.y),std::hypot(major_.z,minor_.z)};
    for (double x : {-1.,1.}) for (double y : {-1.,1.}) for (double z : {-1.,1.})
        result.bounds.push_back({center_.x+x*radius.x,center_.y+y*radius.y,center_.z+z*radius.z});
    return result;
}
std::string EllipseCurve::translated(const Vector3D& offset) const { return translatedPoints(offset,{10},false); }
std::vector<DXFSnapPoint> EllipseCurve::snapPoints() const
{
    return {{center_,"center"},{atParameter(start_),"endpoint"},{atParameter(end_),"endpoint"}};
}
SplineCurve::SplineCurve(DXFRecord record) : DXFEntity(std::move(record))
{
    const auto f=nativeEntityFields(record_); degree_=f.integer(71); periodic_=(f.integer(70)&2)!=0;
    for (const auto& group : f.groups) {
        if (group.first==10) points_.push_back({DXFRecord{{group}}.number(group.first),0,0});
        else if (group.first==20 && !points_.empty()) points_.back().y=DXFRecord{{group}}.number(group.first);
        else if (group.first==30 && !points_.empty()) points_.back().z=DXFRecord{{group}}.number(group.first);
        else if (group.first==40) knots_.push_back(DXFRecord{{group}}.number(group.first));
        else if (group.first==41) weights_.push_back(DXFRecord{{group}}.number(group.first));
    }
    if (degree_<1 || points_.size()<=static_cast<std::size_t>(degree_) || knots_.size()!=points_.size()+degree_+1 ||
        !std::is_sorted(knots_.begin(),knots_.end())) throw std::invalid_argument("SPLINE needs a valid degree, control net and knot vector");
    if (weights_.empty()) weights_.assign(points_.size(),1);
    if (weights_.size()!=points_.size()) throw std::invalid_argument("SPLINE weight count differs from control count");
    for (const auto p : points_) if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) throw std::invalid_argument("Nonfinite SPLINE control point");
    for (double w : weights_) if (!std::isfinite(w) || w<=0) throw std::invalid_argument("SPLINE weights must be finite and positive");
    for (double k : knots_) if (!std::isfinite(k)) throw std::invalid_argument("Nonfinite SPLINE knot");
    if (endParameter()<=startParameter()) throw std::invalid_argument("Empty SPLINE parameter range");
}
double SplineCurve::startParameter() const { return knots_.at(degree_); }
double SplineCurve::endParameter() const { return knots_.at(points_.size()); }
Vector3D SplineCurve::atParameter(double t) const
{
    if (!std::isfinite(t) || t<startParameter() || t>endParameter()) throw std::invalid_argument("SPLINE parameter outside native domain");
    auto span=points_.size()-1;
    if (t<endParameter()) span=static_cast<std::size_t>(std::upper_bound(knots_.begin(),knots_.end(),t)-knots_.begin()-1);
    struct Homogeneous { double x,y,z,w; };
    std::vector<Homogeneous> d;
    for (int j=0;j<=degree_;++j) {
        const auto i=span-degree_+j; const auto p=points_.at(i); const auto w=weights_.at(i);
        d.push_back({p.x*w,p.y*w,p.z*w,w});
    }
    for (int r=1;r<=degree_;++r) for (int j=degree_;j>=r;--j) {
        const auto i=span-degree_+j;
        const double denominator=knots_[i+degree_-r+1]-knots_[i];
        const double alpha=denominator==0 ? 0 : (t-knots_[i])/denominator;
        const auto a=d[j-1],b=d[j];
        d[j]={(1-alpha)*a.x+alpha*b.x,(1-alpha)*a.y+alpha*b.y,(1-alpha)*a.z+alpha*b.z,(1-alpha)*a.w+alpha*b.w};
    }
    const auto h=d.back();
    return {h.x/h.w,h.y/h.w,h.z/h.w};
}
EntityGeometry SplineCurve::geometry() const { EntityGeometry result; result.bounds=points_; return result; }
std::string SplineCurve::translated(const Vector3D& offset) const
{
    if (!std::isfinite(offset.x) || !std::isfinite(offset.y) || !std::isfinite(offset.z)) throw std::invalid_argument("Nonfinite SPLINE displacement");
    const auto number=[](double value) {
        if (!std::isfinite(value)) throw std::overflow_error("Moved SPLINE point exceeds finite range");
        std::ostringstream stream; stream.imbue(std::locale::classic());
        stream<<std::setprecision(std::numeric_limits<double>::max_digits10)<<value;
        return stream.str();
    };
    DXFRecord result;
    bool xdata=false; int depth=0;
    for (std::size_t i=0;i<record_.groups.size();++i) {
        auto group=record_.groups[i];
        if (group.first==1001 && !depth) xdata=true;
        if (group.first==102 && !xdata) {
            if (!group.second.empty() && group.second.front()=='{') ++depth;
            else if (group.second=="}" && depth) --depth;
        }
        if (!xdata && !depth && (group.first==10 || group.first==11 || group.first==20 || group.first==21 || group.first==30 || group.first==31)) {
            const auto axis=group.first/10-1;
            const double delta=axis==0 ? offset.x : axis==1 ? offset.y : offset.z;
            if (delta!=0) group.second=number(DXFRecord{{group}}.number(group.first)+delta);
            result.groups.push_back(group);
            if ((group.first==20 || group.first==21) && offset.z!=0 &&
                (i+1==record_.groups.size() || record_.groups[i+1].first!=group.first+10))
                result.groups.emplace_back(group.first+10,number(offset.z));
        } else result.groups.push_back(group);
    }
    return result.raw();
}
std::vector<DXFSnapPoint> SplineCurve::snapPoints() const
{
    return {{atParameter(startParameter()),"endpoint"},{atParameter(endParameter()),"endpoint"}};
}
std::string affineTransformedCurve(const DXFEntity& entity,const BlockPart& transform)
{
    const auto vector=[&](Vector3D p) {
        auto linear=transform; linear.origin={0,0,0}; return linear.transformPoint(p);
    };
    if (const auto ellipse=dynamic_cast<const EllipseCurve*>(&entity)) {
        const auto u=vector(ellipse->majorAxis()),v=vector(ellipse->minorAxis());
        const auto phase=.5*std::atan2(2*dot(u,v),dot(u,u)-dot(v,v));
        const auto c=std::cos(phase),s=std::sin(phase);
        const Vector3D major{u.x*c+v.x*s,u.y*c+v.y*s,u.z*c+v.z*s};
        const Vector3D minor{-u.x*s+v.x*c,-u.y*s+v.y*c,-u.z*s+v.z*c};
        const auto normal=cross(major,minor); const auto length=norm(normal);
        if (length==0 || norm(major)==0) throw std::invalid_argument("Affine transform collapses ELLIPSE");
        const auto center=transform.transformPoint(ellipse->center());
        const auto result=patchEntityFields(entity.record(),{{10,center.x},{20,center.y},{30,center.z},
            {11,major.x},{21,major.y},{31,major.z},{40,std::min(1.,norm(minor)/norm(major))},
            {210,normal.x/length},{220,normal.y/length},{230,normal.z/length},
            {41,ellipse->startParameter()-phase},{42,ellipse->endParameter()-phase}});
        parseEntity(result)->geometry(); return result;
    }
    if (entity.type()!="SPLINE") throw std::invalid_argument("Expected native ELLIPSE or SPLINE");
    const auto number=[](double value) {
        if (!std::isfinite(value)) throw std::overflow_error("Transformed SPLINE coordinate exceeds finite range");
        std::ostringstream out; out.imbue(std::locale::classic());
        out<<std::setprecision(std::numeric_limits<double>::max_digits10)<<value; return out.str();
    };
    DXFRecord result; bool xdata=false; int depth=0;
    const auto& groups=entity.record().groups;
    for (std::size_t i=0;i<groups.size();++i) {
        const auto& group=groups[i];
        if (group.first==1001 && !depth) xdata=true;
        if (!xdata && group.first==102) {
            if (!group.second.empty() && group.second.front()=='{') ++depth;
            else if (group.second=="}" && depth) --depth;
        }
        if (!xdata && !depth && (group.first==10 || group.first==11 || group.first==12 || group.first==13 || group.first==210)) {
            const int code=group.first; DXFRecord pointRecord{{group}};
            while (i+1<groups.size() && (groups[i+1].first==code+10 || groups[i+1].first==code+20)) pointRecord.groups.push_back(groups[++i]);
            const Vector3D point{pointRecord.number(code),pointRecord.number(code+10),pointRecord.number(code+20)};
            Vector3D moved;
            if (code==210) {
                const auto a=cross(transform.yAxis,transform.zAxis),b=cross(transform.zAxis,transform.xAxis),c=cross(transform.xAxis,transform.yAxis);
                moved={a.x*point.x+b.x*point.y+c.x*point.z,a.y*point.x+b.y*point.y+c.y*point.z,a.z*point.x+b.z*point.y+c.z*point.z};
                const auto n=norm(moved); if (n==0) throw std::invalid_argument("Affine transform collapses SPLINE normal");
                moved={moved.x/n,moved.y/n,moved.z/n};
            } else moved=code==10 || code==11 ? transform.transformPoint(point) : vector(point);
            result.groups.emplace_back(code,number(moved.x)); result.groups.emplace_back(code+10,number(moved.y));
            result.groups.emplace_back(code+20,number(moved.z));
        } else result.groups.push_back(group);
    }
    parseEntity(result.raw())->geometry(); return result.raw();
}

}
