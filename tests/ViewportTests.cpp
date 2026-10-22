#include <DXFCAD/DXFDocument.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok) { if (!ok) throw std::runtime_error("Viewport check failed"); }
void near(double a, double b) { check(std::abs(a-b) < 1e-10); }
const std::string viewport = "0\nVIEWPORT\n5\nA\n330\nBB\n67\n1\n69\n2\n68\n2\n10\n100\n20\n200\n40\n10\n41\n5\n45\n50\n12\n2\n22\n3\n17\n10\n27\n20\n37\n30\n16\n0\n26\n0\n36\n2\n51\n90\n";
std::string drawing(const std::string& raw) {
    return "0\nSECTION\n2\nENTITIES\n"+raw+"0\nENDSEC\n"
        "0\nSECTION\n2\nTABLES\n0\nBLOCK_RECORD\n5\nBB\n340\nCC\n0\nENDSEC\n"
        "0\nSECTION\n2\nOBJECTS\n0\nLAYOUT\n5\nCC\n100\nAcDbPlotSettings\n1\nNot the layout name\n100\nAcDbLayout\n1\nSheet 1\n0\nENDSEC\n0\nEOF\n";
}
}
int main() {
    try {
        const auto raw = viewport+"102\n{opaque\n45\n-1\n102\n}\n1001\nAPP\n1000\npreserve\n";
        const auto source = drawing(raw);
        const auto doc = DXFCAD::DXFDocument::parse(source);
        check(doc.entities.empty() && doc.viewports.size() == 1 && doc.warnings.size() == 1);
        const auto& vp = doc.viewports[0];
        check(vp.raw() == raw && vp.layout() == "Sheet 1" && vp.id() == 2 && vp.navigationIssue().empty());
        near(vp.scale(), 0.1);
        const auto view = vp.modelView();
        near(view.center.x,13); near(view.center.y,18); near(view.center.z,30);
        near(view.xAxis.y,-1); near(view.yAxis.x,1); near(view.height,50);
        check(doc.serialize() == source);
        const auto named = DXFCAD::DXFDocument::parse(drawing(viewport+"410\nExplicit name\n"));
        check(named.viewports[0].layout() == "Explicit name");
        auto frozenSource = drawing(viewport+"331\n aa \n331\nAA\n102\n{opaque\n331\nMissing\n102\n}\n");
        const auto table = frozenSource.find("2\nTABLES\n")+std::string("2\nTABLES\n").size();
        const std::string layer = "0\nLAYER\n5\nAA\n2\nFrozen\n70\n1\n62\n7\n";
        frozenSource.insert(table,layer);
        const auto frozen = DXFCAD::DXFDocument::parse(frozenSource);
        check(frozen.viewports[0].navigationIssue().empty());
        check(frozen.viewports[0].frozenLayers() == std::vector<std::string>{"Frozen"});
        check(frozen.viewports[0].freezesLayer("FROZEN") && frozen.viewports[0].freezesLayer("frozen"));
        check(!frozen.viewports[0].freezesLayer("Other"));
        near(frozen.viewports[0].modelView().height,50);
        check(frozen.serialize() == frozenSource);
        frozenSource.insert(table,layer);
        const auto ambiguous = DXFCAD::DXFDocument::parse(frozenSource);
        check(!ambiguous.viewports[0].navigationIssue().empty());
        check(ambiguous.serialize() == frozenSource);
        for (const auto& change : {std::pair<std::string,std::string>{"69\n2\n","69\n1\n"},
             {"68\n2\n","68\n0\n"}, {"36\n2\n","36\n-1\n"}}) {
            auto record = viewport;
            record.replace(record.find(change.first), change.first.size(), change.second);
            const auto other = DXFCAD::DXFDocument::parse(drawing(record));
            check(other.viewports.size() == 1 && !other.viewports[0].navigationIssue().empty());
            try { other.viewports[0].modelView(); check(false); } catch (const std::invalid_argument&) {}
        }
        for (const auto& extra : {"90\n1\n", "90\n2\n", "90\n65536\n", "331\nAB\n"}) {
            const auto other = DXFCAD::DXFDocument::parse(drawing(viewport+extra));
            check(!other.viewports[0].navigationIssue().empty());
        }
        auto invalid = viewport;
        invalid.replace(invalid.find("45\n50\n"), 6, "45\n0\n");
        const auto rejected = DXFCAD::DXFDocument::parse(drawing(invalid));
        check(rejected.viewports.empty() && rejected.warnings.size() == 1 && rejected.serialize() == drawing(invalid));
        std::cout << "Viewport metadata, layout ownership, camera math, and preservation passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
