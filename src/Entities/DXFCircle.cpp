#include "DXFCircle.h"
#include "EntityCoordinates.h"
#include <DXFCAD/CartesianFrame.h>
#include <DXFCAD/Drafting.h>

#include <cmath>
#include <stdexcept>
#include <utility>

namespace DXFCAD::Detail {

std::string DXFCircle::translated(const Vector3D& offset) const { return translatedPoints(offset,{10},true); }

EntityGeometry DXFCircle::geometry() const {
    const auto center = point(record_, 10);
    const CartesianFrame frame(extrusion(record_));
    return {{}, {{frame.toWorld({center.x, center.y}, center.z), frame.normal(), radius(record_)}}, {}};
}

} // namespace DXFCAD::Detail
