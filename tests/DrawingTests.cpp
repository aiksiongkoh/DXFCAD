#include <DXFCAD/Drawing.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool yes, const char* message = "DXF document check failed") { if (!yes) throw std::runtime_error(message); }
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
        for (const std::string trailer : {"\n\n", "\x1a", "\x1a\r\n\r\n", " \t\r\n\x1a\n"}) {
            const auto terminated = DXFCAD::Drawing::parse(document(lw) + trailer);
            check(terminated.entities.size() == 1 && terminated.entities[0]->raw() == lw,
                  "text-file trailer must preserve entity data");
            check(terminated.serialize() == document(lw), "export must end at DXF EOF");
        }
        rejects([&] { DXFCAD::Drawing::parse(document(lw) + "999\n"); });
        rejects([&] { DXFCAD::Drawing::parse(document(lw) + "\x1a\ninvalid\n"); });
        rejects([&] { DXFCAD::Drawing::parse("0\nSECTION\n2\nENTITIES\n" + lw + "\x1a\n"); });
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
        check(DXFCAD::Drawing::parse(document(centered->raw())).warnings.empty());
        rejects([&] { tg.alignedOrigin(5, 0, 1, 2); });
        for (int horizontal : {-1, 3, 4, 5, 6}) {
            rejects([&] { DXFCAD::parseEntity("0\nTEXT\n11\n0\n21\n0\n40\n1\n72\n"+std::to_string(horizontal)+"\n")->geometry(); });
        }
        const auto doc = DXFCAD::Drawing::parse(document(lw+line->raw()+"0\nSPLINE\n5\nZZ\n"));
        check(doc.entities.size() == 2 && doc.warnings.size() == 1);
        const auto edited=line->translated({2,0,0});
        const auto assembled=doc.serializeCurrent({{1,edited},{-1,line->copied({0,5,0})}});
        const auto current=DXFCAD::Drawing::parse(assembled);
        check(current.entities.size()==2 && current.entities[0]->raw()==edited);
        check(assembled.find("0\nSPLINE\n5\nZZ\n")!=std::string::npos);
        check(DXFCAD::Drawing::parse(doc.serializeCurrent({})).entities.empty());
        rejects([&] { doc.serializeCurrent({{0,lw},{0,lw}}); });
        rejects([&] { doc.serializeCurrent({{-2,lw}}); });
        rejects([&] { doc.serializeCurrent({{2,lw}}); });
        rejects([] { DXFCAD::Drawing::parse("AutoCAD Binary DXF"); });
        rejects([] { DXFCAD::Drawing::parse("0\nSECTION\n2\nENTITIES\n"); });
        rejects([] { DXFCAD::Drawing::parse("0\nSECTION\n2"); });
        rejects([] { DXFCAD::parseEntity("0\nLWPOLYLINE\n90\n2\n10\n0\n20\n0\n")->geometry(); });
        rejects([] { DXFCAD::parseEntity("0\nCIRCLE\n10\n0\n20\n0\n40\n-1\n")->geometry(); });
        rejects([] { DXFCAD::parseEntity("0\nTEXT\n10\n0\n20\n0\n40\n1\n72\n1\n")->geometry(); });
        const auto colored = DXFCAD::parseEntity("0\nLINE\n62\n30\n");
        const auto color = DXFCAD::entityColor(*colored, {"Example"});
        check(color[0] == 1 && color[1] == 0.5 && color[2] == 0);
        const auto table = DXFCAD::Drawing::parse("0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n1\n0\nENDSEC\n0\nSECTION\n2\nTABLES\n0\nLAYER\n2\nHidden\n62\n-5\n0\nENDSEC\n"+document(lw));
        check(table.insertionUnits == 1 && !table.layers.at("Hidden").visible);
        const std::string legacyHeader = "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$DWGCODEPAGE\n3\nANSI_1252\n0\nENDSEC\n";
        const std::string legacyText = "0\nMTEXT\n5\nCD\n10\n0\n20\n0\n40\n1\n3\nM\xe9" "dica \\P\n7\nStandard\n1\n\x80\n102\n{opaque\n1\nkeep\n102\n}\n1001\nAPP\n1000\nunchanged\n";
        const auto legacy = DXFCAD::Drawing::parse(legacyHeader + document(legacyText));
        check(legacy.entities.at(0)->raw() == legacyText, "legacy raw record");
        auto escaped = legacyText;
        escaped.replace(escaped.find('\xe9'), 1, "\\U+00E9");
        escaped.replace(escaped.find('\x80'), 1, "\\U+20AC");
        check(DXFCAD::parseEntity(escaped)->geometry().text->text == "M\xc3\xa9" "dica \n\xe2\x82\xac", "legacy Unicode display");
        check(legacy.serializeCurrent({{0, escaped}}) == legacyHeader + document(legacyText), "legacy unchanged export");
        const auto movedText = DXFCAD::parseEntity(escaped)->translated({2,3,0});
        const auto movedLegacy = legacy.serializeCurrent({{0, movedText}});
        check(movedLegacy.find("3\nM\xe9" "dica \\P\n7\nStandard\n1\n\x80\n") != std::string::npos, "legacy moved bytes");
        check(DXFCAD::Drawing::parse(movedLegacy).entities[0]->geometry().text->origin.x == 2, "legacy moved geometry");
        auto changed = escaped;
        const std::string oldContent = "M\\U+00E9dica";
        changed.replace(changed.find(oldContent), oldContent.size(), "Changed");
        check(legacy.serializeCurrent({{0, changed}}).find("Changed") != std::string::npos, "legacy edited content");
        const std::string escapedText = "0\nTEXT\n5\nEF\n10\n0\n20\n0\n40\n1\n1\nL\\U+00E1piz \\U+D83D\\U+DE00\n";
        check(DXFCAD::parseEntity(escapedText)->geometry().text->text == "L\xc3\xa1piz \xf0\x9f\x98\x80", "TEXT Unicode display");
        rejects([&] { DXFCAD::parseEntity("0\nTEXT\n10\n0\n20\n0\n40\n1\n1\n\\U+D800\n")->geometry(); });
        const std::string legacySingle = "0\nTEXT\n5\nEF\n10\n0\n20\n0\n40\n1\n1\nL\xe1piz\n";
        const auto legacySingleDoc = DXFCAD::Drawing::parse(legacyHeader + document(legacySingle));
        auto escapedSingle = legacySingle;
        escapedSingle.replace(escapedSingle.find('\xe1'), 1, "\\U+00E1");
        check(legacySingleDoc.serializeCurrent({{0, escapedSingle}}) == legacyHeader + document(legacySingle), "TEXT legacy unchanged export");
        std::cout << "DXF entity parsing, geometry, native records, and layers passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
