#include <DXFCAD/DXFLeader.h>
#include <DXFCAD/CartesianFrame.h>
#include <DXFCAD/Drafting.h>
#include "EntityCoordinates.h"
#include <cmath>
#include <stdexcept>

namespace DXFCAD {

DXFLeader::DXFLeader(GroupSequence record, std::string context)
    : DXFEntity(std::move(record)), context_(std::move(context)) {}

std::vector<Vector3D> DXFLeader::vertices() const
{
    const auto fields=nativeEntityFields(record_);
    std::vector<Vector3D> result;
    for (std::size_t i=0; i<fields.groups.size(); ++i) {
        if (fields.groups[i].first!=10) continue;
        GroupSequence vertex;
        vertex.groups.push_back(fields.groups[i]);
        while (i+1<fields.groups.size() && (fields.groups[i+1].first==20 || fields.groups[i+1].first==30))
            vertex.groups.push_back(fields.groups[++i]);
        result.push_back(Detail::point(vertex,10));
    }
    if (result.size()<2) throw std::invalid_argument("LEADER requires at least two vertices");
    if (!fields.value(76).empty() && fields.integer(76)!=static_cast<int>(result.size()))
        throw std::invalid_argument("LEADER vertex count does not match its coordinates");
    return result;
}

std::string DXFLeader::translated(const Vector3D&) const
{
    // Repeated vertices and annotation/style dependencies require a dedicated
    // edit contract. Reject before the generic single-point transformations.
    throw std::invalid_argument("LEADER editing is not supported yet; retain its native record");
}

int DXFLeader::colorIndex() const
{
    const auto fields=nativeEntityFields(record_);
    if (context_.empty()) return fields.integer(62,256);
    const int color=nativeEntityFields(readDXFGroups(context_)).integer(176,0);
    return color==0 ? fields.integer(77,7) : color;
}

EntityGeometry DXFLeader::geometry() const
{
    const auto fields=nativeEntityFields(record_);
    if (fields.integer(72)!=0) throw std::invalid_argument("Spline LEADER paths are not supported yet");
    const int arrow=fields.integer(71,1), annotation=fields.integer(73,3);
    if (arrow<0 || arrow>1 || annotation<0 || annotation>3)
        throw std::invalid_argument("Invalid LEADER flags");
    for (int code : {74,75}) if (fields.integer(code,1)<0 || fields.integer(code,1)>1)
        throw std::invalid_argument("Invalid LEADER hookline flags");
    if (annotation!=3 || (fields.value(340)!="" && fields.value(340)!="0"))
        throw std::invalid_argument("Associated LEADER annotations are not supported yet");
    std::string application;
    for (const auto& [code,value] : record_.groups) {
        if (code==1001) application=value;
        if (application=="ACAD" && code==1000 && value=="DSTYLE")
            throw std::invalid_argument("LEADER style overrides are not supported yet");
    }
    const auto points=vertices();
    const auto normal=CartesianFrame(Detail::extrusion(fields)).normal();
    EntityGeometry result;
    for (std::size_t i=1; i<points.size(); ++i) {
        const auto a=points[i-1], b=points[i];
        if (std::hypot(b.x-a.x,b.y-a.y,b.z-a.z)==0)
            throw std::invalid_argument("LEADER has coincident consecutive vertices");
        result.segments.push_back({a,b,{}});
    }
    if (!arrow) return result;
    const auto style=nativeEntityFields(readDXFGroups(context_));
    if (style.value(0)!="DIMSTYLE" || style.value(2)!=fields.value(3,"Standard"))
        throw std::invalid_argument("LEADER dimension style is missing or mismatched");
    if ((!style.value(341).empty() && style.value(341)!="0") || !style.value(5).empty())
        throw std::invalid_argument("Custom LEADER arrowhead blocks are not supported yet");
    double scale=style.number(40,1);
    if (scale==0) scale=1; // Annotative/automatic DIMSCALE: retained WCS vertices.
    const double size=style.number(41)*scale;
    if (!std::isfinite(size) || size<=0) throw std::invalid_argument("Invalid LEADER arrow size");
    const auto tip=points[0], next=points[1];
    const double length=std::hypot(next.x-tip.x,next.y-tip.y,next.z-tip.z);
    const Vector3D axis{(next.x-tip.x)/length,(next.y-tip.y)/length,(next.z-tip.z)/length};
    if (std::abs(axis.x*normal.x+axis.y*normal.y+axis.z*normal.z)>1e-7)
        throw std::invalid_argument("LEADER arrow direction is outside its extrusion plane");
    const Vector3D side{normal.y*axis.z-normal.z*axis.y,normal.z*axis.x-normal.x*axis.z,normal.x*axis.y-normal.y*axis.x};
    const Vector3D base{tip.x+size*axis.x,tip.y+size*axis.y,tip.z+size*axis.z};
    result.faces.push_back({tip,{base.x+size*side.x/6,base.y+size*side.y/6,base.z+size*side.z/6},
        {base.x-size*side.x/6,base.y-size*side.y/6,base.z-size*side.z/6}});
    return result;
}

} // namespace DXFCAD
