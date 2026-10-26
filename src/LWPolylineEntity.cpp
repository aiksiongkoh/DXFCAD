#include <DXFCAD/LWPolylineEntity.h>
#include <DXFCAD/Drafting.h>
#include <DXFCAD/SnapGeometry.h>

#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>

namespace DXFCAD {

LWPolylineEntity::LWPolylineEntity() : LWPolylineEntity(std::vector<LWPolylineVertex>{}) {}

LWPolylineEntity::LWPolylineEntity(DXFRecord record) : DXFEntity(std::move(record))
{
    if (record_.groups.empty() || record_.groups.front() != std::make_pair(0, std::string("LWPOLYLINE")))
        throw std::invalid_argument("Expected LWPOLYLINE data");
    for (std::size_t i = 1; i < record_.groups.size(); ++i)
        if (record_.groups[i].first == 0) throw std::invalid_argument("Expected exactly one DXF entity");
    const auto fields = nativeEntityFields(record_);
    bool hasY = true;
    for (const auto& [code, value] : fields.groups) {
        if (code == 10) {
            if (!hasY) throw std::invalid_argument("LWPolyline vertex lacks Y");
            vertices_.push_back({DXFRecord{{{code, value}}}.number(code), 0});
            hasY = false;
        } else if (code == 20 || code == 40 || code == 41 || code == 42) {
            if (vertices_.empty()) throw std::invalid_argument("LWPolyline vertex data precedes X");
            auto& vertex = vertices_.back();
            const double number = DXFRecord{{{code, value}}}.number(code);
            if (code == 20) { vertex.y = number; hasY = true; }
            if (code == 40) { vertex.startWidth = number; vertex.hasWidths = true; }
            if (code == 41) { vertex.endWidth = number; vertex.hasWidths = true; }
            if (code == 42) vertex.bulge = number;
        }
    }
    if (!hasY || fields.integer(90, -1) != static_cast<int>(vertices_.size()))
        throw std::invalid_argument("LWPolyline vertex count/coordinates are incomplete");
    closed_ = (fields.integer(70) & 1) != 0;
    constantWidth_ = fields.number(43);
    elevation_ = fields.number(38);
    extrusion_ = {fields.number(210), fields.number(220), fields.number(230, 1)};
    validate();
}

LWPolylineEntity LWPolylineEntity::fromRaw(const std::string& raw)
{
    return LWPolylineEntity(readDXFGroups(raw));
}

EntityGeometry LWPolylineEntity::geometry() const
{
    EntityGeometry result;
    result.segments = worldSegments();
    return result;
}

std::vector<DXFSnapPoint> LWPolylineEntity::snapPoints() const
{
    std::vector<DXFSnapPoint> result;
    for (const auto& point : worldVertices()) result.push_back({point, "vertex"});
    const auto midpoints = SnapGeometry::midpoints(geometry());
    result.insert(result.end(), midpoints.begin(), midpoints.end());
    return result;
}

std::string LWPolylineEntity::translated(const Vector3D& offset) const
{
    const auto result = moved(offset);
    result.geometry();
    return result.raw();
}

LWPolylineEntity LWPolylineEntity::withGeometry(const LWPolylineEntity& geometry,
                                               const std::vector<int>& origins) const
{
    return LWPolylineEntity(geometryRecord(geometry, origins));
}

DXFRecord LWPolylineEntity::geometryRecord(const LWPolylineEntity& polyline,
                                          const std::vector<int>& origins) const
{
    const auto fields = nativeEntityFields(record_);
    std::vector<std::string> ids(vertices_.size());
    std::size_t vertex = 0;
    bool seenVertex = false;
    for (const auto& group : fields.groups) {
        if (group.first == 10) { if (seenVertex) ++vertex; seenVertex = true; }
        if (group.first == 91 && seenVertex) ids.at(vertex) = group.second;
    }
    if (!origins.empty() && origins.size() != polyline.vertices().size())
        throw std::invalid_argument("Vertex origins must match the new vertex count");
    // No identity mapping is needed when every vertex is removed.
    if (origins.empty() && !polyline.vertices().empty() && vertices_.size() != polyline.vertices().size())
        throw std::invalid_argument("Supply vertex origins when changing vertex count");
    std::set<int> used;
    for (int origin : origins) {
        if (origin < -1 || origin >= static_cast<int>(ids.size())
            || (origin >= 0 && !used.insert(origin).second))
            throw std::invalid_argument("Invalid or repeated vertex origin");
    }
    auto number = [](double value) {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
        return out.str();
    };
    DXFRecord geometry;
    auto add = [&](int code, double value) { geometry.groups.emplace_back(code, number(value)); };
    geometry.groups.emplace_back(90, std::to_string(polyline.vertices().size()));
    geometry.groups.emplace_back(70, std::to_string((fields.integer(70)&~1) | (polyline.closed() ? 1 : 0)));
    add(43, polyline.constantWidth()); add(38, polyline.elevation());
    std::size_t index = 0;
    for (const auto& item : polyline.vertices()) {
        add(10, item.x); add(20, item.y);
        if (item.hasWidths || item.startWidth != 0 || item.endWidth != 0) {
            add(40, item.startWidth); add(41, item.endWidth);
        }
        add(42, item.bulge);
        const int origin = origins.empty() ? static_cast<int>(index) : origins[index];
        if (origin >= 0 && !ids[origin].empty()) geometry.groups.emplace_back(91, ids[origin]);
        ++index;
    }
    add(210, polyline.extrusion().x); add(220, polyline.extrusion().y); add(230, polyline.extrusion().z);
    const std::set<int> replaced{90,70,43,38,10,20,40,41,42,91,210,220,230};
    DXFRecord result;
    bool inserted = false, xdata = false;
    int depth = 0;
    auto insert = [&] {
        if (!inserted) result.groups.insert(result.groups.end(), geometry.groups.begin(), geometry.groups.end());
        inserted = true;
    };
    // Construction from values starts a new, identity-free record.
    if (record_.groups.empty()) {
        result.groups = {{0,"LWPOLYLINE"},{100,"AcDbEntity"},{8,"0"},{100,"AcDbPolyline"}};
    }
    for (const auto& group : record_.groups) {
        if (group.first == 1001 && !depth) { insert(); xdata = true; }
        if (group.first == 102 && !xdata) {
            if (!group.second.empty() && group.second.front() == '{') ++depth;
            else if (group.second == "}" && depth) --depth;
            result.groups.push_back(group);
        } else if (!xdata && !depth && replaced.count(group.first)) insert();
        else result.groups.push_back(group);
    }
    insert();
    return result;
}

} // namespace DXFCAD
