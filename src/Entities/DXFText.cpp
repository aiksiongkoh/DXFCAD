#include "DXFText.h"
#include "EntityCoordinates.h"
#include <DXFCAD/CartesianFrame.h>
#include <DXFCAD/Drafting.h>

#include <cmath>
#include <stdexcept>
#include <utility>

namespace DXFCAD::Detail {

std::string DXFText::translated(const Vector3D& offset) const {
    const auto fields = nativeEntityFields(record_);
    return translatedPoints(offset,fields.value(11).empty() ? std::vector<int>{10} : std::vector<int>{10,11},true);
}

EntityGeometry DXFText::geometry() const {
    const auto fields = nativeEntityFields(record_);
    const int horizontal = fields.integer(72), vertical = fields.integer(73);
    if (horizontal < 0 || horizontal > 2 || vertical < 0 || vertical > 3) {
        throw std::invalid_argument("TEXT aligned/fit/middle modes or invalid justification are not supported yet");
    }
    if (fields.integer(71) || fields.number(51)) {
        throw std::invalid_argument("TEXT mirroring or oblique style is not supported yet");
    }
    const double height = fields.number(40), width = fields.number(41, 1);
    if (height <= 0 || width <= 0) throw std::invalid_argument("TEXT height/width must be positive");
    const CartesianFrame frame(extrusion(fields));
    // DXF 11 is the anchor for any justification other than baseline/left.
    const auto origin = point(fields, horizontal || vertical ? 11 : 10);
    const double angle = fields.number(50)*std::acos(-1.0)/180;
    const auto x = frame.xAxis(), y = frame.yAxis();
    const double c = std::cos(angle), s = std::sin(angle);
    return {{}, {}, TextGeometry{fields.value(1), frame.toWorld({origin.x, origin.y}, origin.z),
        {c*x.x+s*y.x, c*x.y+s*y.y, c*x.z+s*y.z},
        {-s*x.x+c*y.x, -s*x.y+c*y.y, -s*x.z+c*y.z}, frame.normal(), height, width, horizontal, vertical}};
}

} // namespace DXFCAD::Detail
