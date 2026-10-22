#include <DXFCAD/SnapGeometry.h>
#include <DXFCAD/OCSFrame.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace DXFCAD;
namespace {
void check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
void near(double a,double b) { check(std::abs(a-b)<1e-7,"Unexpected snap coordinate"); }
std::vector<DXFSnapPoint> hits(const std::string& a,const std::string& b) {
    return SnapGeometry(*parseEntity(a)).intersections(SnapGeometry(*parseEntity(b)));
}
bool contains(const std::vector<DXFSnapPoint>& points,Vector3D p) {
    for (const auto& hit : points) if (std::hypot(hit.position.x-p.x,hit.position.y-p.y,hit.position.z-p.z)<1e-7) return true;
    return false;
}
template<typename F> void rejects(F f) {
    try { f(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid snap frame accepted");
}
}
int main() {
    try {
        const auto line=createLine({-10,0,0},{10,0,0});
        const auto vertical=createLine({2,-10,0},{2,10,0});
        const auto entity=parseEntity(line);
        const auto points=entity->snapPoints();
        check(points.size()==3 && points[2].kind=="midpoint","Missing LINE midpoint");
        near(points[2].position.x,0); near(points[2].position.y,0);
        const auto arc=std::string("0\nARC\n10\n0\n20\n0\n40\n5\n50\n0\n51\n270\n");
        const auto arcPoints=parseEntity(arc)->snapPoints();
        check(arcPoints.back().kind=="midpoint","Missing ARC midpoint");
        near(arcPoints.back().position.x,-5/std::sqrt(2.0)); near(arcPoints.back().position.y,5/std::sqrt(2.0));
        const auto poly=createLWPolyline(LWPolyline({{-5,0,1},{5,0},{5,5}},false,3,2));
        const auto pp=parseEntity(poly)->snapPoints();
        check(pp.size()==5 && pp[3].kind=="midpoint" && pp[4].kind=="midpoint","Polyline midpoint topology wrong");
        near(pp[3].position.y,-5); near(pp[3].position.z,2); near(pp[4].position.y,2.5);
        const auto cw=createLWPolyline(LWPolyline({{-5,0,-1},{5,0}}));
        near(parseEntity(cw)->snapPoints().back().position.y,5);
        const auto closed=createLWPolyline(LWPolyline({{0,0},{4,0},{4,4}},true));
        const auto cp=parseEntity(closed)->snapPoints();
        check(cp.size()==6,"Missing closing-edge midpoint"); near(cp.back().position.x,2); near(cp.back().position.y,2);
        const auto crossing=hits(line,vertical);
        check(crossing.size()==1 && crossing[0].kind=="intersection","Missing finite line intersection");
        near(crossing[0].position.x,2);
        check(hits(createLine({-1,0,0},{1,0,0}),vertical).empty(),"Extended intersection offered");
        check(hits(line,createLine({2,-10,1},{2,10,1})).empty(),"Apparent crossing offered");
        check(hits(line,createLine({-5,0,0},{5,0,0})).empty(),"Collinear overlap offered");
        const auto circle=createCircle({0,0,0},5);
        const auto lc=hits(line,circle);
        check(lc.size()==2 && contains(lc,{-5,0,0}) && contains(lc,{5,0,0}),"Line/circle intersections wrong");
        check(hits(createLine({-10,5,0},{10,5,0}),circle).size()==1,"Tangent duplicated");
        const auto circle2=createCircle({6,0,0},5);
        const auto cc=hits(circle,circle2);
        check(cc.size()==2 && contains(cc,{3,4,0}) && contains(cc,{3,-4,0}),"Circle/circle intersections wrong");
        check(hits(circle,circle).empty(),"Coincident circles offered");
        check(hits(circle,createCircle({10,0,0},5)).size()==1,"Circle tangency duplicated");
        const auto quarter=std::string("0\nARC\n10\n0\n20\n0\n40\n5\n50\n0\n51\n90\n");
        check(hits(line,quarter).size()==1 && contains(hits(line,quarter),{5,0,0}),"Arc endpoint or finite sweep wrong");
        check(hits(createLine({-10,-3,0},{10,-3,0}),quarter).empty(),"Arc extension offered");
        const auto aq=hits(quarter,circle2);
        check(aq.size()==1 && contains(aq,{3,4,0}),"Arc/circle finite sweep wrong");
        const auto below=createLWPolyline(LWPolyline({{-5,0,1},{5,0}}));
        const auto above=createLWPolyline(LWPolyline({{-5,0,-1},{5,0}}));
        check(contains(hits(below,createLine({0,-10,0},{0,10,0})),{0,-5,0}),"Bulged segment intersection wrong");
        check(contains(hits(above,createLine({0,-10,0},{0,10,0})),{0,5,0}),"Clockwise bulge intersection wrong");
        check(hits(below,above).empty(),"Coincident circular supports should not produce ambiguous hits");
        const auto polyBoundary=createLWPolyline(LWPolyline({{2,-5},{2,0},{2,5}}));
        check(hits(line,polyBoundary).size()==1,"Shared polyline vertex duplicated");
        const auto pointsBefore=entity->raw();
        const SnapGeometry shifted(*entity,{0,3,0});
        const auto translated=shifted.intersections(SnapGeometry(*parseEntity(vertical)));
        check(contains(translated,{2,3,0}) && entity->raw()==pointsBefore,"Placement modified native data");
        const OCSFrame frame({1,2,3});
        const SnapGeometry tiltedLine(*entity,frame.toWorld({0,0},7),frame.xAxis(),frame.yAxis(),frame.normal());
        const SnapGeometry tiltedVertical(*parseEntity(vertical),frame.toWorld({0,0},7),frame.xAxis(),frame.yAxis(),frame.normal());
        check(contains(tiltedLine.intersections(tiltedVertical),frame.toWorld({2,0},7)),"Tilted placement intersection wrong");
        const auto tiltedPoly=createLWPolyline(LWPolyline({{-5,0,1},{5,0}},false,0,7,{1,2,3}));
        const auto tiltCut=createLine(frame.toWorld({0,-10},7),frame.toWorld({0,10},7));
        check(contains(hits(tiltedPoly,tiltCut),frame.toWorld({0,-5},7)),"Tilted OCS arc intersection wrong");
        const auto tiltedMidpoint=parseEntity(tiltedPoly)->snapPoints().back().position;
        near(frame.fromWorld(tiltedMidpoint).y,-5);
        const auto bounds=tiltedLine.boundPoints();
        check(bounds.size()==8,"Missing snap bounds");
        for (const auto& p : tiltedLine.intersections(tiltedVertical)) {
            const double inf=std::numeric_limits<double>::infinity();
            Vector3D low{inf,inf,inf},high{-inf,-inf,-inf};
            for (auto b : bounds) {
                low={std::min(low.x,b.x),std::min(low.y,b.y),std::min(low.z,b.z)};
                high={std::max(high.x,b.x),std::max(high.y,b.y),std::max(high.z,b.z)};
            }
            check(p.position.x>=low.x-1e-7 && p.position.x<=high.x+1e-7 && p.position.y>=low.y-1e-7
                  && p.position.y<=high.y+1e-7 && p.position.z>=low.z-1e-7 && p.position.z<=high.z+1e-7,"Intersection outside conservative bounds");
        }
        rejects([&] { SnapGeometry bad(*entity,{0,0,0},{2,0,0}); });
        rejects([&] { SnapGeometry bad(*entity,{NAN,0,0}); });
        std::cout<<"Native curve midpoints, finite intersections, placements, deduplication and apparent-crossing rejection passed.\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
