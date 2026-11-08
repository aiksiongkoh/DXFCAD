#include <DXFCAD/Drawing.h>
#include <DXFCAD/Drafting.h>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok) { if (!ok) throw std::runtime_error("Dimension check failed"); }
template<typename F> void rejects(F f) {
    try { f(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid dimension accepted");
}
const std::string dim = "0\nDIMENSION\n5\nAB\n2\n*D1\n70\n32\n10\n0\n20\n2\n11\n5\n21\n2\n13\n0\n23\n0\n14\n10\n24\n0\n";
const std::string line = "0\nLINE\n62\n0\n10\n0\n20\n2\n11\n10\n21\n2\n";
const std::string arrow = "0\nSOLID\n62\n0\n10\n0\n20\n2\n11\n1\n21\n2.25\n12\n1\n22\n1.75\n13\n1\n23\n1.75\n";
const std::string label = "0\nMTEXT\n62\n0\n10\n5\n20\n2.5\n40\n0.5\n71\n5\n1\n\\A1;10\n";
const std::string block = "0\nBLOCK\n2\n*D1\n70\n1\n10\n0\n20\n0\n"+line+arrow+label+"0\nPOINT\n10\n0\n20\n0\n0\nENDBLK\n";
std::string drawing(const std::string& pictures) {
    return "0\nSECTION\n2\nENTITIES\n"+dim+"0\nENDSEC\n0\nSECTION\n2\nBLOCKS\n"+pictures+"0\nENDSEC\n0\nEOF\n";
}
}
int main() {
    try {
        const auto raw = drawing(block);
        const auto doc = DXFCAD::Drawing::parse(raw);
        check(doc.entities.size() == 1 && doc.warnings.empty());
        const auto e = doc.entities[0];
        check(e->raw() == dim && e->context() == block);
        const auto g = e->geometry();
        check(g.segments.size() == 1 && g.faces.size() == 1 && g.texts.size() == 1 && g.texts[0].text == "10");
        check(e->snapPoints().size() == 3 && e->snapPoints()[2].kind == "midpoint");
        check(doc.serialize({{0,dim}}) == raw);
        check(DXFCAD::parseEntity(dim, block)->geometry().faces[0].size() == 3);
        auto unicodeBlock=block;
        unicodeBlock.replace(unicodeBlock.find("\\A1;10"),6,"\\A1;60\\U+00B0");
        const auto unicodeDimension=DXFCAD::parseEntity(dim,unicodeBlock);
        check(unicodeDimension->geometry().texts[0].text=="60\xc2\xb0");
        check(unicodeDimension->context()==unicodeBlock);
        for (int kind=0; kind<=6; ++kind) {
            std::string variant = "0\nDIMENSION\n5\nAB\n2\n*D1\n70\n"+std::to_string(32+kind)+
                "\n10\n0\n20\n2\n11\n5\n21\n2\n";
            if (kind==0 || kind==1 || kind==2 || kind==5 || kind==6)
                variant += "13\n0\n23\n0\n14\n10\n24\n0\n";
            if (kind==2 || kind==3 || kind==4 || kind==5) variant += "15\n3\n25\n4\n";
            if (kind==2) variant += "16\n5\n26\n6\n";
            const auto entity=DXFCAD::parseEntity(variant,block);
            check(entity->raw()==variant && entity->context()==block);
            const auto picture=entity->geometry();
            check(picture.segments.size()==1 && picture.faces.size()==1 && picture.texts.size()==1);
            check(picture.segments[0].start.x==0 && picture.segments[0].end.x==10);
            check(picture.texts[0].text=="10");
            const auto wrapped="0\nSECTION\n2\nBLOCKS\n"+block+"0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"+
                variant+"0\nENDSEC\n0\nEOF\n";
            const auto parsed=DXFCAD::Drawing::parse(wrapped);
            check(parsed.warnings.empty() && parsed.entities.size()==1 && parsed.serialize()==wrapped);
        }
        rejects([&] { DXFCAD::parseEntity(DXFCAD::patchEntityFields(DXFCAD::readDXFGroups(dim),{{70,39}}), block)->geometry(); });
        rejects([&] { DXFCAD::parseEntity("0\nDIMENSION\n2\n*D1\n70\n36\n10\n0\n20\n2\n11\n5\n21\n2\n", block)->geometry(); });
        rejects([&] { DXFCAD::parseEntity(dim)->geometry(); });
        rejects([&] { DXFCAD::parseEntity(dim+"12\n1\n", block)->geometry(); });
        rejects([&] { doc.serialize({{0,dim+"1\nChanged\n"}}); });
        check(DXFCAD::Drawing::parse(drawing("")).warnings.size() == 1);
        check(DXFCAD::Drawing::parse(drawing(block+block)).warnings.size() == 1);
        for (const std::string child : {"0\nINSERT\n2\nNested\n62\n0\n", "0\nLINE\n62\n1\n10\n0\n20\n0\n11\n1\n21\n1\n"}) {
            const auto bad = "0\nBLOCK\n2\n*D1\n"+child+"0\nENDBLK\n";
            rejects([&] { DXFCAD::parseEntity(dim, bad)->geometry(); });
            const auto rejected = DXFCAD::Drawing::parse(drawing(bad));
            check(rejected.entities.empty() && rejected.warnings.size() == 1 && rejected.serialize() == drawing(bad));
        }
        std::cout << "Cached dimension pictures, dependency snapshots, and export preservation passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
