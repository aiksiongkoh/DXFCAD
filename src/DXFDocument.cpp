#include <DXFCAD/DXFDocument.h>
#include <DXFCAD/OCSFrame.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace DXFCAD {
namespace {
std::string trim(const std::string& s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? "" : s.substr(first, s.find_last_not_of(" \t\r\n")-first+1);
}
double numeric(const std::string& s)
{
    std::istringstream input(s);
    input.imbue(std::locale::classic());
    double value;
    if (!(input >> value) || !std::isfinite(value)) throw std::invalid_argument("Invalid DXF number");
    input >> std::ws;
    if (!input.eof()) throw std::invalid_argument("Trailing data in DXF number");
    return value;
}
int integral(const std::string& s)
{
    const double value = numeric(s);
    if (value != std::trunc(value) || value < std::numeric_limits<int>::min()
        || value > std::numeric_limits<int>::max()) throw std::invalid_argument("Invalid DXF integer");
    return static_cast<int>(value);
}
DXFRecord groups(const std::string& ascii)
{
    if (ascii.find("AutoCAD Binary DXF") == 0) throw std::invalid_argument("Binary DXF is not supported");
    std::istringstream input(ascii);
    std::string code, value;
    DXFRecord result;
    while (std::getline(input, code)) {
        if (result.groups.empty() && code.compare(0, 3, "\xef\xbb\xbf") == 0) code.erase(0, 3);
        if (!std::getline(input, value)) throw std::invalid_argument("DXF group code has no value");
        if (!value.empty() && value.back() == '\r') value.pop_back();
        const int group = integral(code);
        if (group < 0 || group > 1071) throw std::invalid_argument("Invalid DXF group code");
        result.groups.emplace_back(group, value);
    }
    return result;
}
ExtrusionDirection extrusion(const DXFRecord& r)
{
    return {r.number(210, 0), r.number(220, 0), r.number(230, 1)};
}
Vector3D point(const DXFRecord& r, int code)
{
    if (r.value(code).empty() || r.value(code+10).empty()) throw std::invalid_argument("DXF point lacks X/Y");
    return {r.number(code), r.number(code+10), r.number(code+20)};
}
double radius(const DXFRecord& r)
{
    const double value = r.number(40);
    if (value <= 0) throw std::invalid_argument("DXF radius must be positive");
    return value;
}
LWPolyline polyline(const DXFRecord& record)
{
    std::vector<LWPolylineVertex> vertices;
    bool hasY = true;
    for (const auto& [code, value] : record.groups) {
        if (code == 10) {
            if (!hasY) throw std::invalid_argument("LWPolyline vertex lacks Y");
            vertices.push_back({numeric(value), 0}); hasY = false;
        } else if (code == 20 || code == 40 || code == 41 || code == 42) {
            if (vertices.empty()) throw std::invalid_argument("LWPolyline vertex data precedes X");
            auto& v = vertices.back();
            const double n = numeric(value);
            if (code == 20) { v.y = n; hasY = true; }
            if (code == 40) { v.startWidth = n; v.hasWidths = true; }
            if (code == 41) { v.endWidth = n; v.hasWidths = true; }
            if (code == 42) v.bulge = n;
        }
    }
    if (!hasY || record.integer(90, -1) != static_cast<int>(vertices.size())) {
        throw std::invalid_argument("LWPolyline vertex count/coordinates are incomplete");
    }
    return LWPolyline(std::move(vertices), (record.integer(70)&1) != 0,
                      record.number(43), record.number(38), extrusion(record));
}
class PolylineEntity final : public DXFEntity {
public:
    using DXFEntity::DXFEntity;
    EntityGeometry geometry() const override { return {polyline(record_).worldSegments(), {}, {}}; }
    std::vector<LWPolylineWidthRegion> widthRegions(double tolerance) const override {
        return polyline(record_).widthRegions(tolerance);
    }
};
class LineEntity final : public DXFEntity {
public:
    using DXFEntity::DXFEntity;
    EntityGeometry geometry() const override {
        // LINE endpoints are WCS, even when an extrusion vector is present.
        return {{{point(record_, 10), point(record_, 11), std::nullopt}}, {}, {}};
    }
};
class CircleEntity final : public DXFEntity {
public:
    using DXFEntity::DXFEntity;
    EntityGeometry geometry() const override {
        const auto center = point(record_, 10);
        const OCSFrame frame(extrusion(record_));
        return {{}, {{frame.toWorld({center.x, center.y}, center.z), frame.normal(), radius(record_)}}, {}};
    }
};
class ArcEntity final : public DXFEntity {
public:
    using DXFEntity::DXFEntity;
    EntityGeometry geometry() const override {
        const auto center = point(record_, 10);
        const OCSFrame frame(extrusion(record_));
        const double r = radius(record_), pi = std::acos(-1.0);
        const double start = record_.number(50)*pi/180.0;
        double degrees = std::fmod(record_.number(51)-record_.number(50), 360.0);
        if (degrees <= 0) degrees += 360;
        if (degrees == 360) throw std::invalid_argument("ARC with equal angles is ambiguous; use CIRCLE");
        const double sweep = degrees*pi/180.0;
        auto at = [&](double a) { return frame.toWorld({center.x+r*std::cos(a), center.y+r*std::sin(a)}, center.z); };
        const LWPolylineArc3D arc{frame.toWorld({center.x, center.y}, center.z),
                                 at(start+sweep/2), frame.normal(), r, sweep};
        return {{{at(start), at(start+sweep), arc}}, {}, {}};
    }
};
class TextEntity final : public DXFEntity {
public:
    using DXFEntity::DXFEntity;
    EntityGeometry geometry() const override {
        if (record_.integer(72) || record_.integer(73) || record_.integer(71) || record_.number(51)) {
            throw std::invalid_argument("TEXT alignment, mirroring, or oblique style is not supported yet");
        }
        const double height = record_.number(40), width = record_.number(41, 1);
        if (height <= 0 || width <= 0) throw std::invalid_argument("TEXT height/width must be positive");
        const OCSFrame frame(extrusion(record_));
        const auto origin = point(record_, 10);
        const double angle = record_.number(50)*std::acos(-1.0)/180;
        const auto x = frame.xAxis(), y = frame.yAxis();
        const double c = std::cos(angle), s = std::sin(angle);
        return {{}, {}, TextGeometry{record_.value(1), frame.toWorld({origin.x, origin.y}, origin.z),
            {c*x.x+s*y.x, c*x.y+s*y.y, c*x.z+s*y.z},
            {-s*x.x+c*y.x, -s*x.y+c*y.y, -s*x.z+c*y.z}, frame.normal(), height, width}};
    }
};
std::shared_ptr<DXFEntity> entity(DXFRecord record)
{
    const auto type = record.value(0);
    if (type == "LWPOLYLINE") return std::make_shared<PolylineEntity>(std::move(record));
    if (type == "LINE") return std::make_shared<LineEntity>(std::move(record));
    if (type == "ARC") return std::make_shared<ArcEntity>(std::move(record));
    if (type == "CIRCLE") return std::make_shared<CircleEntity>(std::move(record));
    if (type == "TEXT") return std::make_shared<TextEntity>(std::move(record));
    throw std::invalid_argument("Unsupported DXF entity " + type);
}
}

