#include <DXFCAD/Drafting.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace DXFCAD;
namespace {
void check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
void near(double a,double b) { check(std::abs(a-b)<1e-7,"Unexpected offset coordinate or radius"); }
template<typename F> void rejects(F f) {
    try { f(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid curved offset accepted");
}
LWPolyline offset(const LWPolyline& source,double d) { return readLWPolyline(Drafting::offset(createLWPolyline(source),d)); }
void concentric(const LWPolylineArc& a,const LWPolylineArc& b,double radius) {
    near(a.center.x,b.center.x); near(a.center.y,b.center.y); near(b.radius,radius);
}
}
int main() {
    try {
        const LWPolyline semicircle({{-5,0,1},{5,0}},false,2,7,{1,2,3});
        const auto inside=offset(semicircle,1), outside=offset(semicircle,-2);
        near(inside.vertices()[0].x,-4); near(inside.vertices()[1].x,4);
        near(outside.vertices()[0].x,-7); near(outside.vertices()[1].x,7);
        check(inside.vertices()[0].bulge==1,"Unchanged sweep was not retained exactly");
        concentric(*semicircle.segments()[0].arc,*inside.segments()[0].arc,4);
        near(inside.constantWidth(),2); near(inside.elevation(),7); near(inside.extrusion().y,2);
        check(inside.worldSegments().size()==1,"Tilted offset failed to derive");
        const LWPolyline clockwise({{-5,0,-1},{5,0}});
        concentric(*clockwise.segments()[0].arc,*offset(clockwise,1).segments()[0].arc,6);
        check(offset(clockwise,1).vertices()[0].bulge==-1,"Clockwise sweep changed");
        const LWPolyline major({{-5,0,2},{5,0}});
        const auto majorOut=offset(major,-1);
        check(majorOut.vertices()[0].bulge==2,"Major arc became a minor arc");
        concentric(*major.segments()[0].arc,*majorOut.segments()[0].arc,major.segments()[0].arc->radius+1);
        // A common closed circle is encoded as two semicircles, not three vertices.
        const LWPolyline circle({{-5,0,1},{5,0,1}},true);
        const auto ring=offset(circle,1);
        check(ring.closed() && ring.vertices().size()==2,"Closed two-arc topology changed");
        near(ring.segments()[0].arc->radius,4); near(ring.segments()[1].arc->radius,4);
        near(offset(LWPolyline({{-5,0,-1},{5,0,-1}},true),1).segments()[0].arc->radius,6);
        // Mixed closed line/arc profile, including offsets that create major arcs.
        const LWPolyline dShape({{-5,0},{5,0,1}},true);
        const auto dIn=offset(dShape,1), dOut=offset(dShape,-1);
        near(dIn.vertices()[0].y,1); near(dIn.vertices()[1].x,std::sqrt(15.0));
        near(dIn.segments()[1].arc->radius,4);
        check(dOut.vertices()[1].bulge>1,"Outward join lost the major sweep");
        near(dOut.segments()[1].arc->radius,6);
        // Fillet-generated tangent LINE/ARC/LINE joins must remain tangent.
        const auto elbow=createLWPolyline(LWPolyline({{0,0},{10,0},{10,10}}));
        const auto fillet=readLWPolyline(Drafting::fillet(elbow,1,2));
        const auto inset=offset(fillet,1), outset=offset(fillet,-1);
        check(inset.vertices().size()==4 && outset.vertices().size()==4,"Tangent joins added vertices");
        near(inset.vertices()[1].x,8); near(inset.vertices()[1].y,1);
        near(inset.vertices()[2].x,9); near(inset.vertices()[2].y,2);
        near(inset.segments()[1].arc->radius,1); near(outset.segments()[1].arc->radius,3);
        const auto back=offset(outset,1);
        for (std::size_t i=0; i<fillet.vertices().size(); ++i) {
            near(back.vertices()[i].x,fillet.vertices()[i].x); near(back.vertices()[i].y,fillet.vertices()[i].y);
            near(back.vertices()[i].bulge,fillet.vertices()[i].bulge);
        }
        // Non-tangent LINE/ARC and ARC/LINE joins use the nearest support hit.
        const LWPolyline arcLine({{-5,0,1},{5,0},{10,0}});
        const auto joined=offset(arcLine,1);
        near(joined.vertices()[1].x,std::sqrt(15.0)); near(joined.vertices()[1].y,1);
        near(joined.segments()[0].arc->radius,4);
        const LWPolyline lineArc({{-10,0},{-5,0,1},{5,0}});
        const auto joinedStart=offset(lineArc,-1);
        near(joinedStart.vertices()[1].x,-std::sqrt(35.0)); near(joinedStart.vertices()[1].y,-1);
        near(joinedStart.segments()[1].arc->radius,6);
        const LWPolyline twoArcs({{-5,0,1},{5,0,std::tan(std::acos(-1.0)/8)},{10,5}});
        const auto arcJoin=offset(twoArcs,1);
        near(arcJoin.vertices()[1].x,(5+std::sqrt(7.0))/2);
        near(arcJoin.vertices()[1].y,(5-std::sqrt(7.0))/2);
        near(arcJoin.segments()[0].arc->radius,4); near(arcJoin.segments()[1].arc->radius,4);
        const auto arcBridge=offset(twoArcs,2.8);
        check(arcBridge.vertices().size()==4,"Disjoint ARC/ARC supports were not bridged");
        near(arcBridge.segments()[1].arc->radius,2.8);
        const auto sCurve=offset(LWPolyline({{-5,0,1},{5,0,-1},{15,0}}),1);
        near(sCurve.vertices()[1].x,4); near(sCurve.segments()[0].arc->radius,4); near(sCurve.segments()[1].arc->radius,6);
        // An outside gap between disjoint supports needs an exact tangent arc
        // around the original corner. Its radius is the offset distance.
        const auto bridge=offset(arcLine,2.8);
        check(bridge.vertices().size()==4,"Disjoint outer supports were not bridged");
        near(bridge.segments()[0].arc->radius,2.2); near(bridge.segments()[1].arc->radius,2.8);
        near(bridge.segments()[1].arc->center.x,5); near(bridge.segments()[1].arc->center.y,0);
        check(bridge.vertices()[1].bulge<0,"Outer connector direction wrong");
        const std::string rich="0\nLWPOLYLINE\n5\nAB\n330\nCD\n100\nAcDbEntity\n8\n0\n100\nAcDbPolyline\n90\n3\n70\n128\n38\n7\n43\n3\n39\n2\n210\n1\n220\n2\n230\n3\n10\n-5\n20\n0\n42\n1\n40\n2\n41\n4\n91\n101\n10\n5\n20\n0\n40\n6\n41\n8\n91\n102\n10\n10\n20\n0\n91\n103\n102\n{APP\n42\nopaque\n102\n}\n1001\nApp\n1000\nkeep\n";
        const auto native=Drafting::offset(rich,2.8);
        const auto wide=readLWPolyline(native);
        check(wide.vertices().size()==4 && wide.vertices()[1].hasWidths,"New connector lost variable widths");
        near(wide.vertices()[0].startWidth,2); near(wide.vertices()[0].endWidth,4);
        near(wide.vertices()[1].startWidth,4); near(wide.vertices()[1].endWidth,6);
        near(wide.vertices()[2].startWidth,6); near(wide.vertices()[2].endWidth,8);
        const auto entity=parseEntity(native);
        check(entity->handle()=="AB" && entity->record().value(330)=="CD" && entity->record().integer(70)==128,"Entity identity or flags changed");
        for (const auto& text : {"91\n101\n","91\n102\n","91\n103\n","102\n{APP\n42\nopaque\n102\n}","1000\nkeep\n"})
            check(native.find(text)!=std::string::npos,"Native vertex IDs or opaque data lost");
        // Invalid inputs/results fail atomically, rather than dropping geometry.
        rejects([&] { offset(semicircle,5); });
        rejects([&] { offset(circle,6); });
        rejects([&] { offset(fillet,2); });
        rejects([&] { offset(LWPolyline({{0,0},{0,0},{5,0}}),1); });
        rejects([&] { offset(LWPolyline({{0,0},{5,0}},true),1); });
        rejects([&] { offset(LWPolyline({{0,0},{5,5},{0,5},{5,0}},true),1); });
        rejects([&] { offset(LWPolyline({{-5,0,1},{5,0,-1},{-5,0}}),1); });
        const LWPolyline narrowNeck({{0,0,0.02},{20,0},{20,20},{12,20},{12,2},{8,2},{8,20},{0,20}},true);
        try {
            offset(narrowNeck,1.1);
            throw std::runtime_error("Self-crossing curved offset accepted");
        } catch (const std::invalid_argument& error) {
            check(std::string(error.what()).find("self-intersect")!=std::string::npos,"Expected rejection of an output self-intersection");
        }
        const auto roundTrip=DXFDocument::parse(DXFDocument::empty().serialize({}, {}, {entity->copied({0,0,0})}));
        const auto restored=readLWPolyline(roundTrip.entities[0]->raw());
        near(restored.vertices()[1].bulge,wide.vertices()[1].bulge); near(restored.vertices()[1].startWidth,4);
        check(restored.extrusion().z==3 && restored.elevation()==7,"Export lost native plane");
        std::cout<<"Curved polyline offset: analytic joins, round connectors, native data, planes and export passed.\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
