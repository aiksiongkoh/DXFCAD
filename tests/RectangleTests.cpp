#include <DXFCAD/DXFDocument.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace DXFCAD;
void check(bool ok) { if (!ok) throw std::runtime_error("Rectangle check failed"); }
template<typename F> void rejects(F f) { try { f(); } catch (const std::exception&) { return; } throw std::runtime_error("Invalid rectangle accepted"); }
int main() {
    try {
        const auto raw=createRectangle({1,2,3},{5,8,3});
        const auto p=readLWPolyline(raw);
        check(p.closed() && p.vertices().size()==4 && p.elevation()==3);
        double length=0;
        for (const auto& edge : p.worldSegments()) { check(!edge.arc); length+=std::hypot(edge.end.x-edge.start.x,edge.end.y-edge.start.y); }
        check(length==20);
        const auto reversed=readLWPolyline(createRectangle({5,8,3},{1,2,3}));
        check(reversed.closed() && reversed.vertices()[0].x==5);
        const auto out=DXFDocument::parse(DXFDocument::empty(4).serialize({}, {}, {raw,raw}));
        check(out.entities.size()==2 && out.entities[0]->handle()!=out.entities[1]->handle());
        rejects([] { createRectangle({1,2,0},{1,4,0}); });
        rejects([] { createRectangle({1,2,0},{4,2,0}); });
        rejects([] { createRectangle({1,2,0},{4,5,1}); });
        rejects([] { createRectangle({NAN,0,0},{4,5,0}); });
        rejects([] { createRectangle({0,0,0},{4,5,0},"bad\nlayer"); });
        std::cout << "Native closed rectangle creation and validation passed.\n";
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
