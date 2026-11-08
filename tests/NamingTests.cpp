#include <DXFCAD/GroupSequence.h>
#include <DXFCAD/Drawing.h>
#include <DXFCAD/DXFLWPolyline.h>
#include <DXFCAD/DXFPolyline.h>
#include <DXFCAD/CartesianFrame.h>
#include <DXFCAD/ParametricCurves.h>
#include <iostream>
#include <stdexcept>
#include <type_traits>

using namespace DXFCAD;
static_assert(std::is_base_of_v<DXFEntity, DXFLWPolyline>);
static_assert(std::is_base_of_v<DXFEntity, DXFPolyline>);
static_assert(std::is_base_of_v<DXFEntity, DXFEllipse>);
static_assert(std::is_base_of_v<DXFEntity, DXFSpline>);

int main()
{
    try {
        const std::string raw = "0\nLWPOLYLINE\n8\n0\n90\n2\n70\n0\n10\n1\n20\n2\n10\n4\n20\n6\n";
        const auto entity = parseEntity(raw);
        const auto* polyline = dynamic_cast<const DXFLWPolyline*>(entity.get());
        if (!polyline || polyline->raw() != raw || polyline->vertices().size() != 2)
            throw std::runtime_error("Canonical type lost native LWPOLYLINE data");
        const auto drawing = Drawing::empty();
        const auto restored = Drawing::parse(drawing.serialize({}, {}, {raw}));
        if (restored.entities.size() != 1 ||
            !dynamic_cast<const DXFLWPolyline*>(restored.entities.front().get()))
            throw std::runtime_error("Drawing did not restore the native entity type");
        const auto point = CartesianFrame({0, 0, 1}).toWorld({1, 2}, 3);
        if (point.x != 1 || point.y != 2 || point.z != 3)
            throw std::runtime_error("Renamed OCS frame changed default coordinate mapping");
        std::cout << "Native entity types, drawing roundtrip and OCS mapping passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
