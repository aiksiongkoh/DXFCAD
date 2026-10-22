#include <DXFCAD/DXFDocument.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool yes) { if (!yes) throw std::runtime_error("DXF document check failed"); }
template<typename F> void rejects(F f) {
    try { f(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Malformed DXF accepted");
}
std::string document(const std::string& entities) {
    return "0\nSECTION\n2\nENTITIES\n"+entities+"0\nENDSEC\n0\nEOF\n";
}
}
int main()
{
    try {
        const std::string lw = "0\nLWPOLYLINE\n5\nAB\n8\nExample\n90\n2\n70\n0\n38\n3\n43\n4\n10\n0\n20\n0\n40\n1\n41\n2\n42\n1\n10\n2\n20\n0\n1001\nExampleXdata\n1000\nuntouched\n";
        const auto poly = DXFCAD::parseEntity(lw);
        check(poly->raw() == lw && poly->layer() == "Example" && poly->handle() == "AB");
        const auto pg = poly->geometry().segments.at(0);
        check(pg.arc && pg.arc->radius == 1 && pg.start.z == 3);
        const auto line = DXFCAD::parseEntity("0\nLINE\n10\n1\n20\n2\n30\n3\n11\n4\n21\n5\n31\n6\n210\n1\n230\n0\n");
        check(line->geometry().segments[0].start.x == 1 && line->geometry().segments[0].end.z == 6);
        const auto circle = DXFCAD::parseEntity("0\nCIRCLE\n10\n1\n20\n2\n30\n3\n40\n4\n230\n-1\n")->geometry().circles.at(0);
        check(circle.center.x == -1 && circle.center.y == 2 && circle.center.z == -3 && circle.radius == 4);
        const auto arc = DXFCAD::parseEntity("0\nARC\n10\n0\n20\n0\n40\n2\n50\n270\n51\n90\n")->geometry().segments[0];
        check(std::abs(arc.arc->sweep-std::acos(-1.0)) < 1e-12 && std::abs(arc.arc->midpoint.x-2) < 1e-12);
        const auto text = DXFCAD::parseEntity("0\nTEXT\n10\n1\n20\n2\n40\n0.2\n1\n hello \n50\n90\n")->geometry().text.value();
        check(text.text == " hello " && std::abs(text.xAxis.y-1) < 1e-12);
        const auto centered = DXFCAD::parseEntity("0\nTEXT\n10\n99\n20\n99\n11\n4\n21\n5\n40\n2\n41\n2\n72\n1\n73\n2\n1\nCenter\n102\n{opaque\n72\n5\n102\n}\n1001\nAPP\n1070\n7\n");
        const auto tg = centered->geometry().text.value();
        const auto anchor = tg.alignedOrigin(1, -1, 5, 3);
        check(tg.origin.x == 4 && tg.origin.y == 5 && anchor.x == -2 && anchor.y == 4);
        check(centered->snapPoints().at(0).position.x == 4);
        check(DXFCAD::DXFDocument::parse(document(centered->raw())).warnings.empty());
        rejects([&] { tg.alignedOrigin(5, 0, 1, 2); });
        for (int horizontal : {-1, 3, 4, 5, 6}) {
            rejects([&] { DXFCAD::parseEntity("0\nTEXT\n11\n0\n21\n0\n40\n1\n72\n"+std::to_string(horizontal)+"\n")->geometry(); });
        }
        const auto doc = DXFCAD::DXFDocument::parse(document(lw+line->raw()+"0\nSPLINE\n5\nZZ\n"));
        check(doc.entities.size() == 2 && doc.warnings.size() == 1);
        const auto edited=line->translated({2,0,0});
        const auto assembled=doc.serializeCurrent({{1,edited},{-1,line->copied({0,5,0})}});
        const auto current=DXFCAD::DXFDocument::parse(assembled);
        check(current.entities.size()==2 && current.entities[0]->raw()==edited);
        check(assembled.find("0\nSPLINE\n5\nZZ\n")!=std::string::npos);
        check(DXFCAD::DXFDocument::parse(doc.serializeCurrent({})).entities.empty());
        rejects([&] { doc.serializeCurrent({{0,lw},{0,lw}}); });
        rejects([&] { doc.serializeCurrent({{-2,lw}}); });
        rejects([&] { doc.serializeCurrent({{2,lw}}); });
        rejects([] { DXFCAD::DXFDocument::parse("AutoCAD Binary DXF"); });
        rejects([] { DXFCAD::DXFDocument::parse("0\nSECTION\n2\nENTITIES\n"); });
        rejects([] { DXFCAD::DXFDocument::parse("0\nSECTION\n2"); });
        rejects([] { DXFCAD::parseEntity("0\nLWPOLYLINE\n90\n2\n10\n0\n20\n0\n")->geometry(); });
        rejects([] { DXFCAD::parseEntity("0\nCIRCLE\n10\n0\n20\n0\n40\n-1\n")->geometry(); });
        rejects([] { DXFCAD::parseEntity("0\nTEXT\n10\n0\n20\n0\n40\n1\n72\n1\n")->geometry(); });
        const auto colored = DXFCAD::parseEntity("0\nLINE\n62\n30\n");
        const auto color = DXFCAD::entityColor(*colored, {"Example"});
        check(color[0] == 1 && color[1] == 0.5 && color[2] == 0);
        const auto table = DXFCAD::DXFDocument::parse("0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n1\n0\nENDSEC\n0\nSECTION\n2\nTABLES\n0\nLAYER\n2\nHidden\n62\n-5\n0\nENDSEC\n"+document(lw));
        check(table.insertionUnits == 1 && !table.layers.at("Hidden").visible);
        std::cout << "DXF entity parsing, geometry, native records, and layers passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
