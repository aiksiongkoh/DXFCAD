#include <DXFCAD/DXFDocument.h>
#include <DXFCAD/OCSFrame.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace DXFCAD;
namespace {
const double pi=std::acos(-1.0);
void check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
void near(double a,double b) { check(std::abs(a-b)<1e-7,"Unexpected ARC geometry"); }
void same(Vector3D a,Vector3D b) { near(a.x,b.x); near(a.y,b.y); near(a.z,b.z); }
template<typename F> void rejects(F f) {
    try { f(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid ARC creation accepted");
}
}
int main() {
    try {
        const auto raw=createArc({1,2,3},5,0,90);
        const auto entity=parseEntity(raw); const auto g=entity->geometry().segments[0];
        check(entity->type()=="ARC" && entity->handle().empty() && entity->record().value(330).empty(),"New ARC is not identity-free");
        same(g.start,{6,2,3}); same(g.end,{1,7,3}); same(g.arc->center,{1,2,3});
        near(g.arc->radius,5); near(g.arc->sweep,pi/2); same(g.arc->normal,{0,0,1});
        const auto clockwise=parseEntity(createArc({0,0,0},5,0,90,true))->geometry().segments[0];
        same(clockwise.start,{5,0,0}); same(clockwise.end,{0,5,0});
        near(clockwise.arc->sweep,3*pi/2); same(clockwise.arc->normal,{0,0,-1});
        near(parseEntity(createArc({0,0,0},5,350,10))->geometry().segments[0].arc->sweep,20*pi/180);
        near(parseEntity(createArc({0,0,0},5,10,350,true))->geometry().segments[0].arc->sweep,20*pi/180);
        near(parseEntity(createArc({0,0,0},5,-10,370))->geometry().segments[0].arc->sweep,20*pi/180);
        check(parseEntity(createArc({0,0,0},5,1e308,42))->geometry().segments.size()==1,"Finite large angles rejected");
        const auto three=createArcThroughPoints({5,0,2},{0,5,2},{-5,0,2});
        const auto arc=parseEntity(three)->geometry().segments[0];
        same(arc.start,{5,0,2}); same(arc.end,{-5,0,2}); same(arc.arc->midpoint,{0,5,2});
        same(arc.arc->center,{0,0,2}); near(arc.arc->sweep,pi); near(arc.arc->radius,5);
        const auto cw=parseEntity(createArcThroughPoints({5,0,2},{0,-5,2},{-5,0,2}))->geometry().segments[0];
        same(cw.arc->normal,{0,0,-1}); same(cw.arc->midpoint,{0,-5,2}); near(cw.arc->sweep,pi);
        const auto major=parseEntity(createArcThroughPoints({5,0,0},{-5,0,0},{0,5,0}))->geometry().segments[0];
        near(major.arc->sweep,3*pi/2); same(major.arc->normal,{0,0,-1});
        for (auto normal : {ExtrusionDirection{1,2,3},ExtrusionDirection{0,0,-2}}) {
            const OCSFrame frame(normal);
            const auto r=parseEntity(createArcThroughPoints(frame.toWorld({5,0},7),frame.toWorld({0,5},7),frame.toWorld({-5,0},7)));
            const auto edge=r->geometry().segments[0];
            same(edge.arc->center,frame.toWorld({0,0},7)); near(edge.arc->radius,5);
            same(edge.arc->normal,frame.normal()); same(edge.arc->midpoint,frame.toWorld({0,5},7));
        }
        // Translation and unequal chord lengths must not bias the circle solve.
        const auto shifted=parseEntity(createArcThroughPoints({1000005,-2000000,4},{1000003,-1999996,4},{999995,-2000000,4}))->geometry().segments[0];
        same(shifted.arc->center,{1000000,-2000000,4}); near(shifted.arc->radius,5);
        const auto tiny=parseEntity(createArcThroughPoints({1e-100,0,0},{0,1e-100,0},{-1e-100,0,0}))->geometry().segments[0];
        check(std::abs(tiny.arc->radius/1e-100-1)<1e-12,"Small point distances underflowed");
        check(parseEntity(createArc({0,0,0},2,0,90,false,"Arcs"))->layer()=="Arcs","ARC layer lost");
        for (double invalid : {0.0,-1.0,static_cast<double>(NAN),static_cast<double>(INFINITY)})
            rejects([&] { createArc({0,0,0},invalid,0,90); });
        for (auto angles : {std::pair<double,double>{0,360},{-10,350},{360,720},{-360,0},{30,30}})
            rejects([&] { createArc({0,0,0},2,angles.first,angles.second); });
        rejects([&] { createArc({NAN,0,0},2,0,90); });
        rejects([&] { createArc({0,0,0},2,NAN,90); });
        rejects([&] { createArc({0,0,0},2,0,INFINITY); });
        rejects([&] { createArc({0,0,0},2,0,90,false,""); });
        rejects([&] { createArc({0,0,0},2,0,90,false,"Bad\nLayer"); });
        rejects([&] { createArc({1e30,1e30,0},1,0,90); });
        rejects([&] { createArcThroughPoints({0,0,0},{0,0,0},{1,1,0}); });
        rejects([&] { createArcThroughPoints({0,0,0},{1,1,0},{0,0,0}); });
        rejects([&] { createArcThroughPoints({0,0,0},{1,1,1},{2,2,2}); });
        rejects([&] { createArcThroughPoints({0,0,0},{1,1e-14,0},{2,0,0}); });
        rejects([&] { createArcThroughPoints({0,0,0},{NAN,1,0},{2,0,0}); });
        const auto restored=DXFDocument::parse(DXFDocument::empty().serialize({}, {}, {raw,three,createArc({0,0,0},2,0,270,true)}));
        check(restored.entities.size()==3 && restored.entities[0]->handle()!=restored.entities[1]->handle(),"ARC export handles not allocated");
        same(restored.entities[1]->geometry().segments[0].arc->midpoint,{0,5,2});
        std::cout<<"Native ARC creation: numeric/three-point, direction, major arcs, planes, validation and export passed.\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
