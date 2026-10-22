#include <DXFCAD/Contour.h>
#include <DXFCAD/Drafting.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace DXFCAD;
namespace {
void check(bool yes) { if (!yes) throw std::runtime_error("Contour check failed"); }
void near(double a,double b) { check(std::abs(a-b)<1e-7); }
template<class F> void rejects(F f) {
    try { f(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid contour operation accepted");
}
std::string drawing(const std::string& entities) { return "0\nSECTION\n2\nENTITIES\n"+entities+"0\nENDSEC\n0\nEOF\n"; }
void editPlanTests() {
    const std::string line="0\nLINE\n5\nAB\n10\n0\n20\n0\n11\n10\n21\n0\n1001\nTEST\n1000\nretained\n";
    const std::string circle="0\nCIRCLE\n5\nBC\n10\n20\n20\n0\n40\n5\n";
    const auto original=drawing(line+circle+"0\nSPLINE\n999\nopaque\n");
    const auto source=DXFDocument::parse(original);
    const std::vector<std::pair<int,std::string>> live{{0,line},{1,circle}};
    const auto split=Contour::split(line,{3,7});
    const ContourEditPlan plan(source,live,{0},split);
    check(plan.preservesFirst() && plan.records().size()==3);
    check(parseEntity(plan.records()[0])->handle()=="AB");
    check(parseEntity(plan.records()[1])->handle().empty());
    check(DXFDocument::parse(plan.drawingBytes()).entities.size()==4);
    check(plan.drawingBytes().find("opaque")!=std::string::npos);
    const ContourEditPlan replaced(source,live,{1},Contour::breakBetween(circle,1,4));
    check(!replaced.preservesFirst() && parseEntity(replaced.records()[0])->handle().empty());
    check(DXFDocument::parse(replaced.drawingBytes()).entities.back()->type()=="ARC");
    const ContourEditPlan forceNew(source,live,{0},{line},false);
    check(!forceNew.preservesFirst() && parseEntity(forceNew.records()[0])->handle().empty());
    const auto empty=DXFDocument::empty(4);
    const auto a=createLine({0,0,0},{5,0,0}), b=createLine({5,0,0},{5,5,0});
    const ContourEditPlan joined(empty,{{-1,a},{-1,b}},{0,1},{Contour::join({a,b})},false);
    check(!joined.preservesFirst() && DXFDocument::parse(joined.drawingBytes()).entities.size()==1);
    rejects([&]{ContourEditPlan(source,live,{},split);});
    rejects([&]{ContourEditPlan(source,live,{0},{});});
    rejects([&]{ContourEditPlan(source,live,{0,0},split);});
    rejects([&]{ContourEditPlan(source,live,{2},split);});
    rejects([&]{ContourEditPlan(source,{{0,line},{0,line}},{0},split);});
    rejects([&]{ContourEditPlan(source,live,{0},{"0\nCIRCLE\n40\n-1\n"});});
    check(source.serialize()==DXFDocument::parse(original).serialize());
}
void splitTests() {
    const std::string raw="0\nLINE\n5\nAB\n330\nF\n10\n0\n20\n0\n30\n2\n11\n10\n21\n0\n31\n2\n1001\nAPP\n1000\nretain\n";
    auto pieces=Contour::split(raw,{3,7,3});
    check(pieces.size()==3);
    near(Contour::length(pieces[0]),3); near(Contour::length(pieces[1]),4);
    check(parseEntity(pieces[0])->handle()=="AB" && parseEntity(pieces[1])->handle().empty());
    for (const auto& piece : pieces) check(piece.find("retain")!=std::string::npos);
    near(Contour::distanceAtPoint(raw,{6,0.1,2}),6);
    rejects([&]{Contour::distanceAtPoint(raw,{6,0,3});});
    pieces=Contour::breakBetween(raw,3,7); check(pieces.size()==2);
    near(Contour::length(pieces[0]),3); near(Contour::length(pieces[1]),3);
    rejects([&]{Contour::split(raw,{0,10});});
    rejects([&]{Contour::split(raw,{-1});});
    rejects([&]{Contour::breakBetween(raw,0,10);});
    const auto doc=DXFDocument::parse(drawing(raw));
    auto roundtrip=DXFDocument::parse(doc.serialize({{0,pieces[0]}},{},{pieces[1]}));
    check(roundtrip.entities.size()==2 && roundtrip.warnings.empty());
    check(roundtrip.entities[1]->handle()!="AB");
    const std::string curved="0\nLWPOLYLINE\n5\nBC\n90\n3\n70\n128\n38\n3\n210\n0\n220\n1\n230\n0\n10\n0\n20\n0\n42\n1\n40\n2\n41\n6\n91\n7\n10\n10\n20\n0\n91\n8\n10\n15\n20\n0\n91\n9\n1001\nAPP\n1000\nkeep\n";
    const double arc=5*std::acos(-1.0);
    pieces=Contour::split(curved,{arc/2});
    auto a=readLWPolyline(pieces[0]), b=readLWPolyline(pieces[1]);
    near(a.vertices()[0].bulge,std::tan(std::acos(-1.0)/8));
    near(a.vertices()[0].endWidth,4); near(b.vertices()[0].startWidth,4);
    check(a.elevation()==3 && a.extrusion().y==1 && !a.closed());
    check(pieces[0].find("91\n7\n")!=std::string::npos && pieces[1].find("91\n8\n")!=std::string::npos);
    check(nativeEntityFields(parseEntity(pieces[0])->record()).integer(70)==128);
    const auto square=createRectangle({0,0,0},{10,10,0});
    pieces=Contour::split(square,{5});
    check(pieces.size()==1 && !readLWPolyline(pieces[0]).closed()); near(Contour::length(pieces[0]),40);
    pieces=Contour::breakBetween(square,35,5); check(pieces.size()==1); near(Contour::length(pieces[0]),30);
    const auto circle=createCircle({0,0,0},2);
    pieces=Contour::split(circle,{0});
    check(pieces.size()==1 && parseEntity(pieces[0])->type()=="LWPOLYLINE"); near(Contour::length(pieces[0]),4*std::acos(-1.0));
    pieces=Contour::split(circle,{1,4}); check(pieces.size()==2);
    check(parseEntity(pieces[0])->type()=="ARC"); near(Contour::length(pieces[0]),3);
    near(Contour::length(pieces[0])+Contour::length(pieces[1]),4*std::acos(-1.0));
    rejects([&]{Contour::distanceAtPoint(circle,{0,0,0});});
    const auto widthOverride=createLWPolyline(LWPolyline({{0,0,0,0,0,false},{5,0,0,0,0,true},{10,0}},false,4));
    pieces=Contour::split(widthOverride,{3});
    check(readLWPolyline(pieces[0]).widthRegions().empty());
}
void closeJoinTests() {
    auto raw=createLWPolyline(LWPolyline({{0,0},{10,0},{10,10},{0,0}},false));
    auto closed=Contour::close(raw);
    check(readLWPolyline(closed).closed() && readLWPolyline(closed).vertices().size()==3);
    near(Contour::length(closed),Contour::length(raw));
    check(Contour::close(closed)==closed);
    raw=createLWPolyline(LWPolyline({{0,0},{10,0},{10,10,5}},false));
    closed=Contour::close(raw);
    near(readLWPolyline(closed).vertices().back().bulge,0);
    rejects([&]{Contour::close(createLine({0,0,0},{1,0,0}));});
    const auto a=createLine({0,0,2},{10,0,2});
    const auto b=createLine({10,10,2},{10,0,2});
    const auto c=createLine({10,10,2},{0,10,2});
    const auto d=createLine({0,10,2},{0,0,2});
    auto joined=Contour::join({a,c,b,d});
    check(readLWPolyline(joined).closed()); near(Contour::length(joined),40);
    check(parseEntity(joined)->handle().empty());
    rejects([&]{Contour::join({a,c});});
    rejects([&]{Contour::join({a,a});});
    rejects([&]{Contour::join({a,b,createLine({10,0,2},{20,0,2})});});
    rejects([&]{Contour::join({a,createLine({10,0,3},{10,10,3})});});
    rejects([&]{Contour::join({a,Drafting::properties(b,"Other",256,true)});});
    const auto tilted= createLWPolyline(LWPolyline({{0,0,1,2,6,true},{10,0}},false,0,3,{0,1,0}));
    const auto line=createLine({-10,3,0},{-15,3,0});
    joined=Contour::join({line,tilted});
    near(Contour::length(joined),5+5*std::acos(-1.0));
    const auto model=readLWPolyline(joined);
    bool found=false;
    for (const auto& v : model.vertices()) if (v.bulge!=0) {
        near(std::abs(v.bulge),1); near(v.startWidth,6); near(v.endWidth,2); found=true;
    }
    check(found);
    joined=Contour::join({createLine({0,0,0},{10,0,0}),createLine({0,0,10},{0,0,0})});
    near(Contour::length(joined),20);
    raw=createLWPolyline(LWPolyline({{0,0},{10,0},{10,10},{0,0,0,0,0,true}},false,4));
    check(readLWPolyline(Contour::close(raw)).widthRegions().empty());
}
void interiorTests() {
    const auto line=createLine({0,0,2},{10,0,2});
    const auto a=createLine({3,-2,2},{3,2,2}), b=createLine({7,-2,2},{7,2,2});
    auto pieces=Contour::trimInterior(line,{a,b},5);
    check(pieces.size()==2); near(Contour::length(pieces[0]),3); near(Contour::length(pieces[1]),3);
    pieces=Contour::trimInterior(line,{a,b},1);
    check(pieces.size()==1); near(Contour::length(pieces[0]),7);
    rejects([&]{Contour::trimInterior(line,{a,b},3);});
    rejects([&]{Contour::trimInterior(line,{createLine({2,0,2},{8,0,2})},5);});
    rejects([&]{Contour::trimInterior(line,{createCircle({5,1,2},1)},5);}); // tangent only
    const auto touching=createLWPolyline(LWPolyline({{3,0},{4,2},{2,2}},true,0,2));
    rejects([&]{Contour::trimInterior(line,{touching},5);}); // polygon vertex only touches
    const auto touchingOpen=createLWPolyline(LWPolyline({{2,2},{3,0},{4,2}},false,0,2));
    rejects([&]{Contour::trimInterior(line,{touchingOpen},5);});
    const auto circle=createCircle({0,0,2},5);
    pieces=Contour::trimInterior(circle,{createLine({0,-10,2},{0,10,2})},1);
    check(pieces.size()==1); near(Contour::length(pieces[0]),5*std::acos(-1.0));
    check(parseEntity(pieces[0])->type()=="ARC");
    const auto square=createRectangle({0,0,2},{10,10,2});
    pieces=Contour::trimInterior(square,{createLine({5,-2,2},{5,12,2})},0);
    check(pieces.size()==1); near(Contour::length(pieces[0]),20);
    check(!readLWPolyline(pieces[0]).closed());
    pieces=Contour::trimInterior(line,{a,a,b},5); check(pieces.size()==2); // duplicate crossings
    rejects([&]{Contour::trimInterior(line,{createLine({3,2,2},{3,4,2})},5);}); // finite boundary
    near(Contour::distanceAtRay(line,{4,0,10},{4,0,-10}),4);
    const auto vertical=createLine({0,0,0},{0,0,10});
    near(Contour::distanceAtRay(vertical,{10,0,4},{-10,0,4}),4);
    const auto skew=createLine({0,0,0},{10,0,10});
    const auto cut1=createLine({3,-1,3},{3,1,3}),cut2=createLine({7,-1,7},{7,1,7});
    pieces=Contour::trimInterior(skew,{cut1,cut2},5*std::sqrt(2.0));
    check(pieces.size()==2); near(Contour::length(pieces[0]),3*std::sqrt(2.0));
}
}
int main() {
    try { editPlanTests(); splitTests(); closeJoinTests(); interiorTests(); std::cout<<"Contour plans, split, break, close, join and interior trim checks passed.\n"; }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
