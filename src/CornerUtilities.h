#pragma once

#include <DXFCAD/Drafting.h>
#include "PlanarCurves.h"

namespace DXFCAD::Detail {
inline Vector3D minus(Vector3D a,Vector3D b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vector3D plus(Vector3D a,Vector3D b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vector3D times(Vector3D a,double t) { return {a.x*t,a.y*t,a.z*t}; }
inline Vector3D cross3(Vector3D a,Vector3D b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline double dot3(Vector3D a,Vector3D b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline double size(Vector3D p) { return std::hypot(p.x,p.y,p.z); }
inline Vector3D unit(Vector3D p) {
    const double length=size(p);
    if (!std::isfinite(length) || length<=tolerance)
        throw std::invalid_argument("Corner editing needs finite nonzero lines and retained rays");
    return times(p,1/length);
}
inline std::string endpoint(const DXFEntity& entity,bool keepStart,Vector3D p) {
    const int code=keepStart ? 11 : 10;
    const auto raw=patchEntityFields(entity.record(),{{code,p.x},{code+10,p.y},{code+20,p.z}});
    parseEntity(raw)->geometry();
    return raw;
}

} // namespace DXFCAD::Detail
