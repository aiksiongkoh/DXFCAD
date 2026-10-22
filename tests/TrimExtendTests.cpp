#include <DXFCAD/Drafting.h>
#include <DXFCAD/OCSFrame.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace DXFCAD;
namespace {
void check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
void near(double a,double b) { check(std::abs(a-b)<1e-7,"Unexpected trim coordinate or attribute"); }
template<typename F> void rejects(F f) {
    try { f(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid endpoint edit accepted");
}
std::string arc(double a,double b) {
    return patchEntityFields(readDXFGroups("0\nARC\n5\nAB\n330\nCD\n8\nCurves\n10\n0\n20\n0\n40\n5\n50\n0\n51\n90\n1001\nApp\n1000\nkeep\n"),{{50,a},{51,b}});
}
}
int main() {
    try {
        const auto line=createLine({-10,0,0},{10,0,0});
        const auto circle=createCircle({0,0,0},5);
        near(parseEntity(Drafting::trim(line,circle,true))->geometry().segments[0].start.x,-5);
        near(parseEntity(Drafting::trim(line,circle,false))->geometry().segments[0].end.x,5);
        const auto shortLine=createLine({-1,0,0},{1,0,0});
        near(parseEntity(Drafting::trim(shortLine,circle,true,true))->geometry().segments[0].start.x,-5);
        near(parseEntity(Drafting::trim(shortLine,circle,false,true))->geometry().segments[0].end.x,5);
        // The circle support is intersected analytically, but an ARC cutter is finite.
        const auto upper=arc(0,180), lower=arc(180,360);
        const auto horizontal=createLine({-10,3,0},{10,3,0});
        near(parseEntity(Drafting::trim(horizontal,upper,false))->geometry().segments[0].end.x,4);
        rejects([&] { Drafting::trim(horizontal,lower,false); });
        const auto vertical=createLine({0,-10,0},{0,10,0});
        const auto broad=arc(0,180);
        const auto arcStart=parseEntity(Drafting::trim(broad,vertical,true));
        const auto arcEnd=parseEntity(Drafting::trim(broad,vertical,false));
        near(arcStart->record().number(50),90); near(arcEnd->record().number(51),90);
        check(arcStart->handle()=="AB" && arcStart->layer()=="Curves" && arcStart->record().value(330)=="CD","Arc identity lost");
        check(arcStart->raw().find("1000\nkeep\n")!=std::string::npos,"Arc XDATA lost");
        const auto quarter=arc(0,90);
        const auto left=createLine({-5,-10,0},{-5,10,0});
        near(parseEntity(Drafting::trim(quarter,left,false,true))->record().number(51),180);
        const auto down=createLine({-10,-5,0},{10,-5,0});
        near(parseEntity(Drafting::trim(quarter,down,true,true))->record().number(50),270);
        rejects([&] { Drafting::trim(quarter,createLine({5,-1,0},{5,1,0}),false,true); });
        // Circle/circle intersections and wrap-around ARC sweeps.
        const auto shifted=createCircle({5,0,0},5);
        near(parseEntity(Drafting::trim(broad,shifted,true))->record().number(50),60);
        near(parseEntity(Drafting::trim(arc(300,120),shifted,false))->record().number(51),60);
        rejects([&] { Drafting::trim(broad,circle,false); });
        const auto boundary=createLWPolyline(LWPolyline({{-3,-2},{-3,2},{4,2},{4,-2}}));
        near(parseEntity(Drafting::trim(line,boundary,true))->geometry().segments[0].start.x,-3);
        near(parseEntity(Drafting::trim(line,boundary,false))->geometry().segments[0].end.x,4);
        const auto poly=createLWPolyline(LWPolyline({{0,0},{10,0},{10,10},{20,10}}));
        const auto cutter=createLine({-1,5,0},{21,5,0});
        const auto ps=readLWPolyline(Drafting::trim(poly,cutter,true));
        const auto pe=readLWPolyline(Drafting::trim(poly,cutter,false));
        check(ps.vertices().size()==3 && pe.vertices().size()==3,"Polyline segments not removed");
        near(ps.vertices()[0].x,10); near(ps.vertices()[0].y,5);
        near(pe.vertices().back().x,10); near(pe.vertices().back().y,5);
        // A cut exactly at an existing vertex must not create duplicate vertices.
        const auto vertexCut=createLine({5,-5,0},{15,5,0});
        check(readLWPolyline(Drafting::trim(poly,vertexCut,true)).vertices().size()==3,"Start vertex cut duplicated vertex");
        check(readLWPolyline(Drafting::trim(poly,vertexCut,false)).vertices().size()==2,"End vertex cut duplicated vertex");
        const auto polyExtend=readLWPolyline(Drafting::trim(poly,createLine({25,5,0},{25,15,0}),false,true));
        near(polyExtend.vertices().back().x,25);
        const auto polyStart=readLWPolyline(Drafting::trim(poly,createLine({-5,-5,0},{-5,5,0}),true,true));
        near(polyStart.vertices().front().x,-5);
        // Signed bulges, variable widths, vertex IDs and opaque data survive shortening.
        const auto wide=std::string("0\nLWPOLYLINE\n5\nDE\n90\n2\n70\n0\n10\n-5\n20\n0\n42\n1\n40\n2\n41\n6\n91\n101\n10\n5\n20\n0\n91\n102\n1001\nApp\n1000\nkeep\n");
        const auto trimmed=Drafting::trim(wide,vertical,true);
        const auto curved=readLWPolyline(trimmed);
        near(curved.vertices()[0].x,0); near(curved.vertices()[0].y,-5);
        near(curved.vertices()[0].bulge,std::tan(std::acos(-1.0)/8));
        near(curved.vertices()[0].startWidth,4); near(curved.vertices()[0].endWidth,6);
        check(trimmed.find("91\n101\n")!=std::string::npos && trimmed.find("91\n102\n")!=std::string::npos && trimmed.find("1000\nkeep\n")!=std::string::npos,"Polyline native fields lost");
        const auto cw=createLWPolyline(LWPolyline({{-5,0,-1},{5,0}}));
        near(readLWPolyline(Drafting::trim(cw,vertical,false)).vertices()[0].bulge,-std::tan(std::acos(-1.0)/8));
        const auto bulgeCut=createLWPolyline(LWPolyline({{-5,0,1},{5,0}}));
        near(parseEntity(Drafting::trim(createLine({-10,-3,0},{10,-3,0}),bulgeCut,false))->geometry().segments[0].end.x,4);
        // Major arcs remain major when appropriate; extensions retain their center.
        const auto extendedCurve=readLWPolyline(Drafting::trim(wide,createLine({-10,3,0},{10,3,0}),false,true));
        check(extendedCurve.vertices()[0].bulge>1,"Arc extension lost major sweep");
        near(extendedCurve.segments()[0].arc->center.x,0); near(extendedCurve.segments()[0].arc->radius,5);
        // Native OCS, including opposite normals and arbitrary tilted planes.
        const OCSFrame frame({1,2,3});
        const auto tilted=createLWPolyline(LWPolyline({{0,0},{10,0},{10,10}},false,2,7,{1,2,3}));
        const auto tiltCut=createLine(frame.toWorld({5,-5},7),frame.toWorld({5,5},7));
        const auto tp=readLWPolyline(Drafting::trim(tilted,tiltCut,true));
        near(tp.vertices()[0].x,5); near(tp.elevation(),7); near(tp.extrusion().x,1);
        const auto tiltLine=createLine(frame.toWorld({0,0},7),frame.toWorld({10,0},7));
        const auto tiltResult=parseEntity(Drafting::trim(tiltLine,tiltCut,false))->geometry().segments[0].end;
        near(frame.fromWorld(tiltResult).x,5); near(frame.fromWorld(tiltResult).z,7);
        const auto negative=patchEntityFields(readDXFGroups(broad),{{230,-1}});
        near(parseEntity(Drafting::trim(negative,vertical,true))->record().number(50),90);
        rejects([&] { Drafting::trim(line,createCircle({0,0,1},5),false); });
        rejects([&] { Drafting::trim(line,createLine({0,1,-1},{0,1,1}),false); });
        rejects([&] { Drafting::trim(circle,line,false); });
        rejects([&] { Drafting::trim(createRectangle({0,0,0},{5,5,0}),line,false); });
        const auto source=DXFDocument::parse(Drafting::setLayer(DXFDocument::empty().serialize(),"Curves",7,true));
        const auto serialized=DXFDocument::parse(source.serialize({}, {}, {parseEntity(trimmed)->copied({0,0,0}),arcStart->copied({0,0,0})}));
        check(serialized.entities.size()==2,"Edited entities failed export round trip");
        near(readLWPolyline(serialized.entities[0]->raw()).vertices()[0].startWidth,4);
        std::cout<<"Analytic planar LINE/ARC/polyline trim/extend, native attributes and rejection cases passed.\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
