#include <DXFCAD/Drafting.h>
#include <DXFCAD/OCSFrame.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace DXFCAD;
namespace {
void check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
void near(double a,double b) { check(std::abs(a-b)<1e-7,"Unexpected chamfer coordinate"); }
void same(Vector3D a,Vector3D b) { near(a.x,b.x); near(a.y,b.y); near(a.z,b.z); }
template<typename F> void rejects(F f) {
    try { f(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid line chamfer accepted");
}
void connected(const LineChamferResult& r,bool keepA,bool keepB) {
    const auto a=parseEntity(r.first)->geometry().segments[0], b=parseEntity(r.second)->geometry().segments[0];
    const auto connector=parseEntity(r.connector)->geometry().segments[0];
    same(connector.start,keepA ? a.end : a.start); same(connector.end,keepB ? b.end : b.start);
    check(!connector.arc && parseEntity(r.connector)->type()=="LINE","Chamfer must create a native LINE");
}
}
int main() {
    try {
        const auto a=createLine({0,0,3},{10,0,3}), b=createLine({10,0,3},{10,10,3});
        const auto standard=Drafting::chamferLines(a,b,2,3);
        connected(standard,true,false);
        const auto edge=parseEntity(standard.connector)->geometry().segments[0];
        same(edge.start,{8,0,3}); same(edge.end,{10,3,3});
        const auto h=createLine({-10,0,0},{10,0,0}), v=createLine({0,-10,0},{0,10,0});
        for (bool keepA : {false,true}) for (bool keepB : {false,true}) {
            const auto r=Drafting::chamferLines(h,v,2,3,keepA,keepB);
            connected(r,keepA,keepB);
            const auto c=parseEntity(r.connector)->geometry().segments[0];
            same(c.start,{keepA ? -2.0 : 2.0,0,0}); same(c.end,{0,keepB ? -3.0 : 3.0,0});
            const auto edited=parseEntity(r.first)->geometry().segments[0];
            same(keepA ? edited.start : edited.end,keepA ? Vector3D{-10,0,0} : Vector3D{10,0,0});
        }
        const auto extended=Drafting::chamferLines(createLine({0,0,0},{5,0,0}),createLine({10,5,0},{10,10,0}),2,3);
        same(parseEntity(extended.first)->geometry().segments[0].end,{8,0,0});
        same(parseEntity(extended.second)->geometry().segments[0].start,{10,3,0});
        const auto reversed=Drafting::chamferLines(createLine({10,0,3},{0,0,3}),createLine({10,10,3},{10,0,3}),2,3,false,true);
        same(parseEntity(reversed.connector)->geometry().segments[0].start,{8,0,3});
        same(parseEntity(reversed.connector)->geometry().segments[0].end,{10,3,3});
        for (double degrees : {30.0,60.0,120.0,150.0}) {
            const double angle=degrees*std::acos(-1.0)/180;
            const auto r=Drafting::chamferLines(createLine({0,0,0},{20,0,0}),createLine({0,0,0},{20*std::cos(angle),20*std::sin(angle),0}),2,3,false,false);
            connected(r,false,false);
            const auto c=parseEntity(r.connector)->geometry().segments[0];
            same(c.start,{2,0,0}); same(c.end,{3*std::cos(angle),3*std::sin(angle),0});
        }
        const OCSFrame frame({1,2,3});
        const auto tilted=Drafting::chamferLines(createLine(frame.toWorld({0,0},7),frame.toWorld({10,0},7)),
                                                createLine(frame.toWorld({10,0},7),frame.toWorld({10,10},7)),2,3);
        connected(tilted,true,false);
        const auto tiltEdge=parseEntity(tilted.connector)->geometry().segments[0];
        same(frame.fromWorld(tiltEdge.start),{8,0,7}); same(frame.fromWorld(tiltEdge.end),{10,3,7});
        const auto rich=a+"5\nAB\n330\nCD\n6\nDASHED\n62\n1\n370\n25\n420\n16711680\n102\n{APP\n11\n99\n102\n}\n1001\nApp\n1000\nkeep line data\n";
        const auto styled=Drafting::chamferLines(rich,b,2,3);
        const auto original=parseEntity(styled.first), connector=parseEntity(styled.connector);
        check(original->handle()=="AB" && original->record().value(330)=="CD","Source identity lost");
        check(styled.first.find("102\n{APP\n11\n99\n102\n}")!=std::string::npos && styled.first.find("1000\nkeep line data\n")!=std::string::npos,"Opaque source data lost");
        check(connector->handle().empty() && connector->record().value(330).empty() && connector->record().value(1001).empty(),"Connector inherited identity or application data");
        check(connector->record().value(6)=="DASHED" && connector->record().integer(62)==1 && connector->record().integer(370)==25 && connector->record().integer(420)==16711680,"Connector style not inherited");
        for (double distance : {0.0,-1.0,10.0,20.0,static_cast<double>(NAN),static_cast<double>(INFINITY)}) {
            rejects([&] { Drafting::chamferLines(a,b,distance,3); });
            rejects([&] { Drafting::chamferLines(a,b,2,distance); });
        }
        rejects([&] { Drafting::chamferLines(a,b,2,3,false,false); });
        rejects([&] { Drafting::chamferLines(a,createLine({0,2,3},{10,2,3}),2,3); });
        rejects([&] { Drafting::chamferLines(a,createLine({5,0,3},{20,0,3}),2,3); });
        rejects([&] { Drafting::chamferLines(a,createLine({10,0,4},{10,10,4}),2,3); });
        rejects([&] { Drafting::chamferLines(a,createCircle({0,0,0},2),2,3); });
        rejects([&] { Drafting::chamferLines("0\nLINE\n10\n0\n20\n0\n11\n0\n21\n0\n",b,2,3); });
        const auto source=DXFDocument::parse(DXFDocument::empty().serialize({}, {}, {a,b}));
        const auto result=Drafting::chamferLines(source.entities[0]->raw(),source.entities[1]->raw(),2,3);
        const auto restored=DXFDocument::parse(source.serialize({{0,result.first},{1,result.second}}, {}, {result.connector}));
        check(restored.entities.size()==3 && restored.entities[0]->handle()==source.entities[0]->handle() && restored.entities[1]->handle()==source.entities[1]->handle(),"Chamfer export lost source handles");
        check(restored.entities[2]->handle()!=restored.entities[0]->handle() && restored.entities[2]->handle()!=restored.entities[1]->handle(),"Chamfer connector handle collision");
        same(restored.entities[2]->geometry().segments[0].start,{8,0,3});
        std::cout<<"Separate LINE chamfers: distances, sides, extension, planes, styles, metadata and export passed.\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
