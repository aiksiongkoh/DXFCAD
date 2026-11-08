#include <DXFCAD/DXFLeader.h>
#include <DXFCAD/Drawing.h>
#include <iostream>
#include <cmath>
#include <stdexcept>

namespace {
void check(bool condition) { if (!condition) throw std::runtime_error("LEADER check failed"); }
template<class F> void rejects(F f) {
    try { f(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid LEADER accepted");
}
const std::string raw="0\nLEADER\n5\nAB\n3\nSynthetic\n76\n3\n10\n0\n20\n0\n10\n3\n20\n4\n10\n6\n20\n4\n";
const std::string style="0\nDIMSTYLE\n2\nSynthetic\n40\n2\n41\n2.5\n";
}
int main() {
    try {
        const auto e=DXFCAD::parseEntity(raw,style);
        check(dynamic_cast<const DXFCAD::DXFLeader*>(e.get())!=nullptr);
        const auto g=e->geometry();
        check(g.segments.size()==2 && g.faces.size()==1);
        const auto a=g.faces[0][0], b=g.faces[0][1], c=g.faces[0][2];
        check(a.x==0 && a.y==0 && g.segments[1].end.x==6);
        const double area=std::abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))/2;
        check(std::abs(area-25.0/6)<1e-10);
        const auto source="0\nSECTION\n2\nTABLES\n"+style+"0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"+raw+"0\nENDSEC\n0\nEOF\n";
        const auto drawing=DXFCAD::Drawing::parse(source);
        check(drawing.warnings.empty() && drawing.entities.size()==1 && drawing.serialize()==source);
        check(drawing.entities[0]->context()==style);
        check(DXFCAD::entityColor(*e,DXFCAD::DXFLayer{"0",5})==std::vector<double>({1,1,1}));
        check(drawing.resolveEntity(raw)->geometry().faces.size()==1);
        rejects([&]{e->translated({1,0,0});});
        rejects([&]{DXFCAD::parseEntity(raw)->geometry();});
        rejects([&]{DXFCAD::parseEntity(raw+"72\n1\n",style)->geometry();});
        rejects([&]{DXFCAD::parseEntity(raw+"73\n0\n",style)->geometry();});
        rejects([&]{DXFCAD::parseEntity(raw,"0\nDIMSTYLE\n2\nWrong\n41\n2.5\n")->geometry();});
        rejects([&]{DXFCAD::parseEntity("0\nLEADER\n71\n0\n76\n3\n10\n0\n20\n0\n10\n1\n20\n0\n")->geometry();});
        const auto noArrow=DXFCAD::parseEntity(raw+"71\n0\n")->geometry();
        check(noArrow.faces.empty() && noArrow.segments.size()==2);
        std::cout<<"LEADER vertices, style-derived arrow area, persistence and failure contracts passed.\n";
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
