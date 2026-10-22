#include <DXFCAD/DXFDocument.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace DXFCAD;
namespace {
void check(bool ok) { if (!ok) throw std::runtime_error("Move check failed"); }
void shifted(Vector3D a,Vector3D b,Vector3D d) {
    check(std::abs(a.x+d.x-b.x)<1e-10 && std::abs(a.y+d.y-b.y)<1e-10 && std::abs(a.z+d.z-b.z)<1e-10);
}
template<typename F> void rejects(F f) {
    try { f(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid Move accepted");
}
}
int main() {
    try {
        const Vector3D delta{3,-5,7};
        const std::string extra = "102\n{APP\n10\n999\n20\n888\n102\n}\n1001\nTestApp\n1010\n123\n1040\n45\n";
        const std::vector<std::string> records{
            "0\nLINE\n5\nA1\n330\nF1\n8\n0\n10\n1\n20\n2\n11\n3\n21\n4\n210\n1\n220\n2\n230\n3\n",
            "0\nCIRCLE\n5\nA2\n10\n1\n20\n2\n30\n3\n40\n4\n210\n1\n220\n2\n230\n3\n",
            "0\nARC\n5\nA3\n10\n1\n20\n2\n40\n4\n50\n15\n51\n200\n210\n1\n220\n2\n230\n3\n",
            "0\nLWPOLYLINE\n5\nA4\n90\n2\n70\n129\n38\n2\n10\n1\n20\n2\n40\n0.2\n41\n0.4\n42\n0.3\n91\n7\n10\n3\n20\n4\n91\n8\n210\n1\n220\n2\n230\n3\n",
            "0\nTEXT\n5\nA5\n10\n1\n20\n2\n11\n3\n21\n4\n72\n1\n73\n2\n40\n2\n1\nText\n50\n15\n210\n1\n220\n2\n230\n3\n",
            "0\nMTEXT\n5\nA6\n10\n1\n20\n2\n30\n3\n11\n1\n21\n0\n31\n0\n40\n2\n41\n10\n1\nText\\PSecond line\n"
        };
        for (const auto& raw : records) {
            const auto entity = parseEntity(raw+extra);
            const auto moved = parseEntity(entity->translated(delta));
            check(moved->handle()==entity->handle() && moved->type()==entity->type());
            check(moved->record().value(330)==entity->record().value(330));
            check(moved->raw().find(extra)!=std::string::npos);
            const auto before = entity->snapPoints(), after = moved->snapPoints();
            check(before.size()==after.size());
            for (std::size_t i=0; i<before.size(); ++i) shifted(before[i].position,after[i].position,delta);
            const auto a=entity->geometry(), b=moved->geometry();
            if (!a.circles.empty()) check(a.circles[0].radius==b.circles[0].radius);
            if (a.text) {
                shifted(a.text->origin,b.text->origin,delta);
                shifted(a.text->xAxis,b.text->xAxis,{0,0,0});
                check(a.text->text==b.text->text);
            }
            if (entity->type()=="LWPOLYLINE") {
                const auto p=readLWPolyline(moved->raw());
                check(p.vertices()[0].bulge==0.3 && p.vertices()[0].startWidth==0.2);
                check(moved->raw().find("91\n7\n")!=std::string::npos);
            }
            if (entity->type()=="TEXT") {
                // Both insertion and alignment points move, in OCS.
                const auto back=parseEntity(moved->translated({-3,5,-7}));
                check(std::abs(back->record().number(10)-1)<1e-10 && std::abs(back->record().number(11)-3)<1e-10);
            }
            if (entity->type()=="MTEXT") check(moved->record().number(11)==1 && moved->record().number(21)==0);
            rejects([&] { entity->translated({NAN,0,0}); });
        }
        const auto original=parseEntity(records[0]+"102\n{ACAD_REACTORS\n330\nAB\n102\n}\n102\n{ACAD_XDICTIONARY\n360\nAC\n102\n}\n"+extra);
        const auto clone=parseEntity(original->copied(delta));
        check(clone->handle().empty() && clone->record().value(330).empty());
        check(clone->raw().find("ACAD_REACTORS")==std::string::npos && clone->raw().find("ACAD_XDICTIONARY")==std::string::npos);
        check(clone->raw().find(extra)!=std::string::npos);
        for (const auto& record : records) {
            const auto copy=parseEntity(parseEntity(record)->copied(delta));
            const auto output=DXFDocument::parse(DXFDocument::empty(4).serialize({}, {}, {copy->raw(),copy->raw()}));
            check(output.entities.size()==2 && output.entities[0]->handle()!=output.entities[1]->handle());
        }
        const auto offset=displacement({1,2,3},{4,-3,10});
        check(offset.x==3 && offset.y==-5 && offset.z==7);
        rejects([] { displacement({0,0,0},{0,0,0}); });
        rejects([] { displacement({-1e308,0,0},{1e308,0,0}); });
        rejects([] { parseEntity(createLine({1e308,0,0},{0,1,0}))->translated({1e308,0,0}); });
        rejects([] { parseEntity("0\nDIMENSION\n70\n0\n")->translated({1,2,3}); });
        const auto doc=DXFDocument::parse("0\nSECTION\n2\nENTITIES\n"+records[1]+extra+"0\nENDSEC\n0\nEOF\n");
        const auto exported=DXFDocument::parse(doc.serialize({{0,doc.entities[0]->translated(delta)}}));
        shifted(doc.entities[0]->geometry().circles[0].center,exported.entities[0]->geometry().circles[0].center,delta);
        std::cout << "Native entity Move, OCS/WCS, metadata preservation and validation passed.\n";
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
