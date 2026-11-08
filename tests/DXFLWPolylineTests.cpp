#include <DXFCAD/Drawing.h>
#include <DXFCAD/DXFLWPolyline.h>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {
void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected)
{
    check(std::abs(actual-expected) < 1e-10, "Unexpected geometry");
}
template<class F> void rejects(F action)
{
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid native data accepted");
}
}

int main()
{
    try {
        using namespace DXFCAD;
        static_assert(std::is_base_of_v<DXFEntity, DXFLWPolyline>);
        const std::string raw =
            "0\nLWPOLYLINE\n5\nA1\n330\nB2\n100\nAcDbEntity\n8\nCurves\n"
            "102\n{APP\n10\nopaque\n102\n}\n100\nAcDbPolyline\n90\n2\n70\n128\n"
            "39\n5\n10\n0\n20\n0\n42\n1\n91\n11\n10\n2\n20\n0\n91\n22\n"
            "999\nkeep comment\n1001\nAppData\n1040\n12.5\n1000\nkeep xdata\n";
        const auto entity = parseEntity(raw);
        const auto* polyline = dynamic_cast<const DXFLWPolyline*>(entity.get());
        check(polyline != nullptr, "Factory must construct DXFLWPolyline");
        check(polyline->raw() == raw, "Reading changed native data");
        check(polyline->handle() == "A1" && polyline->layer() == "Curves", "Lost entity identity");
        const auto arc = entity->geometry().segments.at(0).arc.value();
        near(arc.center.x, 1); near(arc.center.y, 0); near(arc.radius, 1);
        near(arc.sweep, std::acos(-1.0)); near(arc.midpoint.y, -1);
        check(entity->snapPoints().size() == 3, "Missing polyline snap points");

        const auto moved = polyline->moved({1,2,3});
        near(moved.vertices()[0].x, 1); near(moved.vertices()[0].y, 2); near(moved.elevation(), 3);
        check(entity->translated({1,2,3}) == moved.raw(), "Base virtual dispatch disagrees with editing");
        const auto inserted = moved.insertedVertex(1, {2,4});
        const auto edited = inserted.removedVertex(0).withProperties(true, 2, 7, {0,0,-2});
        check(edited.raw().find("91\n11\n") == std::string::npos, "Deleted vertex ID retained");
        check(edited.raw().find("91\n22\n") != std::string::npos, "Surviving vertex ID lost");
        check(edited.record().integer(70) == 129, "Non-closure flags lost");
        check(edited.handle() == "A1" && edited.record().value(330) == "B2", "Identity/owner changed");
        for (const auto& opaque : {"102\n{APP\n10\nopaque\n102\n}\n", "39\n5\n", "999\nkeep comment\n",
                                   "1001\nAppData\n1040\n12.5\n1000\nkeep xdata\n"})
            check(edited.raw().find(opaque) != std::string::npos, "Opaque/native metadata lost");
        check(polyline->raw() == raw, "Editing mutated the source entity");
        const auto empty = polyline->removedVertex(1).removedVertex(0);
        check(empty.vertices().empty() && empty.record().integer(90) == 0,
              "Removing the last vertex must support the empty editing state");
        check(empty.handle() == "A1", "Removing all vertices lost entity identity");
        const DXFLWPolyline explicitWidths({{0,0,0,1,2}, {2,0}});
        check(explicitWidths.vertices()[0].hasWidths
              && DXFLWPolyline::fromRaw(explicitWidths.raw()).vertices()[0].hasWidths,
              "Decoded widths must agree with emitted native fields");
        const auto reread = DXFLWPolyline::fromRaw(edited.raw());
        near(reread.vertices()[0].x, edited.vertices()[0].x);
        check(reread.closed() && reread.extrusion().z == -2, "Edited properties did not persist");

        const auto document = Drawing::parse("0\nSECTION\n2\nENTITIES\n"+raw+"0\nENDSEC\n0\nEOF\n");
        check(dynamic_cast<const DXFLWPolyline*>(document.entities.at(0).get()) != nullptr,
              "Drawing parser lost concrete type");
        check(document.serialize().find(raw) != std::string::npos, "Unedited drawing did not preserve source");
        const auto transformed = DXFLWPolyline::fromRaw(entity->rotated(90, {0,0,0}));
        near(transformed.worldVertices()[1].x, 0); near(transformed.worldVertices()[1].y, 2);
        const auto inPlane = polyline->rotatedInPlane(std::acos(-1.0)/2);
        near(inPlane.vertices()[1].x, 0); near(inPlane.vertices()[1].y, 2);

        rejects([] { DXFLWPolyline::fromRaw("0\nLINE\n10\n0\n20\n0\n"); });
        rejects([] { DXFLWPolyline::fromRaw("0\nLWPOLYLINE\n90\n2\n10\n0\n20\n0\n"); });
        rejects([&] { DXFLWPolyline::fromRaw(raw+"0\nCIRCLE\n"); });
        rejects([&] { polyline->withVertex(0, {0,0,0,-1}); });
        check(polyline->raw() == raw, "Failed edit changed the source");
        std::cout << "DXFLWPolyline inheritance, geometry, native identity and metadata passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
