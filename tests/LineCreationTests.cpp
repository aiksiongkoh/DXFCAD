#include <DXFCAD/DXFDocument.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool ok) { if (!ok) throw std::runtime_error("Line creation check failed"); }
template<typename F> void rejects(F f) {
    try { f(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid line creation accepted");
}
}
int main() {
    try {
        const DXFCAD::Vector3D a{1.0/3,2,3}, b{4,6,3};
        const auto raw = DXFCAD::createLine(a,b);
        const auto entity = DXFCAD::parseEntity(raw);
        check(entity->type() == "LINE" && entity->handle().empty() && entity->layer() == "0");
        const auto segment = entity->geometry().segments.at(0);
        check(segment.start.x == a.x && segment.start.z == 3 && segment.end.y == 6);
        const auto drawing = DXFCAD::DXFDocument::empty(4);
        const auto output = drawing.serialize({}, {}, {raw,raw});
        const auto restored = DXFCAD::DXFDocument::parse(output);
        check(restored.entities.size() == 2 && restored.insertionUnits == 4);
        check(!restored.entities[0]->handle().empty() && restored.entities[0]->handle() != restored.entities[1]->handle());
        rejects([&] { DXFCAD::createLine(a,a); });
        rejects([&] { DXFCAD::createLine(a,b,"bad\nlayer"); });
        rejects([&] { DXFCAD::createLine({std::numeric_limits<double>::quiet_NaN(),0,0},b); });
        rejects([&] { drawing.serialize({}, {}, {"0\nLINE\n10\n0\n20\n0\n11\n0\n21\n0\n"}); });
        const auto picked = DXFCAD::pointOnXYPlane({1,2,10},{5,6,-10},2);
        check(std::abs(picked.x-2.6) < 1e-12 && std::abs(picked.y-3.6) < 1e-12 && picked.z == 2);
        check(DXFCAD::pointInXYPlane({4,5,2+1e-8},2).z == 2);
        rejects([] { DXFCAD::pointInXYPlane({4,5,3},2); });
        rejects([] { DXFCAD::pointOnXYPlane({0,0,1},{1,0,1}); });
        std::cout << "Native LINE creation, picking, validation, and handle allocation passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
