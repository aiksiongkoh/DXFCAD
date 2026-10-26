#include <DXFCAD/LegacyPolyline.h>
#include <DXFCAD/Drafting.h>
#include <DXFCAD/OCSFrame.h>
#include <cmath>
#include <stdexcept>

namespace DXFCAD {
LegacyPolyline::LegacyPolyline(DXFRecord record) : DXFEntity(std::move(record))
{
    for (const auto& group : record_.groups) {
        if (group.first==0) sequence_.push_back({});
        if (sequence_.empty()) throw std::invalid_argument("POLYLINE lacks a header");
        sequence_.back().groups.push_back(group);
    }
    if (sequence_.size()<2 || sequence_.front().value(0)!="POLYLINE" || sequence_.back().value(0)!="SEQEND")
        throw std::invalid_argument("POLYLINE lacks SEQEND");
    const auto head=nativeEntityFields(headerRecord());
    if (head.integer(70)&(2|4|16|64)) throw std::invalid_argument("Fitted, mesh and polyface POLYLINEs require their native curve/surface models");
    for (std::size_t i=1;i+1<sequence_.size();++i) {
        const auto fields=nativeEntityFields(sequence_[i]);
        if (fields.value(0)!="VERTEX" || (fields.integer(70)&(1|2|8|16|64|128)))
            throw std::invalid_argument("Unsupported POLYLINE vertex");
        if (fields.value(10).empty() || fields.value(20).empty()) throw std::invalid_argument("POLYLINE vertex lacks X/Y");
    }
    geometry();
}
bool LegacyPolyline::threeDimensional() const { return (nativeEntityFields(headerRecord()).integer(70)&8)!=0; }
LWPolylineEntity LegacyPolyline::planarPolyline() const
{
    const auto head=nativeEntityFields(headerRecord());
    std::vector<LWPolylineVertex> vertices;
    for (std::size_t i=1;i+1<sequence_.size();++i) {
        const auto fields=nativeEntityFields(sequence_[i]);
        LWPolylineVertex vertex{fields.number(10),fields.number(20),fields.number(42)};
        const auto start=fields.number(40,head.number(40)),end=fields.number(41,head.number(41));
        if (start!=0 || end!=0) { vertex.hasWidths=true; vertex.startWidth=start; vertex.endWidth=end; }
        vertices.push_back(vertex);
    }
    return LWPolylineEntity(std::move(vertices),(head.integer(70)&1)!=0,0,head.number(30),
        {head.number(210),head.number(220),head.number(230,1)});
}
EntityGeometry LegacyPolyline::geometry() const
{
    EntityGeometry result;
    if (!threeDimensional()) { result.segments=planarPolyline().worldSegments(); return result; }
    std::vector<Vector3D> points;
    for (std::size_t i=1;i+1<sequence_.size();++i) {
        const auto fields=nativeEntityFields(sequence_[i]);
        if (fields.number(42)!=0) throw std::invalid_argument("A 3D POLYLINE cannot have planar bulges");
        points.push_back({fields.number(10),fields.number(20),fields.number(30)});
    }
    if (points.size()<2) return result;
    const auto count=(nativeEntityFields(headerRecord()).integer(70)&1) ? points.size() : points.size()-1;
    for (std::size_t i=0;i<count;++i) result.segments.push_back({points[i],points[(i+1)%points.size()],{}});
    return result;
}
std::vector<LWPolylineWidthRegion> LegacyPolyline::widthRegions(double tolerance) const
{
    return threeDimensional() ? std::vector<LWPolylineWidthRegion>{} : planarPolyline().widthRegions(tolerance);
}
std::string LegacyPolyline::translated(const Vector3D& offset) const
{
    if (!std::isfinite(offset.x) || !std::isfinite(offset.y) || !std::isfinite(offset.z)) throw std::invalid_argument("Move displacement must be finite");
    const auto head=nativeEntityFields(headerRecord());
    const auto delta=threeDimensional() ? offset : OCSFrame({head.number(210),head.number(220),head.number(230,1)}).fromWorld(offset);
    DXFRecord moved;
    for (std::size_t i=0;i<sequence_.size();++i) {
        auto record=sequence_[i];
        if (i==0 && !threeDimensional()) record=readDXFGroups(patchEntityFields(record,{{30,head.number(30)+delta.z}}));
        if (i>0 && i+1<sequence_.size()) {
            const auto fields=nativeEntityFields(record);
            std::map<int,double> values{{10,fields.number(10)+delta.x},{20,fields.number(20)+delta.y}};
            if (threeDimensional()) values[30]=fields.number(30)+delta.z;
            record=readDXFGroups(patchEntityFields(record,values));
        }
        moved.groups.insert(moved.groups.end(),record.groups.begin(),record.groups.end());
    }
    return moved.raw();
}
}
