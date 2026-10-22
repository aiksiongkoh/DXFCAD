#include <DXFCAD/CornerEdit.h>
#include <DXFCAD/Drafting.h>
#include <iostream>
#include <stdexcept>
using namespace DXFCAD;
namespace {
void check(bool value) { if (!value) throw std::runtime_error("Corner plan check failed"); }
template<class F> void rejects(F f) {
    try { f(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid corner plan accepted");
}
}
int main() {
    try {
        const std::string a="0\nLINE\n5\nAB\n10\n0\n20\n0\n11\n10\n21\n0\n1001\nTEST\n1000\nkeep corner data\n";
        const std::string b="0\nLINE\n5\nBC\n10\n10\n20\n0\n11\n10\n21\n10\n";
        const auto raw="0\nSECTION\n2\nENTITIES\n"+a+b+"0\nSPLINE\n999\nopaque\n0\nENDSEC\n0\nEOF\n";
        const auto source=DXFDocument::parse(raw);
        const std::vector<std::pair<int,std::string>> live{{0,a},{1,b}};
        const auto f=Drafting::filletLines(a,b,2);
        const CornerEditPlan fillet(source,live,{0,1},{f.first,f.second},f.arc,"ARC");
        check(fillet.updates()[0]==f.first && fillet.updates()[1]==f.second);
        check(parseEntity(fillet.connector())->handle().empty());
        const auto result=DXFDocument::parse(fillet.drawingBytes());
        check(result.entities.size()==3 && result.entities[0]->handle()=="AB" && result.entities[1]->handle()=="BC");
        check(result.entities[2]->type()=="ARC" && result.entities[2]->handle()!="AB" && result.entities[2]->handle()!="BC");
        check(fillet.drawingBytes().find("opaque")!=std::string::npos);
        // Application payload stays with the original LINE, rather than being
        // duplicated onto a new entity with a different identity/type.
        check(fillet.updates()[0].find("keep corner data")!=std::string::npos);
        check(fillet.connector().find("keep corner data")==std::string::npos);
        const auto c=Drafting::chamferLines(a,b,2,3);
        const CornerEditPlan chamfer(source,live,{0,1},{c.first,c.second},c.connector,"LINE");
        check(DXFDocument::parse(chamfer.drawingBytes()).entities.back()->type()=="LINE");
        const auto empty=DXFDocument::empty(4);
        const auto x=parseEntity(a)->copied({0,0,0}),y=parseEntity(b)->copied({0,0,0});
        const auto newCorner=Drafting::filletLines(x,y,2);
        const CornerEditPlan added(empty,{{-1,x},{-1,y}},{0,1},{newCorner.first,newCorner.second},newCorner.arc,"ARC");
        check(DXFDocument::parse(added.drawingBytes()).entities.size()==3);
        rejects([&]{CornerEditPlan(source,live,{0,0},{f.first,f.second},f.arc,"ARC");});
        rejects([&]{CornerEditPlan(source,live,{0,2},{f.first,f.second},f.arc,"ARC");});
        rejects([&]{CornerEditPlan(source,live,{0,1},{f.first},f.arc,"ARC");});
        rejects([&]{CornerEditPlan(source,live,{0,1},{f.first,f.second},f.arc,"LINE");});
        rejects([&]{CornerEditPlan(source,live,{0,1},{parseEntity(f.first)->copied({0,0,0}),f.second},f.arc,"ARC");});
        rejects([&]{CornerEditPlan(source,live,{0,1},{Drafting::properties(f.first,"Other",-1,true),f.second},f.arc,"ARC");});
        rejects([&]{CornerEditPlan(source,live,{0,1},{c.first,c.second},createLine({100,0,0},{101,0,0}),"LINE");});
        rejects([&]{CornerEditPlan(source,{{0,a},{0,b}},{0,1},{f.first,f.second},f.arc,"ARC");});
        check(source.serialize()==DXFDocument::parse(raw).serialize());
        std::cout<<"Corner identity, connector, full-drawing validation and opaque records passed\n";
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
