#include <DXFCAD/DXFDocument.h>
#include <DXFCAD/OCSFrame.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool condition) { if (!condition) throw std::runtime_error("Editing check failed"); }
void near(double a, double b) { check(std::abs(a-b) < 1e-10); }
template<class F> void rejects(F f) {
    try { f(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid edit accepted");
}
}
int main()
{
    try {
        using namespace DXFCAD;
        const LWPolyline p({{0,0,1,1,2,true},{2,0}}, false, 3, 4, {0,2,0});
        const auto moved = p.translated({1,2,3});
        const auto a = p.worldSegments()[0], b = moved.worldSegments()[0];
        near(b.start.x-a.start.x, 1); near(b.start.y-a.start.y, 2); near(b.start.z-a.start.z, 3);
        near(b.arc->center.x-a.arc->center.x, 1);
        check(moved.extrusion().y == 2 && p.vertices()[0].x == 0);
        const auto rotated = p.rotated(std::acos(-1.0)/2, {1,0});
        near(rotated.vertices()[0].x, 1); near(rotated.vertices()[0].y, -1);
        near(rotated.vertices()[1].y, 1); check(rotated.elevation() == 4);
        const auto scaled = p.scaled(-2);
        check(scaled.vertices()[1].x == -4 && scaled.vertices()[0].bulge == 1);
        check(scaled.vertices()[0].endWidth == 4 && scaled.constantWidth() == 6);
        const auto mirrored = p.mirrored({0,0},{0,1});
        check(mirrored.vertices()[1].x == -2 && mirrored.vertices()[0].bulge == -1);
        near(mirrored.segments()[0].arc->center.x, -p.segments()[0].arc->center.x);
        const auto inserted = p.insertedVertex(1, {1,1});
        check(inserted.vertices().size() == 3 && inserted.removedVertex(1).vertices()[1].x == 2);
        rejects([&] { p.withVertex(0, {0,0,0,-1}); });
        rejects([&] { p.insertedVertex(3, {}); });
        rejects([&] { p.removedVertex(2); });
        rejects([&] { p.scaled(0); });
        rejects([&] { p.scaled(std::numeric_limits<double>::infinity()); });
        rejects([&] { p.mirrored({0,0},{0,0}); });
        rejects([&] { p.translated({0,0,std::numeric_limits<double>::quiet_NaN()}); });
        const auto picked = p.withVertexOnLine(0, {-4,-10,8}, {-4,10,8});
        check(picked.vertices()[0].x == 4 && picked.vertices()[0].y == 8);
        check(picked.vertices()[0].bulge == 1 && picked.vertices()[0].startWidth == 1);
        check(picked.vertices()[0].endWidth == 2 && picked.vertices()[0].hasWidths);
        check(picked.elevation() == 4 && picked.extrusion().y == 2);
        const auto world = picked.worldVertices()[0];
        near(world.x,-4); near(world.y,4); near(world.z,8);
        for (auto extrusion : {ExtrusionDirection{0,0,1}, ExtrusionDirection{0,0,-2}, ExtrusionDirection{1,2,3}}) {
            const LWPolyline tilted({{0,0},{2,0}}, false, 0, 7, extrusion);
            const OCSFrame frame(extrusion);
            const auto target = frame.toWorld({4,-5},7), normal = frame.normal();
            const Vector3D start{target.x+100*normal.x,target.y+100*normal.y,target.z+100*normal.z};
            const Vector3D end{target.x-100*normal.x,target.y-100*normal.y,target.z-100*normal.z};
            const auto result = tilted.withVertexOnLine(0,start,end);
            near(result.vertices()[0].x,4); near(result.vertices()[0].y,-5);
            check(tilted.vertices()[0].x == 0);
        }
        const LWPolyline straight({{0,0,0,1,2,true},{2,0}},false,3,7);
        const auto snapped = straight.withVertexAtVertex(0,1);
        check(snapped.vertices()[0].x == 2 && snapped.vertices()[0].startWidth == 1);
        rejects([&] { p.withVertexAtVertex(0,1); }); // coincident bulged endpoints
        rejects([&] { straight.withVertexAtVertex(5,1); });
        rejects([&] { straight.withVertexOnLine(0,{0,0,0},{1,1,0}); }); // edge-on
        rejects([&] { straight.withVertexOnLine(0,{0,0,0},{1,1,1e-14}); });
        rejects([&] { straight.withVertexOnLine(0,{1,1,1},{1,1,1}); });
        rejects([&] { straight.withVertexOnLine(0,{0,0,0},{0,0,std::numeric_limits<double>::infinity()}); });
        const auto worldSnap = p.withVertexAtWorldPoint(0, {-4,4,8});
        check(worldSnap.vertices()[0].x == 4 && worldSnap.vertices()[0].y == 8);
        check(worldSnap.vertices()[0].bulge == 1 && worldSnap.vertices()[0].hasWidths);
        check(worldSnap.elevation() == 4 && worldSnap.extrusion().y == 2);
        rejects([&] { p.withVertexAtWorldPoint(0,{-4,5,8}); });
        rejects([&] { p.withVertexAtWorldPoint(0,{-4,4,8},-1); });
        rejects([&] { p.withVertexAtWorldPoint(0,{-4,4,8},std::numeric_limits<double>::quiet_NaN()); });
        rejects([&] { p.withVertexAtWorldPoint(0,{-4,4,std::numeric_limits<double>::infinity()}); });
        rejects([&] { p.withVertexAtWorldPoint(0,{-2,4,0}); }); // collapsed bulged edge
        const auto lineSnaps = parseEntity("0\nLINE\n10\n1\n20\n2\n30\n3\n11\n4\n21\n5\n31\n6\n")->snapPoints();
        check(lineSnaps.size() == 3 && lineSnaps[0].kind == "endpoint" && lineSnaps[1].position.z == 6);
        check(lineSnaps[2].kind == "midpoint"); near(lineSnaps[2].position.z,4.5);
        const auto circleSnaps = parseEntity("0\nCIRCLE\n10\n1\n20\n2\n30\n3\n40\n4\n230\n-1\n")->snapPoints();
        check(circleSnaps.size() == 1 && circleSnaps[0].kind == "center");
        check(circleSnaps[0].position.x == -1 && circleSnaps[0].position.z == -3);
        const auto arcSnaps = parseEntity("0\nARC\n10\n0\n20\n0\n40\n2\n50\n0\n51\n90\n")->snapPoints();
        check(arcSnaps.size() == 4 && arcSnaps[2].kind == "center" && arcSnaps[3].kind == "midpoint");
        near(arcSnaps[0].position.x,2); near(arcSnaps[1].position.y,2);
        const auto textSnaps = parseEntity("0\nTEXT\n10\n1\n20\n2\n40\n1\n1\nTest\n")->snapPoints();
        check(textSnaps.size() == 1 && textSnaps[0].kind == "insertion point" && textSnaps[0].position.y == 2);

        const std::string raw = "0\nLWPOLYLINE\n5\nA1\n102\n{APP\n10\nopaque\n102\n}\n100\nAcDbEntity\n8\n0\n100\nAcDbPolyline\n"
            "90\n2\n70\n128\n39\n5\n10\n0\n20\n0\n91\n10\n42\n1\n10\n2\n20\n0\n91\n20\n"
            "999\nkeep comment\n1001\nAppData\n1040\n12.5\n1000\nkeep xdata\n";
        const auto source = readLWPolyline(raw);
        const auto edit = updateLWPolyline(raw, source.insertedVertex(1, {1,2}), {0,-1,1});
        check(edit.find("102\n{APP\n10\nopaque\n102\n}") != std::string::npos);
        check(edit.find("1001\nAppData\n1040\n12.5\n1000\nkeep xdata\n") != std::string::npos);
        check(edit.find("39\n5\n") != std::string::npos && edit.find("999\nkeep comment\n") != std::string::npos);
        check(parseEntity(edit)->record().integer(70) == 128);
        check(edit.find("91\n10\n") != std::string::npos && edit.find("91\n20\n") != std::string::npos);
        const auto deleted = updateLWPolyline(raw, source.removedVertex(0), {1});
        check(deleted.find("91\n10\n") == std::string::npos && deleted.find("91\n20\n") != std::string::npos);
        rejects([&] { updateLWPolyline(raw, source.insertedVertex(1, {1,2})); });
        rejects([&] { updateLWPolyline(raw, source, {0,0}); });
        rejects([&] { updateLWPolyline(raw, source, {0,5}); });
        rejects([&] { updateLWPolyline(raw, source.withVertex(1, {0,0})); });
        const auto created = createLWPolyline(p);
        const auto polySnaps = parseEntity(created)->snapPoints();
        check(polySnaps.size() == 3 && polySnaps[0].kind == "vertex" && polySnaps[0].position.y == 4 && polySnaps[2].kind == "midpoint");
        check(readLWPolyline(created).vertices()[0].hasWidths);
        check(readLWPolyline(created).extrusion().y == 2);
        check(parseEntity(created)->layer() == "0");

        const std::string unknown = "0\nSPLINE\n5\nB0\n999\nopaque unsupported record\n9\n$HANDSEED\n1000\nopaque seed\n9\n$ACADVER\n1000\nAC1009\n";
        const std::string header = "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n1\n9\n$HANDSEED\n5\nC0\n0\nENDSEC\n";
        const std::string blocks = "0\nSECTION\n2\nBLOCKS\n0\nBLOCK\n2\nUnmodeled\n0\nENDBLK\n0\nENDSEC\n";
        const std::string drawing = header+blocks+"0\nSECTION\n2\nENTITIES\n"+raw+unknown+
            "0\nCIRCLE\n5\nB1\n10\n0\n20\n0\n40\n-1\n0\nENDSEC\n0\nSECTION\n2\nOBJECTS\n0\nDICTIONARY\n5\nB2\n0\nENDSEC\n0\nEOF\n";
        const auto doc = DXFDocument::parse(drawing);
        check(doc.entities.size() == 1 && doc.warnings.size() == 2);
        check(doc.serialize() == drawing);
        const auto exported = doc.serialize({{0, edit}}, {}, {created});
        check(exported.find(unknown) != std::string::npos && exported.find(blocks) != std::string::npos);
        check(exported.find("40\n-1\n") != std::string::npos);
        const auto reread = DXFDocument::parse(exported);
        check(reread.entities.size() == 2 && reread.insertionUnits == 1);
        check(reread.entities[1]->handle() == "C1");
        check(reread.entities[0]->raw() == edit);
        check(DXFDocument::parse(doc.serialize({}, {0})).entities.empty());
        // Preservation alone must not require allocating another handle.
        const auto fullHandles = DXFDocument::parse("0\nSECTION\n2\nENTITIES\n0\nSPLINE\n5\nFFFFFFFFFFFFFFFF\n0\nENDSEC\n0\nEOF\n");
        check(fullHandles.serialize().find("FFFFFFFFFFFFFFFF") != std::string::npos);
        rejects([&] { fullHandles.serialize({}, {}, {created}); });
        const auto legacy = DXFDocument::parse("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1009\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nENDSEC\n0\nEOF\n");
        check(!legacy.serialize().empty());
        rejects([&] { legacy.serialize({}, {}, {created}); });
        rejects([&] { doc.serialize({{1,edit}}); });
        rejects([&] { doc.serialize({{0,created}}); });
        rejects([&] { doc.serialize({{0,edit}}, {0}); });
        rejects([&] { doc.serialize({}, {}, {raw}); });
        rejects([&] { doc.serialize({}, {}, {createLWPolyline(p, "Absent")}); });
        rejects([&] { parseEntity(created+unknown); });
        const auto fresh = DXFDocument::parse(DXFDocument::empty(4).serialize({}, {}, {created}));
        check(fresh.entities.size() == 1 && fresh.insertionUnits == 4);
        std::cout << "LWPolyline editing, transforms, metadata, and drawing round trips passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
