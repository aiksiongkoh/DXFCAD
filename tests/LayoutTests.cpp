#include <DXFCAD/Drawing.h>
#include <DXFCAD/Drafting.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool ok) { if (!ok) throw std::runtime_error("Layout check failed"); }
void near(double a,double b) { check(std::abs(a-b)<1e-10); }
double area(const std::vector<DXFCAD::Vector3D>& p) {
    double sum=0;
    for (std::size_t i=0;i<p.size();++i) sum+=p[i].x*p[(i+1)%p.size()].y-p[(i+1)%p.size()].x*p[i].y;
    return sum/2;
}
}
int main() {
    using namespace DXFCAD;
    try {
        PaperClip clip({0,0,10,5});
        for (const auto& pair : std::vector<std::vector<Vector3D>>{
                {{-2,2,0},{12,2,0}},{{5,-2,0},{5,7,0}},{{-2,-2,0},{12,12,0}}}) {
            const auto lines=clip.polyline(pair);
            check(lines.size()==1);
            for (const auto& p : lines[0]) check(PaperRectangle{0,0,10,5}.contains(p));
        }
        const auto horizontal=clip.polyline({{-2,2,0},{12,2,0}});
        near(horizontal[0][0].x,0); near(horizontal[0][1].x,10);
        check(clip.polyline({{-1,6,0},{12,6,0}}).empty());
        check(clip.polyline({}).empty());
        const auto whole=clip.polygon({{-2,-2,0},{12,-2,0},{12,7,0},{-2,7,0}});
        near(area(whole),50);
        const auto triangle=clip.polygon({{0,0,0},{10,0,0},{0,10,0}});
        near(area(triangle),37.5); // 50 minus the upper triangle of area 12.5.
        near(area(clip.polygon({{-5,-5,0},{15,5,0},{-5,15,0}})),43.75);
        const auto concave=clip.polygon({{-1,1,0},{3,1,0},{3,2,0},{1,2,0},{1,4,0},{-1,4,0}});
        near(area(concave),5);
        bool invalid=false;
        try { PaperClip bad({0,0,0,1}); } catch (const std::invalid_argument&) { invalid=true; }
        check(invalid);
        invalid=false;
        try { clip.polyline({{0,0,0},{std::numeric_limits<double>::infinity(),1,0}}); }
        catch (const std::invalid_argument&) { invalid=true; }
        check(invalid);

        const std::string viewport="0\nVIEWPORT\n5\nAB\n330\nBB\n67\n1\n69\n2\n68\n2\n10\n100\n20\n200\n40\n10\n41\n5\n45\n50\n12\n2\n22\n3\n17\n10\n27\n20\n37\n30\n51\n90\n";
        const std::string paper="0\nLINE\n5\nAC\n330\nBB\n67\n1\n10\n1\n20\n2\n11\n4\n21\n6\n1001\nAPP\n1000\nretain\n";
        const std::string metadata="0\nSECTION\n2\nTABLES\n0\nBLOCK_RECORD\n5\nBB\n340\nCC\n0\nENDSEC\n0\nSECTION\n2\nOBJECTS\n0\nLAYOUT\n5\nCC\n100\nAcDbPlotSettings\n1\nWrong name\n70\n0\n44\n297\n45\n210\n100\nAcDbLayout\n1\nSheet 1\n71\n2\n10\n0\n20\n0\n11\n297\n21\n210\n0\nLAYOUT\n5\nCD\n100\nAcDbLayout\n1\nSheet 2\n71\n1\n0\nENDSEC\n";
        const auto raw=metadata+"0\nSECTION\n2\nENTITIES\n"+viewport+paper+"0\nENDSEC\n0\nEOF\n";
        const auto drawing=Drawing::parse(raw);
        check(drawing.entities.empty() && drawing.layouts.size()==2 && drawing.warnings.empty());
        check(drawing.layouts[0].name()=="Sheet 2" && drawing.layouts[1].name()=="Sheet 1");
        const auto& layout=drawing.layouts[1];
        check(layout.entities().size()==1 && layout.entities()[0]->raw()==paper);
        check(layout.viewports().size()==1 && layout.viewports()[0].raw()==viewport);
        near(layout.limits()->maxX,297); near(layout.limits()->maxY,210);
        const auto& vp=layout.viewports()[0];
        const auto center=vp.modelToPaper({13,18,30}); near(center.x,100); near(center.y,200);
        const auto right=vp.modelToPaper({23,18,99}); near(right.x,100); near(right.y,201); near(right.z,0);
        const auto down=vp.modelToPaper({13,28,30}); near(down.x,99); near(down.y,200);
        near(vp.paperBounds().minX,95); near(vp.paperBounds().maxY,202.5);
        check(drawing.serialize()==raw);
        check(drawing.serializeCurrent({})==raw); // Paper entities are retained, never model edit slots.
        auto inferredViewport=viewport;
        inferredViewport.erase(inferredViewport.find("67\n1\n"),5);
        inferredViewport.replace(inferredViewport.find("330\nBB\n"),7,"330\nbb\n");
        const auto ownerOnly=Drawing::parse(metadata+"0\nSECTION\n2\nENTITIES\n"+inferredViewport+"0\nENDSEC\n0\nEOF\n");
        check(ownerOnly.viewports.size()==1 && ownerOnly.viewports[0].raw()==inferredViewport);
        check(ownerOnly.layouts[1].viewports().size()==1);
        auto old="0\nSECTION\n2\nENTITIES\n"+paper+"0\nENDSEC\n0\nEOF\n";
        const auto inferred=Drawing::parse(old);
        check(inferred.layouts.size()==1 && inferred.layouts[0].raw().empty());
        check(inferred.layouts[0].entities().size()==1 && inferred.serialize()==old);
        std::cout<<"Layout ownership, preservation, analytic projection and clipping passed.\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
