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
        const Vector3D delta{0,0,0};
        const Vector3D pivot{2,3,4};
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
            const auto moved = parseEntity(entity->scaled(2,pivot));
            check(moved->handle()==entity->handle() && moved->type()==entity->type());
            check(moved->record().value(330)==entity->record().value(330));
            check(moved->raw().find("102\n{APP\n10\n999\n20\n888\n102\n}\n")!=std::string::npos);
            check(moved->raw().find("1001\nTestApp\n1010\n123\n1040\n45\n")!=std::string::npos);
            const auto before = entity->snapPoints(), after = moved->snapPoints();
            check(before.size()==after.size());
            for (std::size_t i=0; i<before.size(); ++i) shifted({pivot.x+2*(before[i].position.x-pivot.x),pivot.y+2*(before[i].position.y-pivot.y),pivot.z+2*(before[i].position.z-pivot.z)},after[i].position,{0,0,0});
            const auto a=entity->geometry(), b=moved->geometry();
            if (!a.circles.empty()) check(2*a.circles[0].radius==b.circles[0].radius);
            if (a.text) {
                shifted({pivot.x+2*(a.text->origin.x-pivot.x),pivot.y+2*(a.text->origin.y-pivot.y),pivot.z+2*(a.text->origin.z-pivot.z)},b.text->origin,{0,0,0});
                shifted(a.text->xAxis,b.text->xAxis,{0,0,0});
                check(a.text->text==b.text->text && b.text->height==2*a.text->height);
            }
            if (entity->type()=="LWPOLYLINE") {
                const auto p=readLWPolyline(moved->raw());
                check(p.vertices()[0].bulge==0.3 && p.vertices()[0].startWidth==0.4);
                check(moved->raw().find("91\n7\n")!=std::string::npos);
            }
            const auto back=parseEntity(moved->scaled(0.5,pivot));
            const auto restored=back->snapPoints();
            for (std::size_t j=0; j<before.size(); ++j) shifted(before[j].position,restored[j].position,{0,0,0});
            rejects([&] { entity->scaled(NAN,pivot); });
        }
        check(scaleFactor({0,0,0},{0,6,8},5)==2);
        rejects([] { scaleFactor({0,0,0},{0,6,8},0); });
        rejects([] { parseEntity(createCircle({0,0,0},1))->scaled(-1,{0,0,0}); });
        std::cout << "Native rotation, tilted OCS, text directions and metadata passed.\n";
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
