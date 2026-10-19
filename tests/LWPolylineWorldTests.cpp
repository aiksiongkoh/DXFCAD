#include <DXFCAD/LWPolyline.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void near(double actual, double expected)
{
    check(std::isfinite(actual)
          && std::abs(actual - expected) <= 1e-12 * std::max(1.0, std::abs(expected)),
          "Unexpected world coordinate");
}

void near(const DXFCAD::Vector3D& point, double x, double y, double z)
{
    near(point.x, x); near(point.y, y); near(point.z, z);
}

void checkFrames()
{
    using DXFCAD::LWPolyline;
    const std::vector<DXFCAD::LWPolylineVertex> vertices{{10, 20}, {30, 40}};
    const auto elevated = LWPolyline(vertices, false, 0, 5).worldSegments();
    check(elevated.size() == 1 && !elevated[0].arc, "Straight world segment");
    near(elevated[0].start, 10, 20, 5); near(elevated[0].end, 30, 40, 5);
    near(LWPolyline(vertices, false, 0, 5, {0, 0, -2}).worldSegments()[0].start, -10, 20, -5);
    near(LWPolyline(vertices, false, 0, 5, {2, 0, 0}).worldSegments()[0].start, 5, 10, 20);
    near(LWPolyline(vertices, false, 0, 5, {0, 2, 0}).worldSegments()[0].start, -10, 5, 20);

    const double root2 = std::sqrt(2.0), root3 = std::sqrt(3.0), root6 = std::sqrt(6.0);
    const DXFCAD::Vector3D expected{-10 / root2 - 20 / root6 + 5 / root3,
                                   10 / root2 - 20 / root6 + 5 / root3,
                                   40 / root6 + 5 / root3};
    for (const double scale : {1.0, std::numeric_limits<double>::max(),
                               std::numeric_limits<double>::denorm_min()}) {
        const LWPolyline polyline(vertices, false, 0, 5, {scale, scale, scale});
        const auto actual = polyline.worldSegments()[0].start;
        near(actual, expected.x, expected.y, expected.z);
        check(polyline.extrusion().x == scale, "Native extrusion was normalized");
    }

    // Near-polar normals must select DXF's alternate reference axis. Check both
    // signs and the strict threshold boundary; arbitrary rotations differ here.
    for (double x : {-0.01, 0.01}) {
        const double z = std::sqrt(1 - x * x);
        near(LWPolyline({{1, 0}, {0, 0}}, false, 0, 0, {x, 0, z})
                 .worldSegments()[0].start, z, 0, -x);
    }
    for (double x : {-0.02, -1.0 / 64.0, 1.0 / 64.0, 0.02}) {
        const double z = std::sqrt(1 - x * x);
        near(LWPolyline({{1, 0}, {0, 0}}, false, 0, 0, {x, 0, z})
                 .worldSegments()[0].start, 0, std::copysign(1.0, x), 0);
    }
}

void checkWorldArcsAndTopology()
{
    using DXFCAD::LWPolyline;
    for (double bulge : {-1.0, 1.0}) {
        const LWPolyline polyline({{0, 0, bulge, 2, 4}, {2, 0}}, true, 8, 5, {0, 2, 0});
        const auto segments = polyline.worldSegments();
        check(segments.size() == 2 && segments[0].arc && !segments[1].arc, "World topology");
        near(segments[0].start, 0, 5, 0); near(segments[0].end, -2, 5, 0);
        near(segments[1].end, 0, 5, 0);
        const auto& arc = *segments[0].arc;
        near(arc.center, -1, 5, 0); near(arc.midpoint, -1, 5, -bulge);
        near(arc.normal, 0, 1, 0);
        near(arc.radius, 1); near(arc.sweep, bulge * std::acos(-1.0));
        near(polyline.vertices()[0].startWidth, 2); near(polyline.constantWidth(), 8);
        near(polyline.elevation(), 5); near(polyline.extrusion().y, 2);
        near(polyline.segments()[0].start.x, 0);
    }
    check(LWPolyline().worldSegments().empty(), "Empty world geometry");
    check(LWPolyline({{1, 2}}, true).worldSegments().empty(), "Singleton world geometry");
    const auto duplicate = LWPolyline({{1, 2}, {1, 2}}, false, 0, -5).worldSegments();
    near(duplicate[0].start, 1, 2, -5); near(duplicate[0].end, 1, 2, -5);
}

void checkErrors()
{
    const double huge = std::numeric_limits<double>::max();
    const DXFCAD::LWPolyline native({{-huge, -huge}, {0, 0}}, false, 0, huge, {1, 1, 1});
    bool rejected = false;
    try { native.worldSegments(); }
    catch (const std::overflow_error&) { rejected = true; }
    check(rejected, "World-coordinate overflow accepted");
    check(native.vertices()[0].x == -huge && native.elevation() == huge,
          "World geometry failure changed native data");
    rejected = false;
    try { DXFCAD::LWPolyline({{0, 0, 1}, {0, 0}}).worldSegments(); }
    catch (const std::domain_error&) { rejected = true; }
    check(rejected, "Coincident arc error was not propagated");
}

} // namespace

int main()
{
    try {
        checkFrames();
        checkWorldArcsAndTopology();
        checkErrors();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "LWPolyline world-coordinate centerlines passed.\n";
}