std::string DXFRecord::value(int code, const std::string& fallback) const
{
    for (const auto& group : groups) if (group.first == code) return group.second;
    return fallback;
}
double DXFRecord::number(int code, double fallback) const
{
    const auto text = value(code);
    return text.empty() ? fallback : numeric(text);
}
int DXFRecord::integer(int code, int fallback) const
{
    const auto text = value(code);
    return text.empty() ? fallback : integral(text);
}
std::string DXFRecord::raw() const
{
    std::ostringstream result;
    for (const auto& [code, value] : groups) result << code << '\n' << value << '\n';
    return result.str();
}
std::shared_ptr<DXFEntity> parseEntity(const std::string& raw)
{
    auto record = groups(raw);
    if (record.groups.empty() || record.groups.front().first != 0) throw std::invalid_argument("Missing DXF entity type");
    return entity(std::move(record));
}
DXFDocument DXFDocument::read(const std::string& path)
{
    std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open DXF file: " + path);
    std::ostringstream text; text << input.rdbuf();
    return parse(text.str());
}
DXFDocument DXFDocument::parse(const std::string& ascii)
{
    const auto all = groups(ascii);
    DXFDocument result;
    result.layers.emplace("0", DXFLayer{"0"});
    std::string section;
    bool sawEntities = false, sawEOF = false;
    for (std::size_t i = 0; i < all.groups.size();) {
        const auto [code, value] = all.groups[i];
        if (code == 0 && value == "SECTION") {
            if (!section.empty() || i+1 >= all.groups.size() || all.groups[i+1].first != 2)
                throw std::invalid_argument("Malformed DXF section");
            section = all.groups[i+1].second;
            if (section == "ENTITIES") sawEntities = true;
            i += 2; continue;
        }
        if (code == 0 && value == "ENDSEC") { section.clear(); ++i; continue; }
        if (code == 0 && value == "EOF") { sawEOF = true; ++i; continue; }
        if (section == "HEADER" && code == 9 && value == "$INSUNITS" && i+1 < all.groups.size()) {
            result.insertionUnits = integral(all.groups[i+1].second);
        }
        if (code == 0 && (section == "ENTITIES" || (section == "TABLES" && value == "LAYER"))) {
            DXFRecord record;
            record.groups.push_back(all.groups[i++]);
            while (i < all.groups.size() && all.groups[i].first != 0) record.groups.push_back(all.groups[i++]);
            if (section == "TABLES") {
                const int color = record.integer(62, 7);
                DXFLayer layer{record.value(2), std::abs(color), record.integer(420, -1),
                               color >= 0 && !(record.integer(70)&1)};
                result.layers[layer.name] = layer;
            } else {
                try {
                    if (record.integer(67) != 0) throw std::invalid_argument("Paper-space entities are not supported yet");
                    auto native = entity(record);
                    native->geometry(); // Validate before document/UI mutation.
                    result.entities.push_back(std::move(native));
                } catch (const std::exception& error) {
                    result.warnings.push_back(record.value(0)+" "+record.value(5)+": "+error.what());
                }
            }
            continue;
        }
        ++i;
    }
    if (!sawEntities || !sawEOF || !section.empty()) throw std::invalid_argument("DXF is incomplete or lacks ENTITIES");
    return result;
}
bool entityVisible(const DXFEntity& e) { return e.record().integer(60) == 0; }
std::vector<double> entityColor(const DXFEntity& e, const DXFLayer& layer)
{
    const int index = e.record().integer(62, 256);
    const int rgb = e.record().integer(420, index == 256 ? layer.trueColor : -1);
    if (rgb >= 0) return {((rgb>>16)&255)/255.0, ((rgb>>8)&255)/255.0, (rgb&255)/255.0};
    const int aci = index == 256 || index == 0 ? layer.color : std::abs(index);
    const std::vector<std::vector<double>> basic{{1,1,1}, {1,0,0}, {1,1,0}, {0,1,0},
        {0,1,1}, {0,0,1}, {1,0,1}, {1,1,1}, {0.5,0.5,0.5}, {0.75,0.75,0.75}};
    if (aci >= 0 && aci < 10) return basic[aci];
    if (aci >= 250 && aci <= 255) {
        const double shades[]{51,80,105,130,190,255};
        const double v = shades[aci-250]/255;
        return {v,v,v};
    }
    if (aci < 10 || aci > 249) return {1,1,1};
    const double values[]{1.0,165.0/255,127.0/255,76.0/255,38.0/255};
    const int level = (aci-10)%10;
    const double value = values[level/2], saturation = level%2 ? 0.5 : 1.0;
    const double hue = ((aci-10)/10)*0.25; // 24 hue steps, 15 degrees each.
    const double chroma = value*saturation, x = chroma*(1-std::abs(std::fmod(hue,2.0)-1));
    std::vector<double> color;
    if (hue < 1) color = {chroma,x,0}; else if (hue < 2) color = {x,chroma,0};
    else if (hue < 3) color = {0,chroma,x}; else if (hue < 4) color = {0,x,chroma};
    else if (hue < 5) color = {x,0,chroma}; else color = {chroma,0,x};
    for (auto& component : color) component += value-chroma;
    return color;
}
} // namespace DXFCAD
