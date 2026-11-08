#include "DXFLine.h"
#include "EntityCoordinates.h"
#include <DXFCAD/CartesianFrame.h>
#include <DXFCAD/Drafting.h>

#include <cmath>
#include <stdexcept>
#include <utility>

namespace DXFCAD::Detail {

std::string DXFLine::translated(const Vector3D& offset) const { return translatedPoints(offset,{10,11},false); }

EntityGeometry DXFLine::geometry() const {
    // LINE endpoints are WCS, even when an extrusion vector is present.
    return {{{point(record_, 10), point(record_, 11), std::nullopt}}, {}, {}};
}

} // namespace DXFCAD::Detail
