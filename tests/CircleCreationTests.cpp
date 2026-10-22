#include <DXFCAD/DXFDocument.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
void check(bool ok) { if (!ok) throw std::runtime_error("Circle creation check failed"); }
template<typename F> void rejects(F f) {
    try { f(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid circle accepted");
}
}
int main() {
    try {
        const DXFCAD::Vector3D center{1.0/3,2,3};
        check(DXFCAD::circleRadius(center,{center.x+3,6,3}) == 5);
        const auto raw = DXFCAD::createCircle(center,5);
        const auto entity = DXFCAD::parseEntity(raw);
        check(entity->type() == "CIRCLE" && entity->handle().empty() && entity->layer() == "0");
        const auto circle = entity->geometry().circles.at(0);
        check(circle.center.x == center.x && circle.center.z == 3 && circle.radius == 5);
        const auto drawing = DXFCAD::DXFDocument::empty(4);
        const auto restored = DXFCAD::DXFDocument::parse(drawing.serialize({}, {}, {raw,raw}));
        check(restored.entities.size() == 2 && restored.insertionUnits == 4);
        check(!restored.entities[0]->handle().empty() && restored.entities[0]->handle() != restored.entities[1]->handle());
        rejects([&] { DXFCAD::createCircle(center,0); });
        rejects([&] { DXFCAD::createCircle(center,-1); });
        rejects([&] { DXFCAD::createCircle(center,std::numeric_limits<double>::infinity()); });
        rejects([&] { DXFCAD::createCircle({NAN,0,0},1); });
        rejects([&] { DXFCAD::createCircle(center,1,"bad\nlayer"); });
        rejects([&] { DXFCAD::circleRadius(center,center); });
        rejects([&] { DXFCAD::circleRadius(center,{4,6,4}); });
        rejects([&] { DXFCAD::circleRadius({-1e308,0,0},{1e308,0,0}); });
        rejects([&] { drawing.serialize({}, {}, {"0\nCIRCLE\n10\n0\n20\n0\n40\n-1\n"}); });
        std::cout << "CIRCLE creation, radius validation, and export passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
