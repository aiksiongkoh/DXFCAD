#include <DXFCAD/EntityConversion.h>
#include <DXFCAD/DXFPolyline.h>
#include <DXFCAD/ParametricCurves.h>
#include <DXFCAD/Drafting.h>
#include <DXFCAD/CartesianFrame.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace DXFCAD {
namespace {
constexpr double pi=3.14159265358979323846;
Vector3D sub(Vector3D a,Vector3D b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
double norm(Vector3D a) { return std::hypot(a.x,a.y,a.z); }
double dot(Vector3D a,Vector3D b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
std::string attributes(const std::string& generated,const DXFEntity& source)
{
    const auto compound=dynamic_cast<const DXFPolyline*>(&source);
    const auto& original=compound ? compound->headerRecord() : source.record();
    const auto f=nativeEntityFields(original);
    std::map<int,std::string> properties{{8,source.layer()}};
    for (const auto code : {6,39,48,60,62,370,420,430,440})
        if (!f.value(code).empty()) properties[code]=f.value(code);
    auto record=readDXFGroups(patchEntityStringFields(readDXFGroups(generated),properties));
    // Application data remains authoritative across type conversion; identity
    // and reactors belong to the old object, so do not copy those bindings.
    bool xdata=false;
    for (const auto& group : original.groups) {
        if (group.first==1001) xdata=true;
        if (xdata || group.first==999) record.groups.push_back(group);
    }
    return record.raw();
}
std::string arc(const LWPolylineSegment3D& segment)
{
    const auto& a=*segment.arc;
    const CartesianFrame frame({a.normal.x,a.normal.y,a.normal.z});
    const auto c=frame.fromWorld(a.center);
    auto start=frame.fromWorld(segment.start),end=frame.fromWorld(segment.end);
    if (a.sweep<0) std::swap(start,end);
    const auto degrees=[&](Vector3D p) { auto result=std::atan2(p.y-c.y,p.x-c.x)*180/pi; return result<0 ? result+360 : result; };
    GroupSequence record{{{0,"ARC"},{100,"AcDbEntity"},{8,"0"},{100,"AcDbCircle"},
        {10,"0"},{20,"0"},{30,"0"},{40,"1"},{210,"0"},{220,"0"},{230,"1"},
        {100,"AcDbArc"},{50,"0"},{51,"90"}}};
    return patchEntityFields(record,{{10,c.x},{20,c.y},{30,c.z},{40,a.radius},
        {210,a.normal.x},{220,a.normal.y},{230,a.normal.z},{50,degrees(start)},{51,degrees(end)}});
}
std::string transform(const DXFEntity& entity,const BlockPart& part)
{
    if (entity.type()=="ELLIPSE" || entity.type()=="SPLINE") {
        const auto native=affineTransformedCurve(entity,part);
        return parseEntity(native)->copied({0,0,0});
    }
    if (entity.type()=="LINE") {
        const auto s=entity.geometry().segments.at(0);
        return attributes(createLine(part.transformPoint(s.start),part.transformPoint(s.end)),entity);
    }
    // Existing native transformations retain entity semantics/metadata for
    // planar similarities, including reflection and nested block composition.
    const double x=norm(part.xAxis),y=norm(part.yAxis);
    const double tolerance=1e-9*std::max({1.0,x,y});
    if (x==0 || std::abs(x-y)>tolerance || std::abs(dot(part.xAxis,part.yAxis))>tolerance*x ||
        std::abs(part.xAxis.z)>tolerance || std::abs(part.yAxis.z)>tolerance) {
        // Circular curves under a general affine block transform are exact
        // ellipses. Do not approximate them or retain a false circle radius.
        if (entity.type()=="CIRCLE" || entity.type()=="ARC") {
            Vector3D center,major,normal; double sweep=2*pi;
            const auto geometry=entity.geometry();
            if (entity.type()=="CIRCLE") {
                const auto circle=geometry.circles.at(0); center=circle.center; normal=circle.normal;
                major=CartesianFrame({normal.x,normal.y,normal.z}).toWorld({circle.radius,0},0);
            } else {
                const auto curve=geometry.segments.at(0); center=curve.arc->center;
                normal=curve.arc->normal; major=sub(curve.start,center); sweep=curve.arc->sweep;
            }
            auto raw=patchEntityFields(readDXFGroups("0\nELLIPSE\n8\n0\n10\n0\n20\n0\n30\n0\n11\n1\n21\n0\n31\n0\n40\n1\n41\n0\n42\n1\n210\n0\n220\n0\n230\n1\n"),
                {{10,center.x},{20,center.y},{30,center.z},{11,major.x},{21,major.y},{31,major.z},
                 {42,sweep},{210,normal.x},{220,normal.y},{230,normal.z}});
            raw=attributes(raw,entity);
            return affineTransformedCurve(*parseEntity(raw),part);
        }
        throw std::invalid_argument("Native block polyline/text conversion requires a planar similarity transform");
    }
    std::string raw=entity.copied({0,0,0});
    if (part.xAxis.x*part.yAxis.y-part.xAxis.y*part.yAxis.x<0) raw=parseEntity(raw)->mirrored(0,{0,0,0});
    raw=parseEntity(raw)->scaled(x,{0,0,0});
    raw=parseEntity(raw)->rotated(std::atan2(part.xAxis.y,part.xAxis.x)*180/pi,{0,0,0});
    return parseEntity(raw)->translated(part.origin);
}
}
std::vector<std::string> EntityConversion::explode(const DXFEntity& entity)
{
    std::vector<std::string> result;
    if (entity.type()=="LWPOLYLINE" || entity.type()=="POLYLINE") {
        for (const auto& s : entity.geometry().segments) result.push_back(attributes(s.arc ? arc(s) : createLine(s.start,s.end),entity));
    } else if (entity.type()=="INSERT") {
        // blockParts resolves nested INSERTs and array placements through the
        // document's existing native block context, never display tessellation.
        for (const auto& part : entity.blockParts()) {
            if (!part.visible) continue;
            auto raw=transform(*part.entity,part);
            const auto fields=nativeEntityFields(part.entity->record());
            std::map<int,std::string> values;
            if (part.entity->layer()=="0") values[8]=entity.layer();
            if (fields.integer(62,256)==0) {
                const auto root=nativeEntityFields(entity.record());
                values[62]=root.value(62,"256");
                if (!root.value(420).empty()) values[420]=root.value(420);
            }
            result.push_back(patchEntityStringFields(readDXFGroups(raw),values));
        }
    } else throw std::invalid_argument("Native explode requires an INSERT, LWPOLYLINE or POLYLINE");
    return result;
}
std::string EntityConversion::circleAsArc(const std::string& raw)
{
    const auto circle=parseEntity(raw);
    if (circle->type()!="CIRCLE") throw std::invalid_argument("Expected a native CIRCLE");
    circle->geometry();
    auto record=readDXFGroups(circle->copied({0,0,0}));
    for (auto& group : record.groups) if (group.first==0) { group.second="ARC"; break; }
    // AcDbArc precedes application XDATA, rather than being appended inside it.
    auto at=record.groups.begin();
    while (at!=record.groups.end() && at->first<1000) ++at;
    record.groups.insert(at,{{100,"AcDbArc"},{50,"0"},{51,"360"}});
    return record.raw();
}
}
