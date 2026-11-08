#include "DXFDimension.h"
#include "DXFLine.h"
#include "DXFCircle.h"
#include "DXFArc.h"
#include "DXFText.h"
#include "DXFMText.h"
#include "EntityCoordinates.h"
#include <DXFCAD/CartesianFrame.h>
#include <DXFCAD/Drafting.h>

#include <cmath>
#include <stdexcept>
#include <utility>

namespace DXFCAD::Detail {

DXFDimension::DXFDimension(GroupSequence record, std::string context)
    : DXFEntity(std::move(record)), context_(std::move(context)) {}

std::string DXFDimension::context() const { return context_; }

EntityGeometry DXFDimension::geometry() const {
    const auto fields = nativeEntityFields(record_);
    const int kind = fields.integer(70)&7;
    if (kind == 7) throw std::invalid_argument("Invalid DIMENSION type");
    const auto normal = CartesianFrame(extrusion(fields)).normal();
    if (normal.x != 0 || normal.y != 0 || normal.z != 1 || fields.number(51)
        || fields.number(12) || fields.number(22) || fields.number(32))
        throw std::invalid_argument("Transformed DIMENSION pictures are not supported yet");
    // The cached BLOCK supplies the picture for every native dimension kind;
    // its definition points differ by kind and must not be inferred from a
    // linear dimension's fields (radial dimensions, for example, use 15).
    for (int code : {10, 11}) point(fields, code);
    if (kind == 0 || kind == 1 || kind == 2 || kind == 5 || kind == 6)
        for (int code : {13, 14}) point(fields, code);
    if (kind == 2 || kind == 3 || kind == 4 || kind == 5) point(fields, 15);
    if (kind == 2) point(fields, 16);
    const auto data = readDXFGroups(context_);
    std::vector<GroupSequence> records;
    for (const auto& g : data.groups) {
        if (g.first == 0) records.emplace_back();
        if (records.empty()) throw std::invalid_argument("Invalid DIMENSION picture context");
        records.back().groups.push_back(g);
    }
    if (records.size() < 3 || records.front().value(0) != "BLOCK" || records.back().value(0) != "ENDBLK"
        || fields.value(2).empty() || records.front().value(2) != fields.value(2))
        throw std::invalid_argument("DIMENSION picture block is missing or mismatched");
    const auto header = nativeEntityFields(records.front());
    if ((header.integer(70)&(4|8|16)) || header.number(10) || header.number(20) || header.number(30))
        throw std::invalid_argument("External or displaced DIMENSION picture blocks are not supported");
    EntityGeometry result;
    for (std::size_t i = 1; i+1 < records.size(); ++i) {
        auto child = nativeEntityFields(records[i]);
        const auto type = child.value(0);
        if (type == "POINT") continue; // Dimension definition markers are nonprinting.
        if (child.integer(60)) continue;
        if (child.integer(62, 256) != 0 || child.integer(420, -1) >= 0)
            throw std::invalid_argument("DIMENSION picture children must inherit the dimension color (BYBLOCK)");
        if (type == "SOLID") {
            if (child.number(39)) throw std::invalid_argument("Thick DIMENSION arrowheads are not supported");
            const auto a = point(child, 10), b = point(child, 11), c = point(child, 12), d = point(child, 13);
            if (c.x != d.x || c.y != d.y || c.z != d.z)
                throw std::invalid_argument("Only triangular DIMENSION arrowheads are supported");
            const Vector3D u{b.x-a.x,b.y-a.y,b.z-a.z}, v{c.x-a.x,c.y-a.y,c.z-a.z};
            const double area = std::hypot(u.y*v.z-u.z*v.y, u.z*v.x-u.x*v.z, u.x*v.y-u.y*v.x);
            if (!std::isfinite(area) || area == 0) throw std::invalid_argument("Degenerate DIMENSION arrowhead");
            const CartesianFrame frame(extrusion(child));
            result.faces.push_back({frame.toWorld({a.x,a.y},a.z), frame.toWorld({b.x,b.y},b.z), frame.toWorld({c.x,c.y},c.z)});
            continue;
        }
        EntityGeometry geometry;
        if (type == "LINE") geometry = DXFLine(child).geometry();
        else if (type == "ARC") geometry = DXFArc(child).geometry();
        else if (type == "CIRCLE") geometry = DXFCircle(child).geometry();
        else if (type == "TEXT") geometry = DXFText(child).geometry();
        else if (type == "MTEXT") {
            // A prefix alignment command has no effect on a single plain
            // text run. Decode the remaining content through MTEXT itself.
            for (auto& [code, value] : child.groups) if ((code == 1 || code == 3)
                && value.size() >= 4 && value.compare(0,2,"\\A") == 0
                && value[2] >= '0' && value[2] <= '2' && value[3] == ';') value.erase(0,4);
            geometry = DXFMText(child).geometry();
            if (geometry.text->text.find('\n') != std::string::npos)
                throw std::invalid_argument("Multiline DIMENSION picture text is not supported yet");
        } else throw std::invalid_argument("Unsupported DIMENSION picture entity "+type);
        result.segments.insert(result.segments.end(), geometry.segments.begin(), geometry.segments.end());
        result.circles.insert(result.circles.end(), geometry.circles.begin(), geometry.circles.end());
        if (geometry.text) result.texts.push_back(*geometry.text);
    }
    if (result.segments.empty() && result.circles.empty() && result.faces.empty() && result.texts.empty())
        throw std::invalid_argument("DIMENSION picture is empty");
    return result;
}

} // namespace DXFCAD::Detail
