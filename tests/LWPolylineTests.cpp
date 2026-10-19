#include <DXFCAD/LWPolyline.h>

#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

void check(bool condition, const char* message)
{
    // Unlike assert(), these checks also run in Release builds.
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template<typename Function>
void rejects(Function construct)
{
    try {
        construct();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Invalid native data was accepted");
}

void checkDefaults()
{
    const DXFCAD::LWPolyline polyline;
    check(polyline.vertices().empty(), "Default vertices");
    check(!polyline.closed(), "Default closed state");
    check(polyline.constantWidth() == 0.0, "Default width");
    check(polyline.elevation() == 0.0, "Default elevation");
    check(polyline.extrusion().x == 0.0 && polyline.extrusion().y == 0.0
              && polyline.extrusion().z == 1.0, "Default extrusion");
}

void checkPreservation()
{
    std::vector<DXFCAD::LWPolylineVertex> vertices{
        {1.25, -2.5, -0.5, 0.25, 0.75},
        {9.0, 3.0, 2.0, 1.5, 2.5},
        {-4.0, 8.0, 0.0, 0.0, 0.0},
    };
    const DXFCAD::LWPolyline polyline(vertices, true, 4.0, -7.5, {2.0, -3.0, 4.0});
    check(polyline.vertices().size() == vertices.size(), "Vertex count changed");
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const auto& actual = polyline.vertices()[i];
        const auto& expected = vertices[i];
        check(actual.x == expected.x && actual.y == expected.y, "Vertex order/coordinates changed");
        check(actual.bulge == expected.bulge, "Bulge changed");
        check(actual.startWidth == expected.startWidth && actual.endWidth == expected.endWidth,
              "Segment widths changed");
    }
    check(polyline.closed(), "Closed state changed");
    check(polyline.constantWidth() == 4.0, "Constant width changed");
    check(polyline.elevation() == -7.5, "Elevation changed");
    check(polyline.extrusion().x == 2.0 && polyline.extrusion().y == -3.0
              && polyline.extrusion().z == 4.0, "Extrusion was normalized or changed");
    vertices[0].x = 100.0;
    check(polyline.vertices()[0].x == 1.25, "Polyline must own its native data");
    const DXFCAD::LWPolyline open(polyline.vertices());
    check(!open.closed() && open.vertices().back().x == -4.0,
          "Open polyline must preserve its last vertex");
}

void checkValidation()
{
    using DXFCAD::LWPolyline;
    using DXFCAD::LWPolylineVertex;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const double invalid : {-1.0, nan, inf, -inf}) {
        rejects([=] { LWPolyline p({}, false, invalid); });
        rejects([=] { LWPolyline p({{0.0, 0.0, 0.0, invalid, 0.0}}); });
        rejects([=] { LWPolyline p({{0.0, 0.0, 0.0, 0.0, invalid}}); });
    }
    for (const double invalid : {nan, inf, -inf}) {
        rejects([=] { LWPolyline p({}, false, 0.0, invalid); });
        for (int component = 0; component < 3; ++component) {
            rejects([=] {
                LWPolylineVertex vertex;
                if (component == 0) vertex.x = invalid;
                if (component == 1) vertex.y = invalid;
                if (component == 2) vertex.bulge = invalid;
                LWPolyline p({vertex});
            });
            rejects([=] {
                DXFCAD::ExtrusionDirection direction;
                if (component == 0) direction.x = invalid;
                if (component == 1) direction.y = invalid;
                if (component == 2) direction.z = invalid;
                LWPolyline p({}, false, 0.0, 0.0, direction);
            });
        }
    }
    rejects([] { LWPolyline p({}, false, 0.0, 0.0, {0.0, 0.0, 0.0}); });
}

} // namespace

int main()
{
    try {
        checkDefaults();
        checkPreservation();
        checkValidation();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "LWPolyline defaults, native data preservation, and validation passed.\n";
    return 0;
}
