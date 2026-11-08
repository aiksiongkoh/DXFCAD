#pragma once

#include <DXFCAD/GroupSequence.h>
#include <DXFCAD/Vector3D.h>
#include <cmath>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace DXFCAD::Detail {

inline double numeric(const std::string& s)
{
    std::istringstream input(s);
    input.imbue(std::locale::classic());
    double value;
    if (!(input >> value) || !std::isfinite(value)) throw std::invalid_argument("Invalid DXF number");
    input >> std::ws;
    if (!input.eof()) throw std::invalid_argument("Trailing data in DXF number");
    return value;
}

inline Vector3D extrusion(const GroupSequence& r)
{
    return {r.number(210, 0), r.number(220, 0), r.number(230, 1)};
}
inline Vector3D point(const GroupSequence& r, int code)
{
    if (r.value(code).empty() || r.value(code+10).empty()) throw std::invalid_argument("DXF point lacks X/Y");
    return {r.number(code), r.number(code+10), r.number(code+20)};
}
inline double radius(const GroupSequence& r)
{
    const double value = r.number(40);
    if (value <= 0) throw std::invalid_argument("DXF radius must be positive");
    return value;
}
} // namespace DXFCAD::Detail
