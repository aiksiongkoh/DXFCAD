#include <DXFCAD/PaperGeometry.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace DXFCAD {
namespace {
Vector3D between(const Vector3D& a,const Vector3D& b,double t) {
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,0};
}
void validate(const Vector3D& p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y))
        throw std::invalid_argument("Paper coordinates must be finite");
}
}
bool PaperRectangle::contains(const Vector3D& p) const {
    return p.x>=minX && p.x<=maxX && p.y>=minY && p.y<=maxY;
}
PaperClip::PaperClip(PaperRectangle rectangle) : rectangle_(rectangle) {
    validate({rectangle.minX,rectangle.minY,0}); validate({rectangle.maxX,rectangle.maxY,0});
    if (rectangle.minX>=rectangle.maxX || rectangle.minY>=rectangle.maxY)
        throw std::invalid_argument("Paper clipping rectangle must have positive dimensions");
}
std::vector<std::vector<Vector3D>> PaperClip::polyline(const std::vector<Vector3D>& points) const {
    for (const auto& p : points) validate(p);
    std::vector<std::vector<Vector3D>> result;
    // Liang-Barsky: clip each segment parametrically against four half-planes.
    for (std::size_t i=1;i<points.size();++i) {
        const auto& a=points[i-1]; const auto& b=points[i];
        const double dx=b.x-a.x,dy=b.y-a.y;
        const double p[]={-dx,dx,-dy,dy};
        const double q[]={a.x-rectangle_.minX,rectangle_.maxX-a.x,a.y-rectangle_.minY,rectangle_.maxY-a.y};
        double first=0,last=1;
        bool visible=true;
        for (int k=0;k<4;++k) {
            if (p[k]==0) { if (q[k]<0) visible=false; }
            else if (p[k]<0) first=std::max(first,q[k]/p[k]);
            else last=std::min(last,q[k]/p[k]);
        }
        if (!visible || first>last) continue;
        const auto bounded=[&](Vector3D point) {
            point.x=std::clamp(point.x,rectangle_.minX,rectangle_.maxX);
            point.y=std::clamp(point.y,rectangle_.minY,rectangle_.maxY);
            return point;
        };
        const auto start=bounded(between(a,b,first)),end=bounded(between(a,b,last));
        // Separate segments avoid joining across excluded portions of a curve.
        result.push_back({start,end});
    }
    return result;
}
std::vector<Vector3D> PaperClip::polygon(const std::vector<Vector3D>& points) const {
    for (const auto& p : points) validate(p);
    auto output=points;
    // Sutherland-Hodgman clipping; preserve winding for face holes.
    for (int side=0;side<4 && !output.empty();++side) {
        auto input=std::move(output); output.clear();
        const auto distance=[&](const Vector3D& p) {
            switch(side) {
            case 0:return p.x-rectangle_.minX;
            case 1:return rectangle_.maxX-p.x;
            case 2:return p.y-rectangle_.minY;
            default:return rectangle_.maxY-p.y;
            }
        };
        auto previous=input.back(); double before=distance(previous);
        for (const auto& current : input) {
            const double after=distance(current);
            if ((before>=0)!=(after>=0)) {
                auto intersection=between(previous,current,before/(before-after));
                // Set only this half-plane's boundary. The other coordinate
                // must stay unchanged until its own clipping pass.
                if (side==0) intersection.x=rectangle_.minX;
                if (side==1) intersection.x=rectangle_.maxX;
                if (side==2) intersection.y=rectangle_.minY;
                if (side==3) intersection.y=rectangle_.maxY;
                output.push_back(intersection);
            }
            if (after>=0) output.push_back({current.x,current.y,0});
            previous=current; before=after;
        }
    }
    for (auto& point : output) {
        point.x=std::clamp(point.x,rectangle_.minX,rectangle_.maxX);
        point.y=std::clamp(point.y,rectangle_.minY,rectangle_.maxY);
    }
    return output;
}
} // namespace DXFCAD
