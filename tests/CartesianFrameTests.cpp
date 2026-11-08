#include <DXFCAD/CartesianFrame.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace DXFCAD;
namespace {
void near(double actual, double expected)
{
    // Dimensionless unit axes and modest coordinates: absolute 1e-12 is well
    // above double rounding error and well below any modeling tolerance.
    if (std::abs(actual - expected) > 1e-12 || !std::isfinite(actual))
        throw std::runtime_error("Cartesian frame numerical contract failed");
}
void near(const Vector3D& actual, const Vector3D& expected)
{
    near(actual.x, expected.x); near(actual.y, expected.y); near(actual.z, expected.z);
}
double dot(const Vector3D& a, const Vector3D& b)
{
    return a.x*b.x + a.y*b.y + a.z*b.z;
}
}

int main()
{
    try {
        const CartesianFrame upright({0, 0, 2});
        near(upright.xAxis(), {1, 0, 0});
        near(upright.yAxis(), {0, 1, 0});
        near(upright.normal(), {0, 0, 1});
        near(upright.toWorld({1, 2}, 3), {1, 2, 3});
        const CartesianFrame down({0, 0, -2});
        near(down.xAxis(), {-1, 0, 0});
        near(down.yAxis(), {0, 1, 0});
        near(down.toWorld({1, 2}, 3), {-1, 2, -3});
        const CartesianFrame tilted({1, 1, 1});
        const double r2 = std::sqrt(2.0), r3 = std::sqrt(3.0), r6 = std::sqrt(6.0);
        near(tilted.xAxis(), {-1/r2, 1/r2, 0});
        near(tilted.yAxis(), {-1/r6, -1/r6, 2/r6});
        near(tilted.normal(), {1/r3, 1/r3, 1/r3});
        for (const Vector3D normal : {Vector3D{1, 2, 3}, {0, 2, 0},
                                     {1e300, 2e300, 3e300}, {1e-300, 2e-300, 3e-300}}) {
            const CartesianFrame frame(normal);
            const auto x = frame.xAxis(), y = frame.yAxis(), z = frame.normal();
            near(dot(x,x), 1); near(dot(y,y), 1); near(dot(z,z), 1);
            near(dot(x,y), 0); near(dot(x,z), 0); near(dot(y,z), 0);
            near(Vector3D{x.y*y.z-x.z*y.y, x.z*y.x-x.x*y.z, x.x*y.y-x.y*y.x}, z);
            near(frame.fromWorld(frame.toWorld({2, -3}, 4)), {2, -3, 4});
            const auto hit = frame.intersectLine(frame.toWorld({2,-3}, -5),
                                                 frame.toWorld({2,-3}, 5), 4);
            near(hit.x, 2); near(hit.y, -3);
        }
        Vector3D original{0, 0, 7};
        const CartesianFrame copied(original);
        near(original.z, 7);
        original.z = -7;
        near(copied.normal(), {0, 0, 1});
        for (const Vector3D invalid : {Vector3D{}, {0,0,std::numeric_limits<double>::infinity()},
                                      {std::numeric_limits<double>::quiet_NaN(),0,1}}) {
            bool rejected = false;
            try { const CartesianFrame frame(invalid); }
            catch (const std::invalid_argument&) { rejected = true; }
            if (!rejected) throw std::runtime_error("Invalid normal was accepted");
        }
        bool parallelRejected = false;
        try { upright.intersectLine({0,0,0}, {1,0,0}, 1); }
        catch (const std::domain_error&) { parallelRejected = true; }
        if (!parallelRejected) throw std::runtime_error("Parallel line was accepted");
        std::cout << "Cartesian axes, analytical mappings, intersections and validation passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
