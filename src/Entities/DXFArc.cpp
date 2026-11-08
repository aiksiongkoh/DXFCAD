#include "DXFArc.h"
#include "EntityCoordinates.h"
#include <DXFCAD/CartesianFrame.h>
#include <DXFCAD/Drafting.h>

#include <cmath>
#include <stdexcept>
#include <utility>

namespace DXFCAD::Detail {

std::string DXFArc::translated(const Vector3D& offset) const { return translatedPoints(offset,{10},true); }

EntityGeometry DXFArc::geometry() const {
    const auto center = point(record_, 10);
    const CartesianFrame frame(extrusion(record_));
    const double r = radius(record_), pi = std::acos(-1.0);
    const double start = record_.number(50)*pi/180.0;
    double degrees = std::fmod(record_.number(51)-record_.number(50), 360.0);
    if (degrees <= 0) degrees += 360;
    if (degrees == 360 && record_.number(51)==record_.number(50))
        throw std::invalid_argument("ARC with equal angles is ambiguous; use CIRCLE");
    const double sweep = degrees*pi/180.0;
    auto at = [&](double a) { return frame.toWorld({center.x+r*std::cos(a), center.y+r*std::sin(a)}, center.z); };
    const LWPolylineArc3D arc{frame.toWorld({center.x, center.y}, center.z),
                             at(start+sweep/2), frame.normal(), r, sweep};
    return {{{at(start), at(start+sweep), arc}}, {}, {}};
}

} // namespace DXFCAD::Detail
