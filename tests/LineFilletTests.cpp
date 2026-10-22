#include <DXFCAD/Drafting.h>
#include <DXFCAD/OCSFrame.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace DXFCAD;
namespace {
void check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
void near(double a,double b) { check(std::abs(a-b)<1e-7,"Unexpected fillet coordinate"); }
void same(Vector3D a,Vector3D b) { near(a.x,b.x); near(a.y,b.y); near(a.z,b.z); }
template<typename F> void rejects(F f) {
    try { f(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid line fillet accepted");
}
void tangency(const LineFilletResult& result,double radius,bool keepA,bool keepB) {
    const auto a=parseEntity(result.first)->geometry().segments[0], b=parseEntity(result.second)->geometry().segments[0];
    const auto arc=parseEntity(result.arc)->geometry().segments[0];
    same(arc.start,keepA ? a.end : a.start); same(arc.end,keepB ? b.end : b.start);
    near(arc.arc->radius,radius);
    auto perpendicular=[&](const LWPolylineSegment3D& s,Vector3D p) {
        const auto c=arc.arc->center;
        const double dot=(s.end.x-s.start.x)*(p.x-c.x)+(s.end.y-s.start.y)*(p.y-c.y)+(s.end.z-s.start.z)*(p.z-c.z);
        near(dot,0);
    };
    perpendicular(a,arc.start); perpendicular(b,arc.end);
    check(arc.arc->sweep>0 && arc.arc->sweep<std::acos(-1.0),"Fillet should be a minor arc");
}
}
int main() {
    try {
        const auto first=createLine({0,0,3},{10,0,3}), second=createLine({10,0,3},{10,10,3});
        const auto standard=Drafting::filletLines(first,second,2);
        tangency(standard,2,true,false);
        same(parseEntity(standard.first)->geometry().segments[0].end,{8,0,3});
        same(parseEntity(standard.second)->geometry().segments[0].start,{10,2,3});
        same(parseEntity(standard.arc)->geometry().segments[0].arc->center,{8,2,3});
        const auto horizontal=createLine({-10,0,0},{10,0,0}), vertical=createLine({0,-10,0},{0,10,0});
        for (bool keepA : {false,true}) for (bool keepB : {false,true}) {
            const auto r=Drafting::filletLines(horizontal,vertical,2,keepA,keepB);
            tangency(r,2,keepA,keepB);
            same(parseEntity(r.arc)->geometry().segments[0].arc->center,{keepA ? -2.0 : 2.0,keepB ? -2.0 : 2.0,0});
            same(keepA ? parseEntity(r.first)->geometry().segments[0].start : parseEntity(r.first)->geometry().segments[0].end,
                 keepA ? Vector3D{-10,0,0} : Vector3D{10,0,0});
        }
        const auto extend=Drafting::filletLines(createLine({0,0,0},{5,0,0}),createLine({10,5,0},{10,10,0}),2);
        tangency(extend,2,true,false);
        same(parseEntity(extend.first)->geometry().segments[0].end,{8,0,0});
        same(parseEntity(extend.second)->geometry().segments[0].start,{10,2,0});
        const auto reverse=Drafting::filletLines(createLine({10,0,3},{0,0,3}),createLine({10,10,3},{10,0,3}),2,false,true);
        same(parseEntity(reverse.arc)->geometry().segments[0].arc->center,{8,2,3});
        for (double degrees : {30.0,60.0,120.0,150.0}) {
            const double angle=degrees*std::acos(-1.0)/180;
            const auto r=Drafting::filletLines(createLine({0,0,0},{20,0,0}),createLine({0,0,0},{20*std::cos(angle),20*std::sin(angle),0}),1,false,false);
            tangency(r,1,false,false);
            near(parseEntity(r.first)->geometry().segments[0].start.x,1/std::tan(angle/2));
            near(parseEntity(r.arc)->geometry().segments[0].arc->sweep,std::acos(-1.0)-angle);
        }
        const OCSFrame frame({1,2,3});
        const auto tiltA=createLine(frame.toWorld({0,0},7),frame.toWorld({10,0},7));
        const auto tiltB=createLine(frame.toWorld({10,0},7),frame.toWorld({10,10},7));
        const auto tilted=Drafting::filletLines(tiltA,tiltB,2);
        tangency(tilted,2,true,false);
        same(frame.fromWorld(parseEntity(tilted.arc)->geometry().segments[0].arc->center),{8,2,7});
        const auto rich=first+"5\nAB\n330\nCD\n6\nDASHED\n62\n1\n370\n25\n420\n16711680\n102\n{APP\n11\n99\n102\n}\n1001\nApp\n1000\nkeep line data\n";
        const auto metadata=Drafting::filletLines(rich,second,2);
        const auto edited=parseEntity(metadata.first), arc=parseEntity(metadata.arc);
        check(edited->handle()=="AB" && edited->record().value(330)=="CD","Fillet changed line identity");
        check(metadata.first.find("102\n{APP\n11\n99\n102\n}")!=std::string::npos && metadata.first.find("1000\nkeep line data\n")!=std::string::npos,"Fillet lost opaque data");
        check(arc->type()=="ARC" && arc->handle().empty() && arc->record().value(330).empty(),"New ARC inherited identity");
        check(arc->record().value(1001).empty() && arc->record().value(11).empty(),"New ARC inherited LINE data");
        check(arc->record().value(6)=="DASHED" && arc->record().integer(62)==1 && arc->record().integer(370)==25 && arc->record().integer(420)==16711680,"ARC display styles not inherited");
        rejects([&] { Drafting::filletLines(first,second,0); });
        rejects([&] { Drafting::filletLines(first,second,-1); });
        rejects([&] { Drafting::filletLines(first,second,NAN); });
        rejects([&] { Drafting::filletLines(first,second,INFINITY); });
        rejects([&] { Drafting::filletLines(first,second,10); });
        rejects([&] { Drafting::filletLines(first,second,20); });
        rejects([&] { Drafting::filletLines(first,second,2,false,false); });
        rejects([&] { Drafting::filletLines(first,createLine({0,2,3},{10,2,3}),2); });
        rejects([&] { Drafting::filletLines(first,createLine({5,0,3},{20,0,3}),2); });
        rejects([&] { Drafting::filletLines(first,createLine({10,0,4},{10,10,4}),2); });
        rejects([&] { Drafting::filletLines(first,createCircle({0,0,0},2),2); });
        rejects([&] { Drafting::filletLines("0\nLINE\n10\n0\n20\n0\n11\n0\n21\n0\n",second,2); });
        const auto source=DXFDocument::parse(DXFDocument::empty().serialize({}, {}, {first,second}));
        const auto exported=Drafting::filletLines(source.entities[0]->raw(),source.entities[1]->raw(),2);
        const auto reopened=DXFDocument::parse(source.serialize({{0,exported.first},{1,exported.second}}, {}, {exported.arc}));
        check(reopened.entities.size()==3 && reopened.entities[0]->handle()==source.entities[0]->handle()
              && reopened.entities[1]->handle()==source.entities[1]->handle() && reopened.entities[2]->type()=="ARC","Fillet export lost native identities");
        check(reopened.entities[2]->handle()!=reopened.entities[0]->handle() && reopened.entities[2]->handle()!=reopened.entities[1]->handle(),"Fillet ARC handle collision");
        std::cout<<"Separate LINE fillets: all sides, tangency, extension, tilted planes, styles, metadata and export passed.\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
