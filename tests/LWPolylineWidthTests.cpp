#include <DXFCAD/LWPolyline.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value) { if (!value) throw std::runtime_error("Width region check failed"); }
void near(double a, double b, double tolerance = 1e-10) { check(std::abs(a-b) <= tolerance); }
double area(const DXFCAD::LWPolylineWidthRegion& r) {
    double sum = 0;
    for (std::size_t i = 0; i < r.points.size(); ++i) {
        const auto& a = r.points[i]; const auto& b = r.points[(i+1)%r.points.size()];
        sum += a.x*b.y-a.y*b.x;
    }
    return std::abs(sum)/2;
}
template<typename Error, typename F> void rejects(F f) {
    try { f(); } catch (const Error&) { return; }
    throw std::runtime_error("Invalid width derivation accepted");
}
}
int main()
{
    try {
        using DXFCAD::LWPolyline;
        check(LWPolyline().widthRegions().empty());
        check(LWPolyline({{0,0},{4,0}}).widthRegions().empty());
        const LWPolyline tapered({{0,0,0,2,4},{4,0}});
        const auto trapezoid = tapered.widthRegions().at(0);
        check(trapezoid.points.size() == 4 && trapezoid.segmentIndex == 0);
        near(trapezoid.points[0].y,1); near(trapezoid.points[1].y,2);
        near(trapezoid.points[2].y,-2); near(area(trapezoid),12);
        near(tapered.vertices()[0].startWidth,2);
        near(area(LWPolyline({{0,0},{4,0}},false,2).widthRegions()[0]),8);
        check(LWPolyline({{0,0,0,0,0,true},{4,0}},false,2).widthRegions().empty());
        check(LWPolyline({{0,0},{4,0,0,0,0,true}},false,2).widthRegions().empty());
        near(area(LWPolyline({{0,0,0,0,2},{4,0}}).widthRegions()[0]),4);
        near(area(LWPolyline({{0,0,1e-12,2,2},{2,0}}).widthRegions(1e-8)[0]),4,1e-8);
        check(LWPolyline({{1,2,0,3,3},{1,2}}).widthRegions().empty());
        const double pi = std::acos(-1.0);
        for (double sign : {-1.0,1.0}) {
            const double bulge = sign*(std::sqrt(2.0)-1);
            const auto quarter = LWPolyline({{1,0,bulge,0.4,0.4},{0,sign}}).widthRegions(1e-5);
            check(quarter.size() == 1);
            near(area(quarter[0]),0.2*pi,0.0001);
            near(quarter[0].points[0].x,1.2);
            const auto varied = LWPolyline({{1,0,bulge,0.2,0.6},{0,sign}}).widthRegions(1e-5);
            near(area(varied[0]),0.2*pi,0.0001);
            const auto wide = LWPolyline({{1,0,bulge,3,3},{0,sign}}).widthRegions(1e-5);
            check(wide.size() == 2);
            near(area(wide[0])+area(wide[1]),(6.25+0.25)*pi/4,0.0002);
            const auto cross = LWPolyline({{1,0,bulge,1,3},{0,sign}}).widthRegions();
            check(cross.size() == 2);
        }
        const auto closed = LWPolyline({{0,0,0,1,1},{4,0,0,1,1},{2,3,0,1,1}},true).widthRegions();
        check(closed.size() == 3 && closed[2].segmentIndex == 2);
        const auto tilted = LWPolyline({{0,0,0,2,2},{4,0}},false,0,5,{0,2,0}).widthRegions()[0];
        near(tilted.points[0].x,0); near(tilted.points[0].y,5); near(tilted.points[0].z,1);
        for (double t : {0.0,-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
            rejects<std::invalid_argument>([&] { tapered.widthRegions(t); });
        }
        rejects<std::overflow_error>([] {
            LWPolyline({{0,0,1,1,1},{2,0}}).widthRegions(1e-30);
        });
        std::cout << "Width priority, tapered lines/arcs, wide arcs, topology, and OCS passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
