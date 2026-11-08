#include <DXFCAD/Drawing.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok) { if (!ok) throw std::runtime_error("MTEXT check failed"); }
void near(double a, double b) { check(std::abs(a-b) < 1e-10); }
template<typename F> void rejects(F f) {
    try { f(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Unsupported MTEXT accepted");
}
const std::string base = "0\nMTEXT\n5\nAB\n10\n10\n20\n20\n30\n30\n40\n2\n";
DXFCAD::TextBounds measure(const std::string& value) {
    std::size_t count = 0;
    for (unsigned char c : value) if ((c & 0xc0) != 0x80) ++count;
    return {0, 0, static_cast<double>(count), value.empty() ? 0.0 : 2.0};
}
}

int main() {
    try {
        const auto raw = base+"3\none "+"\n1\ntwo\\P\\P"+"\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"+"\n41\n3\n1001\nAPP\n1000\npreserve\n";
        const auto entity = DXFCAD::parseEntity(raw);
        check(entity->raw() == raw && entity->type() == "MTEXT");
        const auto text = *entity->geometry().text;
        const auto runs = text.layout(measure);
        check(runs.size() == 4 && runs[0].text == "one" && runs[1].text == "two");
        check(runs[2].text == "\xc3\xa9\xc3\xa9\xc3\xa9" && runs[3].text == "\xc3\xa9");
        near(runs[0].origin.x, 10); near(runs[0].origin.y, 18);
        near(runs[2].origin.y, 18-3*2*5.0/3.0); // Empty paragraph keeps its height.
        for (int attachment = 1; attachment <= 9; ++attachment) {
            const auto t = *DXFCAD::parseEntity(base+"1\nA\n41\n10\n71\n"+std::to_string(attachment)+"\n")->geometry().text;
            const auto r = t.layout(measure).at(0);
            near(r.origin.x, 10-((attachment-1)%3)*5);
            near(r.origin.y, 18+(attachment-1)/3);
        }
        const auto escaped = *DXFCAD::parseEntity(base+"1\n\\{A\\}\\\\\\~B\n")->geometry().text;
        check(escaped.text == "{A}\\\xc2\xa0" "B");
        const auto unicodeRaw=base+"3\n45\\U+00\n1\nB0 \\U+03a9 \\U+D83D\\U+DE00\n";
        const auto unicode=DXFCAD::parseEntity(unicodeRaw);
        check(unicode->geometry().text->text=="45\xc2\xb0 \xce\xa9 \xf0\x9f\x98\x80");
        check(unicode->raw()==unicodeRaw);
        check(DXFCAD::parseEntity(base+"1\n\\\\U+00B0\n")->geometry().text->text=="\\U+00B0");
        // Synthetic equivalents of grouped font and uniform-height MTEXT.
        const auto groupedRaw = base+"3\n{\\fArial|b0|i0|c0|p34;First\\P\n1\nSecond}\n";
        const auto grouped = DXFCAD::parseEntity(groupedRaw);
        check(grouped->raw() == groupedRaw);
        check(grouped->geometry().text->text == "First\nSecond");
        near(grouped->geometry().text->height, 2);
        const auto scaledRaw = base+"1\n   {\\H0.75x;First\\PSecond }\n";
        const auto scaled = *DXFCAD::parseEntity(scaledRaw)->geometry().text;
        check(scaled.text == "   First\nSecond ");
        near(scaled.height, 1.5);
        near(scaled.lineSpacing*scaled.height*5.0/3.0, 2*5.0/3.0);
        const auto absolute = *DXFCAD::parseEntity(base+"1\n{\\H3;A}{\\H1.5x;B}\n")->geometry().text;
        check(absolute.text == "AB"); near(absolute.height, 3);
        const auto restored = *DXFCAD::parseEntity(base+"1\n{\\H3;{\\H2;A}}B\n")->geometry().text;
        check(restored.text == "AB"); near(restored.height, 2);
        for (int style : {1, 2}) {
            const auto large = *DXFCAD::parseEntity(base+"1\n{\\H2x;A\\PB}\n73\n"+std::to_string(style)+"\n")->geometry().text;
            near(large.height, 4);
            near(large.height*large.lineSpacing*5.0/3.0, (style == 1 ? 4 : 2)*5.0/3.0);
        }
        const auto groupedDrawing = "0\nSECTION\n2\nENTITIES\n"+groupedRaw+"0\nENDSEC\n0\nEOF\n";
        const auto groupedDoc = DXFCAD::Drawing::parse(groupedDrawing);
        check(groupedDoc.warnings.empty() && groupedDoc.entities.size() == 1 && groupedDoc.serialize() == groupedDrawing);
        const auto tilted = *DXFCAD::parseEntity(base+"1\nA\n210\n0\n220\n1\n230\n0\n11\n0\n21\n0\n31\n2\n")->geometry().text;
        near(tilted.origin.x, 10); near(tilted.origin.y, 20); near(tilted.origin.z, 30);
        near(tilted.xAxis.z, 1); near(tilted.yAxis.x, 1);
        near(tilted.layout(measure)[0].origin.x, 8);
        const auto rotated = *DXFCAD::parseEntity(base+"1\nA\n11\n1\n21\n0\n50\n1.5707963267948966\n")->geometry().text;
        near(rotated.xAxis.y, 1);
        const auto vectorWins = *DXFCAD::parseEntity(base+"1\nA\n50\n1.5707963267948966\n11\n1\n21\n0\n")->geometry().text;
        near(vectorWins.xAxis.x, 1);
        for (const std::string extra : {"71\n0\n", "71\n10\n", "72\n3\n", "41\n-1\n", "44\n0\n",
             "75\n1\n", "90\n1\n", "11\n0\n21\n0\n", "11\n0\n21\n0\n31\n1\n",
             "1\n\\W2;Styled\n", "1\n{Unclosed\n", "1\nUnopened}\n", "1\nBad\\\n",
             "1\n{\\H2x;A}B\n", "1\n\\H0;Bad\n", "1\n\\H-1x;Bad\n",
             "1\n\\Hnan;Bad\n", "1\n\\H1e999x;Bad\n", "1\n\\H2oops;Bad\n", "1\n\\H2\n",
             "1\n\\f;Bad\n", "1\n\\fArial|b1;Bold\n", "1\n\\fArial|i1;Italic\n",
             "1\n\\fArial|cwrong;Bad\n", "1\n\\fArial\n", "1\nA\tB\n",
             "1\n{\\H3;A} {\\H3;B}\n", "1\n\\fArial\\H3;Bad\n",
             "1\n\\U+00\n", "1\n\\U+00G0\n", "1\n\\U00B0\n",
             "1\n\\U+D800\n", "1\n\\U+DC00\n", "1\n\\U+D800\\U+0041\n", "1\n\\U+0000\n",
             "1\n{{{{{{{{{Too deep}}}}}}}}}\n"}) {
            rejects([&] { DXFCAD::parseEntity(base+extra)->geometry(); });
        }
        const auto drawing = "0\nSECTION\n2\nENTITIES\n"+raw+"0\nENDSEC\n0\nEOF\n";
        const auto doc = DXFCAD::Drawing::parse(drawing);
        check(doc.warnings.empty() && doc.entities.size() == 1 && doc.serialize() == drawing);
        std::cout << "MTEXT parsing, layout, WCS placement, and record preservation passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
