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
          "Unexpected derived geometry");
}

template<typename Error, typename Function>
void rejects(Function function)
{
    try {
        function();
    } catch (const Error&) {
        return;
    }
    throw std::runtime_error("Expected geometry error");
}

void checkLinesAndTopology()
{
    using DXFCAD::LWPolyline;
    check(LWPolyline().segments().empty(), "Empty entity has edges");
    check(LWPolyline({{1, 2, 1}}, true).segments().empty(), "Single vertex has edges");
    const LWPolyline open({{0, 0}, {2, 0}, {2, 3, 1}});
    const auto lines = open.segments();
    check(lines.size() == 2, "Open segment count");
    check(!lines[0].arc && !lines[1].arc, "Open final bulge must be unused");
    near(lines[0].start.x, 0); near(lines[0].end.x, 2);
    near(lines[1].start.y, 0); near(lines[1].end.y, 3);
    const LWPolyline closed(open.vertices(), true);
    const auto loop = closed.segments();
    check(loop.size() == 3 && loop.back().arc.has_value(), "Closing bulge was ignored");
    near(loop.back().start.x, 2); near(loop.back().start.y, 3);
    near(loop.back().end.x, 0); near(loop.back().end.y, 0);
    const auto duplicate = LWPolyline({{2, 3}, {2, 3}}).segments();
    check(duplicate.size() == 1 && !duplicate[0].arc, "Zero length line lost");
}

void checkArcs()
{
    using DXFCAD::LWPolyline;
    const double pi = std::acos(-1.0);
    for (double sign : {-1.0, 1.0}) {
        const auto semicircle = LWPolyline({{0, 0, sign}, {2, 0}}).segments()[0].arc.value();
        near(semicircle.center.x, 1); near(semicircle.center.y, 0);
        near(semicircle.radius, 1); near(semicircle.sweep, sign * pi);
        near(semicircle.midpoint.x, 1); near(semicircle.midpoint.y, -sign);

        const double quarterBulge = sign * (std::sqrt(2.0) - 1.0);
        const auto quarter = LWPolyline({{1, 0, quarterBulge}, {0, sign}}).segments()[0].arc.value();
        near(quarter.center.x, 0); near(quarter.center.y, 0);
        near(quarter.radius, 1); near(quarter.sweep, sign * pi / 2);
        near(quarter.midpoint.x, std::sqrt(0.5));
        near(quarter.midpoint.y, sign * std::sqrt(0.5));

        const double majorBulge = sign * (1.0 + std::sqrt(2.0));
        const auto major = LWPolyline({{0, 0, majorBulge}, {2, 0}}).segments()[0].arc.value();
        near(major.center.x, 1); near(major.center.y, -sign);
        near(major.radius, std::sqrt(2.0)); near(major.sweep, sign * 1.5 * pi);
        near(major.midpoint.x, 1); near(major.midpoint.y, -majorBulge);
    }
    const auto vertical = LWPolyline({{0, 0, 1}, {0, 2}}).segments()[0].arc.value();
    near(vertical.center.x, 0); near(vertical.center.y, 1);
    near(vertical.midpoint.x, 1); near(vertical.midpoint.y, 1);

    // Width/elevation/extrusion stay native; this API derives only OCS centerlines.
    const LWPolyline native({{0, 0, 1, 4, 5}, {2, 0}}, false, 8, 12, {2, 3, 4});
    near(native.segments()[0].arc->radius, 1);
    near(native.vertices()[0].bulge, 1); near(native.vertices()[0].startWidth, 4);
    near(native.vertices()[0].endWidth, 5); near(native.constantWidth(), 8);
    near(native.elevation(), 12); near(native.extrusion().z, 4);

    // A small nonzero bulge must remain an arc, not be silently flattened.
    const auto shallow = LWPolyline({{0, 0, 1e-12}, {2, 0}}).segments()[0].arc.value();
    near(shallow.sweep, 4e-12); near(shallow.radius, 5e11);
    near(shallow.midpoint.y, -1e-12);
}

void checkGeometryErrors()
{
    using DXFCAD::LWPolyline;
    rejects<std::domain_error>([] { LWPolyline({{0, 0, 1}, {0, 0}}).segments(); });
    const double huge = std::numeric_limits<double>::max();
    rejects<std::overflow_error>([=] { LWPolyline({{-huge, 0, 1}, {huge, 0}}).segments(); });
    rejects<std::overflow_error>([=] { LWPolyline({{0, 0, huge}, {2, 0}}).segments(); });
    rejects<std::overflow_error>([] {
        LWPolyline({{0, 0, std::numeric_limits<double>::denorm_min()}, {2, 0}}).segments();
    });
    // Failure to derive geometry must not discard otherwise finite native data.
    const LWPolyline native({{0, 0, 1}, {0, 0}});
    rejects<std::domain_error>([&] { native.segments(); });
    near(native.vertices()[0].bulge, 1);
    check(native.vertices().size() == 2, "Geometry failure changed native data");
}

} // namespace

int main()
{
    try {
        checkLinesAndTopology();
        checkArcs();
        checkGeometryErrors();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "LWPolyline OCS line and arc geometry passed.\n";
}
