#include <DXFCAD/DXFDocument.h>
#include <DXFCAD/SnapGeometry.h>
#include <DXFCAD/Drafting.h>
#include <DXFCAD/OCSFrame.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>

namespace DXFCAD {
std::shared_ptr<DXFEntity> makeInsertEntity(DXFRecord record, const std::string& context);
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
// Application-defined 102 groups and XDATA are opaque, even if they contain
// codes also used by ordinary entity geometry.
DXFRecord nativeFields(const DXFRecord& record)
{
    DXFRecord result;
    int depth = 0;
    for (const auto& g : record.groups) {
        if (g.first == 1001 && depth == 0) break;
        if (g.first == 102) {
            if (!g.second.empty() && g.second.front() == '{') ++depth;
            else if (g.second == "}" && depth) --depth;
            continue;
        }
        if (!depth) result.groups.push_back(g);
    }
    return result;
}
std::string patchedStrings(const DXFRecord& record, const std::map<int,std::string>& formatted, const std::set<int>& removed = {})
{
    DXFRecord result;
    std::set<int> written;
    auto missing = [&] { for (const auto& g : formatted) if (!written.count(g.first)) {
        result.groups.push_back(g); written.insert(g.first);
    }};
    const auto fields=nativeFields(record);
    const auto type=fields.value(0);
    const bool entityRecord=type=="LINE" || type=="ARC" || type=="CIRCLE" || type=="LWPOLYLINE" || type=="TEXT" || type=="MTEXT" || type=="INSERT";
    bool hasEntitySubclass=false;
    for (const auto& g : fields.groups) if (g.first==100 && g.second=="AcDbEntity") hasEntitySubclass=true;
    auto common=[&] { for (int code : {8,60,62}) if (formatted.count(code) && fields.value(code).empty() && !written.count(code)) {
        result.groups.emplace_back(code,formatted.at(code)); written.insert(code);
    }};
    bool xdata = false, inEntity=false;
    int depth = 0;
    for (auto g : record.groups) {
        if (!xdata && !depth && entityRecord) {
            if ((inEntity && g.first==100 && g.second!="AcDbEntity") || (!hasEntitySubclass && (g.first==10 || g.first==100))) common();
            if (g.first==100 && g.second=="AcDbEntity") inEntity=true;
        }
        if (g.first == 1001 && depth == 0) { missing(); xdata = true; }
        if (!xdata && g.first == 102) {
            if (!g.second.empty() && g.second.front() == '{') ++depth;
            else if (g.second == "}" && depth) --depth;
        } else if (!xdata && !depth && formatted.count(g.first)) {
            g.second = formatted.at(g.first); written.insert(g.first);
        }
        if (!xdata && !depth && removed.count(g.first)) continue;
        result.groups.push_back(std::move(g));
    }
    missing();
    return result.raw();
}
std::string patched(const DXFRecord& record, const std::map<int,double>& values)
{
    std::map<int,std::string> formatted;
    for (const auto& [code,value] : values) {
        if (!std::isfinite(value)) throw std::overflow_error("Transformed DXF value exceeds finite range");
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
        formatted[code] = out.str();
    }
    return patchedStrings(record,formatted);
}
void finitePoint(const Vector3D& p) {
    for (double n : {p.x,p.y,p.z}) if (!std::isfinite(n)) throw std::invalid_argument("Transform coordinates must be finite");
}
Vector3D rotateVector(const Vector3D& p, double angle) {
    const double c=std::cos(angle), s=std::sin(angle);
    return {c*p.x-s*p.y,s*p.x+c*p.y,p.z};
}
class PolylineEntity final : public DXFEntity {
public:
    using DXFEntity::DXFEntity;
    std::string translated(const Vector3D& offset) const override {
        return updateLWPolyline(raw(),polyline(nativeFields(record_)).translated(offset));
    }
    EntityGeometry geometry() const override { return {polyline(nativeFields(record_)).worldSegments(), {}, {}}; }
    std::vector<DXFSnapPoint> snapPoints() const override {
        std::vector<DXFSnapPoint> result;
        for (const auto& p : polyline(nativeFields(record_)).worldVertices()) result.push_back({p,"vertex"});
        const auto midpoints=SnapGeometry::midpoints(geometry());
        result.insert(result.end(),midpoints.begin(),midpoints.end());
        return result;
    }
    std::vector<LWPolylineWidthRegion> widthRegions(double tolerance) const override {
        return polyline(nativeFields(record_)).widthRegions(tolerance);
    }
};
class LineEntity final : public DXFEntity {
public:
    using DXFEntity::DXFEntity;
    std::string translated(const Vector3D& offset) const override { return translatedPoints(offset,{10,11},false); }
    EntityGeometry geometry() const override {
        // LINE endpoints are WCS, even when an extrusion vector is present.
        return {{{point(record_, 10), point(record_, 11), std::nullopt}}, {}, {}};
    }
};
class CircleEntity final : public DXFEntity {
public:
    using DXFEntity::DXFEntity;
    std::string translated(const Vector3D& offset) const override { return translatedPoints(offset,{10},true); }
    EntityGeometry geometry() const override {
        const auto center = point(record_, 10);
        const OCSFrame frame(extrusion(record_));
        return {{}, {{frame.toWorld({center.x, center.y}, center.z), frame.normal(), radius(record_)}}, {}};
    }
};
class ArcEntity final : public DXFEntity {
public:
    using DXFEntity::DXFEntity;
    std::string translated(const Vector3D& offset) const override { return translatedPoints(offset,{10},true); }
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
    std::string translated(const Vector3D& offset) const override {
        const auto fields = nativeFields(record_);
        return translatedPoints(offset,fields.value(11).empty() ? std::vector<int>{10} : std::vector<int>{10,11},true);
    }
    EntityGeometry geometry() const override {
        const auto fields = nativeFields(record_);
        const int horizontal = fields.integer(72), vertical = fields.integer(73);
        if (horizontal < 0 || horizontal > 2 || vertical < 0 || vertical > 3) {
            throw std::invalid_argument("TEXT aligned/fit/middle modes or invalid justification are not supported yet");
        }
        if (fields.integer(71) || fields.number(51)) {
            throw std::invalid_argument("TEXT mirroring or oblique style is not supported yet");
        }
        const double height = fields.number(40), width = fields.number(41, 1);
        if (height <= 0 || width <= 0) throw std::invalid_argument("TEXT height/width must be positive");
        const OCSFrame frame(extrusion(fields));
        // DXF 11 is the anchor for any justification other than baseline/left.
        const auto origin = point(fields, horizontal || vertical ? 11 : 10);
        const double angle = fields.number(50)*std::acos(-1.0)/180;
        const auto x = frame.xAxis(), y = frame.yAxis();
        const double c = std::cos(angle), s = std::sin(angle);
        return {{}, {}, TextGeometry{fields.value(1), frame.toWorld({origin.x, origin.y}, origin.z),
            {c*x.x+s*y.x, c*x.y+s*y.y, c*x.z+s*y.z},
            {-s*x.x+c*y.x, -s*x.y+c*y.y, -s*x.z+c*y.z}, frame.normal(), height, width, horizontal, vertical}};
    }
};
class MTextEntity final : public DXFEntity {
public:
    using DXFEntity::DXFEntity;
    std::string translated(const Vector3D& offset) const override { return translatedPoints(offset,{10},false); }
    EntityGeometry geometry() const override {
        const auto fields = nativeFields(record_);
        const int attachment = fields.integer(71, 1), direction = fields.integer(72, 1);
        if (attachment < 1 || attachment > 9 || (direction != 1 && direction != 5))
            throw std::invalid_argument("Unsupported MTEXT attachment or drawing direction");
        const double height = fields.number(40), width = fields.number(41), spacing = fields.number(44, 1);
        const int spacingStyle = fields.integer(73, 1);
        if (height <= 0 || width < 0 || spacing < 0.25 || spacing > 4 || (spacingStyle != 1 && spacingStyle != 2))
            throw std::invalid_argument("Invalid MTEXT height, width, or line spacing");
        if (fields.integer(75) || fields.integer(76) > 1 || fields.integer(90))
            throw std::invalid_argument("MTEXT columns and background fills are not supported yet");
        std::string content;
        for (const auto& [code, value] : fields.groups) if (code == 3 || code == 1) content += value;
        std::string plain;
        for (std::size_t i = 0; i < content.size(); ++i) {
            const char c = content[i];
            if (c == '\\') {
                if (++i == content.size()) throw std::invalid_argument("Incomplete MTEXT escape");
                const char command = content[i];
                if (command == 'P') plain += '\n';
                else if (command == '\\' || command == '{' || command == '}') plain += command;
                else if (command == '~') plain += "\xc2\xa0";
                else throw std::invalid_argument("MTEXT inline formatting is not supported yet");
            } else if (c == '{' || c == '}' || c == '\t' || c == '\r') {
                throw std::invalid_argument("MTEXT groups, tabs, or control characters are not supported yet");
            } else plain += c;
        }
        const OCSFrame frame(extrusion(fields));
        const auto normal = frame.normal();
        Vector3D x = frame.xAxis();
        // A direction vector and rotation can coexist; the last one wins.
        bool useVector = false;
        double angle = 0;
        for (const auto& [code, value] : fields.groups) {
            if (code == 11) useVector = true;
            if (code == 50) { useVector = false; angle = numeric(value); }
        }
        if (useVector) {
            x = point(fields, 11);
            const double length = std::hypot(x.x, x.y, x.z);
            if (!std::isfinite(length) || length == 0) throw std::invalid_argument("Invalid MTEXT direction vector");
            x = {x.x/length, x.y/length, x.z/length};
            if (std::abs(x.x*normal.x+x.y*normal.y+x.z*normal.z) > 1e-7)
                throw std::invalid_argument("MTEXT direction must lie in its extrusion plane");
        } else {
            const auto a = frame.xAxis(), b = frame.yAxis();
            x = {std::cos(angle)*a.x+std::sin(angle)*b.x, std::cos(angle)*a.y+std::sin(angle)*b.y,
                 std::cos(angle)*a.z+std::sin(angle)*b.z};
        }
        Vector3D y{normal.y*x.z-normal.z*x.y, normal.z*x.x-normal.x*x.z, normal.x*x.y-normal.y*x.x};
        // MTEXT insertion/direction are WCS, unlike TEXT's OCS insertion.
        TextGeometry text{plain, point(fields, 10), x, y, normal, height, 1};
        text.multiline = true;
        text.referenceWidth = width;
        text.lineSpacing = spacing;
        text.attachment = attachment;
        return {{}, {}, text};
    }
};
class DimensionEntity final : public DXFEntity {
public:
    DimensionEntity(DXFRecord record, std::string context)
        : DXFEntity(std::move(record)), context_(std::move(context)) {}
    std::string context() const override { return context_; }
    EntityGeometry geometry() const override {
        const auto fields = nativeFields(record_);
        if ((fields.integer(70)&7) != 0)
            throw std::invalid_argument("Only cached rotated/linear DIMENSION pictures are supported");
        const auto normal = OCSFrame(extrusion(fields)).normal();
        if (normal.x != 0 || normal.y != 0 || normal.z != 1 || fields.number(51)
            || fields.number(12) || fields.number(22) || fields.number(32))
            throw std::invalid_argument("Transformed DIMENSION pictures are not supported yet");
        for (int code : {10, 11, 13, 14}) point(fields, code);
        const auto data = groups(context_);
        std::vector<DXFRecord> records;
        for (const auto& g : data.groups) {
            if (g.first == 0) records.emplace_back();
            if (records.empty()) throw std::invalid_argument("Invalid DIMENSION picture context");
            records.back().groups.push_back(g);
        }
        if (records.size() < 3 || records.front().value(0) != "BLOCK" || records.back().value(0) != "ENDBLK"
            || fields.value(2).empty() || records.front().value(2) != fields.value(2))
            throw std::invalid_argument("DIMENSION picture block is missing or mismatched");
        const auto header = nativeFields(records.front());
        if ((header.integer(70)&(4|8|16)) || header.number(10) || header.number(20) || header.number(30))
            throw std::invalid_argument("External or displaced DIMENSION picture blocks are not supported");
        EntityGeometry result;
        for (std::size_t i = 1; i+1 < records.size(); ++i) {
            auto child = nativeFields(records[i]);
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
                const OCSFrame frame(extrusion(child));
                result.faces.push_back({frame.toWorld({a.x,a.y},a.z), frame.toWorld({b.x,b.y},b.z), frame.toWorld({c.x,c.y},c.z)});
                continue;
            }
            EntityGeometry geometry;
            if (type == "LINE") geometry = LineEntity(child).geometry();
            else if (type == "ARC") geometry = ArcEntity(child).geometry();
            else if (type == "CIRCLE") geometry = CircleEntity(child).geometry();
            else if (type == "TEXT") geometry = TextEntity(child).geometry();
            else if (type == "MTEXT") {
                // A prefix alignment command has no effect on a single plain
                // text run; stacked/inline-formatted content still rejects.
                for (auto& [code, value] : child.groups) if ((code == 1 || code == 3)
                    && value.size() >= 4 && value.compare(0,2,"\\A") == 0
                    && value[2] >= '0' && value[2] <= '2' && value[3] == ';') value.erase(0,4);
                geometry = MTextEntity(child).geometry();
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
private:
    std::string context_;
};

std::shared_ptr<DXFEntity> entity(DXFRecord record, const std::string& context = "")
{
    const auto type = record.value(0);
    if (type == "LWPOLYLINE") return std::make_shared<PolylineEntity>(std::move(record));
    if (type == "LINE") return std::make_shared<LineEntity>(std::move(record));
    if (type == "ARC") return std::make_shared<ArcEntity>(std::move(record));
    if (type == "CIRCLE") return std::make_shared<CircleEntity>(std::move(record));
    if (type == "TEXT") return std::make_shared<TextEntity>(std::move(record));
    if (type == "MTEXT") return std::make_shared<MTextEntity>(std::move(record));
    if (type == "DIMENSION") return std::make_shared<DimensionEntity>(std::move(record), context);
    if (type == "INSERT") return makeInsertEntity(std::move(record), context);
    throw std::invalid_argument("Unsupported DXF entity " + type);
}
}

DXFRecord readDXFGroups(const std::string& ascii) { return groups(ascii); }
DXFRecord nativeEntityFields(const DXFRecord& record) { return nativeFields(record); }
std::string patchEntityFields(const DXFRecord& record, const std::map<int,double>& values) { return patched(record,values); }

std::string patchEntityStringFields(const DXFRecord& record, const std::map<int,std::string>& values, const std::set<int>& removed) {
    for (const auto& [code,value] : values) if (value.find_first_of("\r\n")!=std::string::npos) throw std::invalid_argument("DXF values cannot contain newlines");
    return patchedStrings(record,values,removed);
}

Vector3D TextGeometry::alignedOrigin(double minX, double minY, double maxX, double maxY) const
{
    if (!std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(maxX)
        || !std::isfinite(maxY) || minX > maxX || minY > maxY)
        throw std::invalid_argument("Invalid text outline bounds");
    double x = 0, y = 0;
    if (horizontalAlignment || verticalAlignment) {
        x = horizontalAlignment == 1 ? -(minX+maxX)/2 :
            horizontalAlignment == 2 ? -maxX : -minX;
        if (verticalAlignment == 1) y = -minY;
        if (verticalAlignment == 2) y = -(minY+maxY)/2;
        if (verticalAlignment == 3) y = -maxY;
    }
    x *= widthFactor;
    return {origin.x+x*xAxis.x+y*yAxis.x, origin.y+x*xAxis.y+y*yAxis.y,
            origin.z+x*xAxis.z+y*yAxis.z};
}

std::vector<TextGeometry> TextGeometry::layout(const std::function<TextBounds(const std::string&)>& measure) const
{
    if (!multiline) return {*this};
    std::vector<std::string> lines;
    auto width = [&](const std::string& s) { const auto b = measure(s); return b.maxX-b.minX; };
    std::size_t start = 0;
    do {
        const auto end = text.find('\n', start);
        const auto paragraph = text.substr(start, end == std::string::npos ? end : end-start);
        if (referenceWidth == 0 || paragraph.empty()) lines.push_back(paragraph);
        else {
            std::string line;
            std::size_t lastSpace = std::string::npos;
            for (std::size_t i = 0; i < paragraph.size();) {
                const auto begin = i++;
                // Keep each UTF-8 codepoint intact while splitting long words.
                while (i < paragraph.size() && (static_cast<unsigned char>(paragraph[i]) & 0xc0) == 0x80) ++i;
                const auto character = paragraph.substr(begin, i-begin);
                if (character == " ") lastSpace = line.size();
                line += character;
                if (width(line) > referenceWidth && line.size() > character.size()) {
                    if (lastSpace != std::string::npos && lastSpace > 0) {
                        lines.push_back(line.substr(0, lastSpace));
                        line.erase(0, lastSpace+1);
                    } else {
                        lines.push_back(line.substr(0, line.size()-character.size()));
                        line = character;
                    }
                    lastSpace = line.find_last_of(' ');
                }
            }
            lines.push_back(line);
        }
        if (end == std::string::npos) break;
        start = end+1;
    } while (start <= text.size());
    const double advance = height*5.0/3.0*lineSpacing;
    double maxWidth = 0, bottom = -static_cast<double>(lines.size()-1)*advance, top = height;
    std::vector<TextBounds> bounds;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto b = measure(lines[i]);
        bounds.push_back(b);
        maxWidth = std::max(maxWidth, b.maxX-b.minX);
        bottom = std::min(bottom, b.minY-i*advance);
        top = std::max(top, b.maxY-i*advance);
    }
    const int column = (attachment-1)%3, row = (attachment-1)/3;
    const double offsetX = -column*(referenceWidth > 0 ? referenceWidth : maxWidth)/2;
    const double offsetY = row == 0 ? -top : row == 1 ? -(top+bottom)/2 : -bottom;
    std::vector<TextGeometry> runs;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].empty()) continue;
        auto run = *this;
        run.text = lines[i];
        run.multiline = false;
        const double x = offsetX-bounds[i].minX, y = offsetY-i*advance;
        run.origin = {origin.x+x*xAxis.x+y*yAxis.x, origin.y+x*xAxis.y+y*yAxis.y, origin.z+x*xAxis.z+y*yAxis.z};
        runs.push_back(run);
    }
    return runs;
}

std::string DXFRecord::value(int code, const std::string& fallback) const
{
    for (const auto& group : groups) if (group.first == code) return group.second;
    return fallback;
}

DXFViewport::DXFViewport(DXFRecord record, std::string layout,
                         const std::map<std::string,std::string>& layerHandles)
    : record_(std::move(record)), layout_(std::move(layout))
{
    const auto fields = nativeFields(record_);
    if (fields.value(0) != "VIEWPORT" || fields.integer(67) != 1)
        throw std::invalid_argument("Expected a paper-space VIEWPORT");
    if (fields.integer(69) <= 0 || fields.number(40) <= 0 || fields.number(41) <= 0 || fields.number(45) <= 0)
        throw std::invalid_argument("Invalid VIEWPORT ID or dimensions");
    point(fields, 10);
    for (int code : {12,22,16,26,36,17,27,37,51}) fields.number(code);
    fields.integer(68); fields.integer(90);
    if (layout_.empty()) layout_ = fields.value(410, "Paper space");
    const double ratio = fields.number(41)/fields.number(45);
    if (!std::isfinite(ratio) || ratio <= 0) throw std::invalid_argument("Invalid VIEWPORT scale");
    std::set<std::string> frozen;
    for (const auto& [code,value] : fields.groups) if (code == 331) {
        auto handle = trim(value);
        std::transform(handle.begin(),handle.end(),handle.begin(),[](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        const auto layer = layerHandles.find(handle);
        if (layer == layerHandles.end() || layer->second.empty())
            frozenLayerIssue_ = "Frozen-layer reference is missing or ambiguous";
        else if (frozen.insert(layer->second).second) frozenLayers_.push_back(layer->second);
    }
}
int DXFViewport::id() const { return nativeFields(record_).integer(69); }
double DXFViewport::paperWidth() const { return nativeFields(record_).number(40); }
double DXFViewport::paperHeight() const { return nativeFields(record_).number(41); }
double DXFViewport::viewHeight() const { return nativeFields(record_).number(45); }
double DXFViewport::scale() const { return paperHeight()/viewHeight(); }
bool DXFViewport::freezesLayer(const std::string& name) const
{
    auto folded = [](unsigned char c) { return c >= 'a' && c <= 'z' ? c-'a'+'A' : c; };
    for (const auto& layer : frozenLayers_) if (layer.size() == name.size()
        && std::equal(layer.begin(),layer.end(),name.begin(),[&](unsigned char a, unsigned char b) { return folded(a) == folded(b); }))
        return true;
    return false;
}
std::string DXFViewport::navigationIssue() const
{
    const auto fields = nativeFields(record_);
    if (id() == 1) return "Paper-space overview";
    const int flags = fields.integer(90);
    if (!fields.integer(68) || (flags&131072)) return "Viewport is off";
    if (flags&1) return "Perspective views are not supported yet";
    if ((flags&(2|4|65536)) || !fields.value(340).empty()) return "Clipped views are not supported yet";
    if (!frozenLayerIssue_.empty()) return frozenLayerIssue_;
    if (fields.number(16) != 0 || fields.number(26) != 0 || fields.number(36,1) <= 0)
        return "Only top views are supported yet";
    return {};
}
DXFViewportView DXFViewport::modelView() const
{
    const auto issue = navigationIssue();
    if (!issue.empty()) throw std::invalid_argument(issue);
    const auto fields = nativeFields(record_);
    const double angle = fields.number(51)*std::acos(-1.0)/180;
    // WCS -> DCS applies the view twist; invert it to obtain camera axes.
    const Vector3D x{std::cos(angle),-std::sin(angle),0}, y{std::sin(angle),std::cos(angle),0};
    const double cx = fields.number(12), cy = fields.number(22);
    const Vector3D center{fields.number(17)+cx*x.x+cy*y.x, fields.number(27)+cx*x.y+cy*y.y, fields.number(37)};
    if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z))
        throw std::invalid_argument("Invalid VIEWPORT model center");
    return {center,x,y,{0,0,1},viewHeight()};
}
std::string DXFEntity::mirrored(double degrees, const Vector3D& pivot) const
{
    if (!std::isfinite(degrees)) throw std::invalid_argument("Mirror line angle must be finite");
    finitePoint(pivot);
    translated({0,0,0});
    const auto fields=nativeFields(record_);
    const double pi=std::acos(-1.0), angle=std::fmod(degrees,180)*pi/180;
    const double ux=std::cos(angle), uy=std::sin(angle);
    auto reflect = [&](Vector3D p) { const double dot=ux*p.x+uy*p.y; return Vector3D{2*ux*dot-p.x,2*uy*dot-p.y,p.z}; };
    auto world = [&](Vector3D p) {
        auto v=reflect({p.x-pivot.x,p.y-pivot.y,p.z-pivot.z});
        Vector3D result{pivot.x+v.x,pivot.y+v.y,p.z}; finitePoint(result); return result;
    };
    const auto oldNormal=extrusion(fields);
    auto normal=reflect({oldNormal.x,oldNormal.y,oldNormal.z});
    // A reflected frame changes handedness. Reversing its normal preserves
    // arc/bulge parameter direction and reflects both axes of text outlines.
    if (type()!="LINE") normal={-normal.x,-normal.y,-normal.z};
    const OCSFrame oldFrame(oldNormal), frame({normal.x,normal.y,normal.z});
    auto ocs = [&](Vector3D p) { return frame.fromWorld(world(oldFrame.toWorld({p.x,p.y},p.z))); };
    if (type()=="LWPOLYLINE") {
        const auto previous=readLWPolyline(raw());
        auto vertices=previous.vertices();
        double elevation=previous.elevation();
        for (auto& v : vertices) { const auto p=ocs({v.x,v.y,previous.elevation()}); v.x=p.x; v.y=p.y; elevation=p.z; }
        auto result=updateLWPolyline(raw(),LWPolyline(vertices,previous.closed(),previous.constantWidth(),elevation,{normal.x,normal.y,normal.z}));
        if (!fields.value(39).empty()) result=patched(parseEntity(result)->record(),{{39,-fields.number(39)}});
        return result;
    }
    std::map<int,double> values;
    auto put = [&](int code,Vector3D p) { values[code]=p.x; values[code+10]=p.y; values[code+20]=p.z; };
    if (type()=="LINE") { put(10,world(point(fields,10))); put(11,world(point(fields,11))); }
    else if (type()=="MTEXT") put(10,world(point(fields,10)));
    else {
        put(10,ocs(point(fields,10)));
        if (type()=="TEXT" && !fields.value(11).empty()) put(11,ocs(point(fields,11)));
    }
    put(210,normal);
    if (type()!="LINE" && !fields.value(39).empty()) values[39]=-fields.number(39);
    if (type()=="ARC") {
        const auto g=geometry().segments.at(0);
        const auto c=frame.fromWorld(world(g.arc->center)), a=frame.fromWorld(world(g.start));
        double start=std::atan2(a.y-c.y,a.x-c.x)*180/pi;
        if (start<0) start+=360;
        values[50]=start; values[51]=std::fmod(start+g.arc->sweep*180/pi,360);
    }
    if (type()=="TEXT" || type()=="MTEXT") {
        const auto x=reflect(geometry().text->xAxis), local=frame.fromWorld(x);
        const double a=std::atan2(local.y,local.x);
        if (type()=="TEXT") values[50]=a*180/pi;
        else {
            if (!fields.value(11).empty() || fields.value(50).empty()) put(11,x);
            if (!fields.value(50).empty()) values[50]=a;
        }
    }
    if (type()=="INSERT") {
        const double a=fields.number(50)*pi/180;
        const auto local=frame.fromWorld(reflect(oldFrame.toWorld({std::cos(a),std::sin(a)},0)));
        values[50]=std::atan2(local.y,local.x)*180/pi;
        values[43]=-fields.number(43,1);
    }
    const auto result=patched(record_,values);
    parseEntity(result,context())->geometry();
    return result;
}
double scaleFactor(const Vector3D& center, const Vector3D& target, double referenceLength)
{
    if (!std::isfinite(referenceLength) || referenceLength<=0) throw std::invalid_argument("Reference length must be positive and finite");
    const auto d=displacement(center,target);
    const double factor=std::hypot(d.x,d.y,d.z)/referenceLength;
    if (!std::isfinite(factor) || factor<=0) throw std::invalid_argument("Scale factor must be positive and finite");
    return factor;
}
std::string DXFEntity::scaled(double factor, const Vector3D& pivot) const
{
    if (!std::isfinite(factor) || factor<=0) throw std::invalid_argument("Scale factor must be positive and finite");
    finitePoint(pivot);
    translated({0,0,0});
    if (factor==1) return raw();
    const auto fields=nativeFields(record_);
    const OCSFrame frame(extrusion(fields));
    auto world = [&](const Vector3D& p) {
        Vector3D result{pivot.x+(p.x-pivot.x)*factor,pivot.y+(p.y-pivot.y)*factor,pivot.z+(p.z-pivot.z)*factor};
        finitePoint(result); return result;
    };
    auto ocs = [&](const Vector3D& p) { return frame.fromWorld(world(frame.toWorld({p.x,p.y},p.z))); };
    if (type()=="LWPOLYLINE") {
        const auto previous=readLWPolyline(raw());
        const auto center=frame.fromWorld(pivot);
        auto model=previous.scaled(factor,{center.x,center.y});
        model=model.withProperties(previous.closed(),model.constantWidth(),center.z+(previous.elevation()-center.z)*factor,previous.extrusion());
        auto result=updateLWPolyline(raw(),model);
        if (!fields.value(39).empty()) result=patched(parseEntity(result)->record(),{{39,fields.number(39)*factor}});
        return result;
    }
    std::map<int,double> values;
    auto put = [&](int code,Vector3D p) { values[code]=p.x; values[code+10]=p.y; values[code+20]=p.z; };
    if (type()=="LINE") { put(10,world(point(fields,10))); put(11,world(point(fields,11))); }
    else if (type()=="MTEXT") put(10,world(point(fields,10)));
    else {
        put(10,ocs(point(fields,10)));
        if (type()=="TEXT" && !fields.value(11).empty()) put(11,ocs(point(fields,11)));
    }
    if (type()=="CIRCLE" || type()=="ARC" || type()=="TEXT" || type()=="MTEXT") values[40]=fields.number(40)*factor;
    if (type()=="MTEXT") for (int code : {41,42,43}) if (!fields.value(code).empty()) values[code]=fields.number(code)*factor;
    if (type()=="INSERT") {
        for (int code : {41,42,43}) values[code]=fields.number(code,1)*factor;
        for (int code : {44,45}) if (!fields.value(code).empty()) values[code]=fields.number(code)*factor;
    }
    if (!fields.value(39).empty()) values[39]=fields.number(39)*factor;
    const auto result=patched(record_,values);
    parseEntity(result,context())->geometry();
    return result;
}
double rotationAngle(const Vector3D& center, const Vector3D& direction)
{
    const auto delta = displacement(center,direction);
    if (delta.x == 0 && delta.y == 0) throw std::invalid_argument("Rotation direction must differ in XY");
    return std::atan2(delta.y,delta.x)*180/std::acos(-1.0);
}
std::string DXFEntity::rotated(double degrees, const Vector3D& pivot) const
{
    if (!std::isfinite(degrees)) throw std::invalid_argument("Rotation angle must be finite");
    finitePoint(pivot);
    translated({0,0,0}); // Read-only entities cannot be transformed.
    const auto fields = nativeFields(record_);
    const double pi=std::acos(-1.0), angle=std::fmod(degrees,360)*pi/180;
    if (angle == 0) return raw();
    const auto oldNormal=extrusion(fields);
    const auto normal=rotateVector({oldNormal.x,oldNormal.y,oldNormal.z},angle);
    const OCSFrame oldFrame(oldNormal), frame({normal.x,normal.y,normal.z});
    auto world = [&](const Vector3D& p) {
        auto v=rotateVector({p.x-pivot.x,p.y-pivot.y,p.z-pivot.z},angle);
        Vector3D result{v.x+pivot.x,v.y+pivot.y,p.z}; finitePoint(result); return result;
    };
    auto ocs = [&](const Vector3D& p) { return frame.fromWorld(world(oldFrame.toWorld({p.x,p.y},p.z))); };
    if (type()=="LWPOLYLINE") {
        const auto previous=readLWPolyline(raw());
        auto vertices=previous.vertices();
        double elevation=previous.elevation();
        for (auto& vertex : vertices) {
            const auto p=ocs({vertex.x,vertex.y,previous.elevation()});
            vertex.x=p.x; vertex.y=p.y; elevation=p.z;
        }
        return updateLWPolyline(raw(),LWPolyline(vertices,previous.closed(),previous.constantWidth(),elevation,{normal.x,normal.y,normal.z}));
    }
    std::map<int,double> values;
    auto put = [&](int code,Vector3D p) { values[code]=p.x; values[code+10]=p.y; values[code+20]=p.z; };
    if (type()=="LINE") { put(10,world(point(fields,10))); put(11,world(point(fields,11))); }
    else if (type()=="MTEXT") put(10,world(point(fields,10)));
    else {
        put(10,ocs(point(fields,10)));
        if (type()=="TEXT" && !fields.value(11).empty()) put(11,ocs(point(fields,11)));
    }
    put(210,normal);
    if (type()=="ARC") {
        const auto g=geometry().segments.at(0);
        const auto center=frame.fromWorld(world(g.arc->center)), start=frame.fromWorld(world(g.start));
        double a=std::atan2(start.y-center.y,start.x-center.x)*180/pi;
        if (a<0) a+=360;
        values[50]=a; values[51]=a+g.arc->sweep*180/pi;
    }
    if (type()=="TEXT" || type()=="MTEXT") {
        const auto x=rotateVector(geometry().text->xAxis,angle);
        const auto local=frame.fromWorld(x);
        const double a=std::atan2(local.y,local.x);
        if (type()=="TEXT") values[50]=a*180/pi;
        else {
            if (!fields.value(11).empty() || fields.value(50).empty()) put(11,x);
            if (!fields.value(50).empty()) values[50]=a;
        }
    }
    if (type()=="INSERT") {
        const double a=fields.number(50)*pi/180;
        const auto local=frame.fromWorld(rotateVector(oldFrame.toWorld({std::cos(a),std::sin(a)},0),angle));
        values[50]=std::atan2(local.y,local.x)*180/pi;
    }
    const auto result=patched(record_,values);
    parseEntity(result,context())->geometry();
    return result;
}
std::string DXFEntity::copied(const Vector3D& offset) const
{
    auto source = parseEntity(translated(offset),context())->record();
    DXFRecord result;
    int depth = 0, skipDepth = 0;
    bool xdata = false;
    for (const auto& g : source.groups) {
        if (g.first == 1001 && depth == 0) xdata = true;
        if (!xdata && g.first == 102) {
            if (!g.second.empty() && g.second.front() == '{') {
                ++depth;
                if (!skipDepth && (g.second == "{ACAD_REACTORS" || g.second == "{ACAD_XDICTIONARY")) skipDepth = depth;
            } else if (g.second == "}" && depth) {
                const bool skipped = skipDepth != 0;
                if (skipDepth == depth) skipDepth = 0;
                --depth;
                if (skipped) continue;
            }
        }
        if (skipDepth) continue;
        if (!xdata && depth == 0 && (g.first == 5 || g.first == 330 || g.first == 360)) continue;
        result.groups.push_back(g);
    }
    return result.raw();
}
std::string DXFEntity::translated(const Vector3D&) const
{
    throw std::invalid_argument(type()+" does not support native Move yet");
}
std::string DXFEntity::translatedPoints(const Vector3D& offset, const std::vector<int>& codes, bool ocs) const
{
    for (double n : {offset.x,offset.y,offset.z})
        if (!std::isfinite(n)) throw std::invalid_argument("Move displacement must be finite");
    geometry();
    if (offset.x == 0 && offset.y == 0 && offset.z == 0) return raw();
    const auto fields = nativeFields(record_);
    const auto delta = ocs ? OCSFrame(extrusion(fields)).fromWorld(offset) : offset;
    std::map<int,std::string> values;
    for (int code : codes) {
        const auto p = point(fields,code);
        const Vector3D moved{p.x+delta.x,p.y+delta.y,p.z+delta.z};
        int axis = 0;
        for (double value : {moved.x,moved.y,moved.z}) {
            if (!std::isfinite(value)) throw std::overflow_error("Moved coordinates exceed finite range");
            std::ostringstream number;
            number.imbue(std::locale::classic());
            number << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
            values[code+10*axis++] = number.str();
        }
    }
    DXFRecord result;
    std::set<int> written;
    auto missing = [&] {
        for (const auto& g : values) if (!written.count(g.first)) {
            result.groups.push_back(g); written.insert(g.first);
        }
    };
    int depth = 0;
    bool xdata = false;
    for (auto g : record_.groups) {
        if (g.first == 1001 && depth == 0) { missing(); xdata = true; }
        if (!xdata && g.first == 102) {
            if (!g.second.empty() && g.second.front() == '{') ++depth;
            else if (g.second == "}" && depth) --depth;
        } else if (!xdata && !depth && values.count(g.first)) {
            g.second = values.at(g.first); written.insert(g.first);
            // An omitted Z defaults to zero. Insert its moved value beside Y,
            // retaining subclass boundaries and opaque application groups.
            const int z = g.first+10;
            if (values.count(z) && fields.value(z).empty()) {
                result.groups.push_back(g);
                result.groups.emplace_back(z,values.at(z));
                written.insert(z);
                continue;
            }
        }
        result.groups.push_back(std::move(g));
    }
    missing();
    parseEntity(result.raw(),context())->geometry();
    return result.raw();
}
Vector3D displacement(const Vector3D& start, const Vector3D& end)
{
    for (double n : {start.x,start.y,start.z,end.x,end.y,end.z})
        if (!std::isfinite(n)) throw std::invalid_argument("Move points must be finite");
    const Vector3D delta{end.x-start.x,end.y-start.y,end.z-start.z};
    for (double n : {delta.x,delta.y,delta.z})
        if (!std::isfinite(n)) throw std::overflow_error("Move displacement exceeds finite range");
    if (delta.x == 0 && delta.y == 0 && delta.z == 0)
        throw std::invalid_argument("Move displacement must be nonzero");
    return delta;
}

std::vector<DXFSnapPoint> DXFEntity::snapPoints() const
{
    const auto derived = geometry();
    std::vector<DXFSnapPoint> result;
    for (const auto& segment : derived.segments) {
        result.push_back({segment.start,"endpoint"});
        result.push_back({segment.end,"endpoint"});
        if (segment.arc) result.push_back({segment.arc->center,"center"});
    }
    for (const auto& circle : derived.circles) result.push_back({circle.center,"center"});
    if (derived.text) result.push_back({derived.text->origin,"insertion point"});
    const auto midpoints=SnapGeometry::midpoints(derived);
    result.insert(result.end(),midpoints.begin(),midpoints.end());
    return result;
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
std::shared_ptr<DXFEntity> parseEntity(const std::string& raw, const std::string& context)
{
    auto record = groups(raw);
    if (record.groups.empty() || record.groups.front().first != 0) throw std::invalid_argument("Missing DXF entity type");
    for (std::size_t i = 1; i < record.groups.size(); ++i)
        if (record.groups[i].first == 0) throw std::invalid_argument("Expected exactly one DXF entity");
    return entity(std::move(record), context);
}
LWPolyline readLWPolyline(const std::string& raw)
{
    const auto e = parseEntity(raw);
    if (e->type() != "LWPOLYLINE") throw std::invalid_argument("Expected LWPOLYLINE data");
    return polyline(nativeFields(e->record()));
}

std::string updateLWPolyline(const std::string& raw, const LWPolyline& p, const std::vector<int>& origins)
{
    const auto previous = readLWPolyline(raw);
    const auto record = parseEntity(raw)->record();
    const auto fields = nativeFields(record);
    std::vector<std::string> ids(previous.vertices().size());
    std::size_t vertex = 0;
    bool seenVertex = false;
    for (const auto& g : fields.groups) {
        if (g.first == 10) { if (seenVertex) ++vertex; seenVertex = true; }
        if (g.first == 91 && seenVertex) ids.at(vertex) = g.second;
    }
    if (!origins.empty() && origins.size() != p.vertices().size())
        throw std::invalid_argument("Vertex origins must match the new vertex count");
    if (origins.empty() && previous.vertices().size() != p.vertices().size())
        throw std::invalid_argument("Supply vertex origins when changing vertex count");
    std::set<int> used;
    for (int origin : origins) {
        if (origin < -1 || origin >= static_cast<int>(ids.size()) || (origin >= 0 && !used.insert(origin).second))
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
    geometry.groups.emplace_back(90, std::to_string(p.vertices().size()));
    geometry.groups.emplace_back(70, std::to_string((fields.integer(70)&~1) | (p.closed() ? 1 : 0)));
    add(43, p.constantWidth()); add(38, p.elevation());
    std::size_t index = 0;
    for (const auto& v : p.vertices()) {
        add(10, v.x); add(20, v.y);
        if (v.hasWidths || v.startWidth != 0 || v.endWidth != 0) { add(40, v.startWidth); add(41, v.endWidth); }
        add(42, v.bulge);
        const int origin = origins.empty() ? static_cast<int>(index) : origins[index];
        if (origin >= 0 && !ids[origin].empty()) geometry.groups.emplace_back(91, ids[origin]);
        ++index;
    }
    add(210, p.extrusion().x); add(220, p.extrusion().y); add(230, p.extrusion().z);
    const std::set<int> replaced{90,70,43,38,10,20,40,41,42,91,210,220,230};
    DXFRecord result;
    bool inserted = false, xdata = false;
    int depth = 0;
    auto insert = [&] {
        if (!inserted) result.groups.insert(result.groups.end(), geometry.groups.begin(), geometry.groups.end());
        inserted = true;
    };
    for (const auto& g : record.groups) {
        if (g.first == 1001 && !depth) { insert(); xdata = true; }
        if (g.first == 102 && !xdata) {
            if (!g.second.empty() && g.second.front() == '{') ++depth;
            else if (g.second == "}" && depth) --depth;
            result.groups.push_back(g);
        } else if (!xdata && !depth && replaced.count(g.first)) insert();
        else result.groups.push_back(g);
    }
    insert();
    // Validate derived geometry as well as native values before exposing an edit.
    parseEntity(result.raw())->geometry();
    return result.raw();
}

std::string createLWPolyline(const LWPolyline& p, const std::string& layer)
{
    if (p.vertices().size() < 2) throw std::invalid_argument("A new LWPolyline requires at least two vertices");
    if (layer.empty() || layer.find_first_of("\r\n") != std::string::npos)
        throw std::invalid_argument("Layer name must be nonempty and contain no newlines");
    // Use an empty initial record with explicit new-vertex origins.
    return updateLWPolyline("0\nLWPOLYLINE\n100\nAcDbEntity\n8\n"+layer+
        "\n100\nAcDbPolyline\n90\n0\n70\n0\n", p, std::vector<int>(p.vertices().size(), -1));
}
std::string createRectangle(const Vector3D& first, const Vector3D& opposite, const std::string& layer)
{
    finitePoint(first);
    const auto b=pointInXYPlane(opposite,first.z);
    if (first.x==b.x || first.y==b.y) throw std::invalid_argument("Rectangle width and height must be nonzero");
    return createLWPolyline(LWPolyline({{first.x,first.y},{b.x,first.y},{b.x,b.y},{first.x,b.y}},true,0,first.z),layer);
}
std::string createLine(const Vector3D& start, const Vector3D& end, const std::string& layer)
{
    for (double n : {start.x,start.y,start.z,end.x,end.y,end.z})
        if (!std::isfinite(n)) throw std::invalid_argument("LINE coordinates must be finite");
    if (start.x == end.x && start.y == end.y && start.z == end.z)
        throw std::invalid_argument("LINE endpoints must be distinct");
    if (layer.empty() || layer.find_first_of("\r\n") != std::string::npos)
        throw std::invalid_argument("Layer name must be nonempty and contain no newlines");
    std::ostringstream raw;
    raw.imbue(std::locale::classic());
    raw << std::setprecision(std::numeric_limits<double>::max_digits10)
        << "0\nLINE\n100\nAcDbEntity\n8\n" << layer << "\n100\nAcDbLine\n"
        << "10\n" << start.x << "\n20\n" << start.y << "\n30\n" << start.z
        << "\n11\n" << end.x << "\n21\n" << end.y << "\n31\n" << end.z << '\n';
    return raw.str();
}
std::string createCircle(const Vector3D& center, double radius, const std::string& layer)
{
    for (double n : {center.x,center.y,center.z,radius})
        if (!std::isfinite(n)) throw std::invalid_argument("CIRCLE center and radius must be finite");
    if (radius <= 0) throw std::invalid_argument("CIRCLE radius must be positive");
    if (layer.empty() || layer.find_first_of("\r\n") != std::string::npos)
        throw std::invalid_argument("Layer name must be nonempty and contain no newlines");
    std::ostringstream raw;
    raw.imbue(std::locale::classic());
    raw << std::setprecision(std::numeric_limits<double>::max_digits10)
        << "0\nCIRCLE\n100\nAcDbEntity\n8\n" << layer << "\n100\nAcDbCircle\n"
        << "10\n" << center.x << "\n20\n" << center.y << "\n30\n" << center.z
        << "\n40\n" << radius << '\n';
    return raw.str();
}
double circleRadius(const Vector3D& center, const Vector3D& circumference)
{
    const auto p = pointInXYPlane(circumference,center.z);
    const double radius = std::hypot(p.x-center.x,p.y-center.y);
    createCircle(center,radius);
    return radius;
}
Vector3D pointOnXYPlane(const Vector3D& start, const Vector3D& end, double elevation)
{
    const auto p = OCSFrame({0,0,1}).intersectLine(start,end,elevation);
    return {p.x,p.y,elevation};
}
Vector3D pointInXYPlane(const Vector3D& p, double elevation)
{
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !std::isfinite(elevation))
        throw std::invalid_argument("Picked coordinates must be finite");
    if (std::abs(p.z-elevation) > 1e-7)
        throw std::invalid_argument("Snap target is outside the drawing plane");
    return {p.x,p.y,elevation};
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
    result.source_ = all;
    result.layers.emplace("0", DXFLayer{"0"});
    std::map<std::string,std::string> layoutNames, ownerLayouts, layerHandles;
    std::string metadataSection;
    // LAYOUT names belong to AcDbLayout, not the preceding plot settings.
    for (std::size_t i = 0; i < all.groups.size();) {
        DXFRecord record;
        record.groups.push_back(all.groups[i++]);
        while (i < all.groups.size() && all.groups[i].first != 0) record.groups.push_back(all.groups[i++]);
        const auto fields = nativeFields(record);
        if (fields.value(0) == "SECTION") metadataSection = fields.value(2);
        if (fields.value(0) == "ENDSEC") metadataSection.clear();
        if (metadataSection == "OBJECTS" && fields.value(0) == "LAYOUT") {
            bool layoutSubclass = false;
            for (const auto& [code,value] : fields.groups) {
                if (code == 100) layoutSubclass = value == "AcDbLayout";
                if (code == 1 && layoutSubclass) layoutNames[fields.value(5)] = value;
            }
        }
        if (metadataSection == "TABLES" && fields.value(0) == "BLOCK_RECORD") ownerLayouts[fields.value(5)] = fields.value(340);
        if (metadataSection == "TABLES" && fields.value(0) == "LAYER") {
            auto handle = trim(fields.value(5));
            std::transform(handle.begin(),handle.end(),handle.begin(),[](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            if (!handle.empty()) {
                if (layerHandles.count(handle)) layerHandles[handle].clear();
                else layerHandles[handle] = fields.value(2);
            }
        }
    }
    // Resolve picture dependencies independently of section order. Snapshot
    // only the referenced block on the entity for source-free restoration.
    std::map<std::string, std::string> blocks;
    std::set<std::string> duplicates;
    std::string blockSection;
    for (std::size_t i = 0; i < all.groups.size(); ++i) {
        const auto& g = all.groups[i];
        if (g.first == 0 && g.second == "SECTION" && i+1 < all.groups.size()) blockSection = all.groups[i+1].second;
        if (g.first == 0 && g.second == "ENDSEC") blockSection.clear();
        if (blockSection != "BLOCKS" || g.first != 0 || g.second != "BLOCK") continue;
        DXFRecord block;
        std::size_t j = i;
        for (; j < all.groups.size(); ++j) {
            if (j > i && all.groups[j].first == 0 && (all.groups[j].second == "ENDSEC" || all.groups[j].second == "BLOCK")) break;
            block.groups.push_back(all.groups[j]);
            if (all.groups[j].first == 0 && all.groups[j].second == "ENDBLK") {
                while (j+1 < all.groups.size() && all.groups[j+1].first != 0) block.groups.push_back(all.groups[++j]);
                const auto name = block.value(2);
                if (blocks.count(name)) duplicates.insert(name);
                blocks[name] = block.raw();
                break;
            }
        }
        i = j > i ? j-1 : i;
    }
    for (const auto& name : duplicates) blocks.erase(name);
    std::string insertContext;
    for (const auto& entry : blocks) insertContext += entry.second;
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
        if (code == 0 && value == "ENDSEC") {
            if (section == "ENTITIES") result.entitiesEnd_ = i;
            section.clear(); ++i; continue;
        }
        if (code == 0 && value == "EOF") { sawEOF = true; ++i; continue; }
        if (section == "HEADER" && code == 9 && value == "$INSUNITS" && i+1 < all.groups.size()) {
            result.insertionUnits = integral(all.groups[i+1].second);
        }
        if (code == 0 && (section == "ENTITIES" || (section == "TABLES" && value == "LAYER"))) {
            const auto begin = i;
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
                    if (record.value(0) == "VIEWPORT" && record.integer(67) == 1) {
                        const auto fields = nativeFields(record);
                        std::string layout = fields.value(410);
                        const auto owner = ownerLayouts.find(fields.value(330));
                        if (layout.empty() && owner != ownerLayouts.end()) {
                            const auto name = layoutNames.find(owner->second);
                            if (name != layoutNames.end()) layout = name->second;
                        }
                        result.viewports.emplace_back(record, layout, layerHandles);
                        result.warnings.push_back("VIEWPORT "+record.value(5)+": Saved view retained; paper-space rendering is not supported yet");
                        continue;
                    }
                    if (record.integer(67) != 0) throw std::invalid_argument("Paper-space entities are not supported yet");
                    const auto found = blocks.find(record.value(2));
                    auto native = entity(record, record.value(0) == "INSERT" ? insertContext :
                        record.value(0) == "DIMENSION" && found != blocks.end() ? found->second : "");
                    native->geometry(); // Validate before document/UI mutation.
                    result.entities.push_back(std::move(native));
                    result.slots_.emplace_back(begin, i);
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

DXFDocument DXFDocument::empty(int units)
{
    if (units < 0 || units > 24) throw std::invalid_argument("Invalid DXF insertion units");
    return parse("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n"+
        std::to_string(units)+"\n9\n$HANDSEED\n5\n1\n0\nENDSEC\n0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n1\n"
        "0\nLAYER\n2\n0\n70\n0\n62\n7\n6\nCONTINUOUS\n0\nENDTAB\n0\nENDSEC\n"
        "0\nSECTION\n2\nENTITIES\n0\nENDSEC\n0\nEOF\n");
}

std::shared_ptr<DXFEntity> DXFDocument::resolveEntity(const std::string& raw) const
{
    if (nativeFields(groups(raw)).value(0)!="INSERT") return parseEntity(raw);
    DXFRecord context;
    bool inBlocks=false;
    for (std::size_t i=0;i<source_.groups.size();) {
        DXFRecord record;
        record.groups.push_back(source_.groups[i++]);
        while (i<source_.groups.size() && source_.groups[i].first!=0) record.groups.push_back(source_.groups[i++]);
        if (record.value(0)=="SECTION") { inBlocks=record.value(2)=="BLOCKS"; continue; }
        if (record.value(0)=="ENDSEC") { inBlocks=false; continue; }
        if (inBlocks) context.groups.insert(context.groups.end(),record.groups.begin(),record.groups.end());
    }
    auto result=parseEntity(raw,context.raw());
    result->geometry();
    return result;
}

std::string DXFDocument::serialize(const std::map<std::size_t, std::string>& replacements,
                                  const std::vector<std::size_t>& removed,
                                  const std::vector<std::string>& added) const
{
    if (source_.groups.empty()) throw std::invalid_argument("Use DXFDocument::empty() to create a drawing");
    std::set<std::size_t> deleted(removed.begin(), removed.end());
    for (auto index : deleted) if (index >= slots_.size()) throw std::out_of_range("Removed entity index");
    for (const auto& item : replacements) {
        if (item.first >= slots_.size() || deleted.count(item.first)) throw std::invalid_argument("Invalid replacement index");
        const auto replacement = parseEntity(item.second, entities.at(item.first)->context());
        if (replacement->type() == "DIMENSION" && item.second != entities.at(item.first)->raw())
            throw std::invalid_argument("DIMENSION picture regeneration is not supported; retain its native record");
        replacement->geometry();
        // Handles and ownership are document identity, not editable geometry.
        const auto& original = entities.at(item.first)->record();
        if (replacement->type() == "LWPOLYLINE" && item.second != original.raw()
            && readLWPolyline(item.second).vertices().size() < 2)
            throw std::invalid_argument("Export requires at least two polyline vertices");
        if (replacement->type() != original.value(0) || replacement->handle() != original.value(5)
            || replacement->record().value(330) != original.value(330))
            throw std::invalid_argument("Replacement must retain entity type, handle, and owner");
    }
    std::set<std::string> handles;
    for (const auto& g : source_.groups) if (g.first == 5 || g.first == 105) {
        auto handle = g.second;
        std::transform(handle.begin(), handle.end(), handle.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        handles.insert(handle);
    }
    unsigned long long next = 1;
    auto hex = [](unsigned long long n) { std::ostringstream out; out << std::uppercase << std::hex << n; return out.str(); };
    if (!added.empty()) for (const auto& handle : handles) {
        try {
            std::size_t count;
            const auto n = std::stoull(handle, &count, 16);
            if (count == handle.size() && n >= next) {
                if (n == std::numeric_limits<unsigned long long>::max()) throw std::overflow_error("DXF handle space exhausted");
                next = n+1;
            }
        } catch (const std::invalid_argument&) { } catch (const std::out_of_range&) { }
    }
    std::optional<std::size_t> handseedIndex;
    std::string version, section;
    for (std::size_t i = 0; i+1 < source_.groups.size(); ++i) {
        const auto& g = source_.groups[i];
        if (g.first == 0 && g.second == "SECTION") section = source_.groups[i+1].second;
        if (g.first == 0 && g.second == "ENDSEC") section.clear();
        if (section == "HEADER" && g.first == 9) {
            if (g.second == "$HANDSEED" && source_.groups[i+1].first == 5) handseedIndex = i+1;
            if (g.second == "$ACADVER") version = source_.groups[i+1].second;
        }
    }
    std::string additions;
    for (const auto& raw : added) {
        const auto e = resolveEntity(raw);
        if (e->type() != "LWPOLYLINE" && e->type() != "LINE" && e->type() != "CIRCLE" && e->type() != "ARC" && e->type() != "TEXT" && e->type() != "MTEXT" && e->type() != "INSERT") throw std::invalid_argument("Unsupported new entity type");
        if (e->type() == "LWPOLYLINE" && readLWPolyline(raw).vertices().size() < 2) throw std::invalid_argument("Export requires at least two polyline vertices");
        if (e->type() == "LINE") {
            const auto fields = nativeFields(e->record());
            createLine(point(fields,10),point(fields,11),e->layer());
        }
        e->geometry();
        if (e->record().integer(67) || !e->handle().empty() || !e->record().value(330).empty())
            throw std::invalid_argument("New entities must be model-space records without handles or owners");
        if (!layers.count(e->layer())) throw std::invalid_argument("New entity layer is absent from the source drawing");
        if (next == std::numeric_limits<unsigned long long>::max()) throw std::overflow_error("DXF handle space exhausted");
        auto record = e->record();
        record.groups.insert(record.groups.begin()+1, {5, hex(next++)});
        additions += record.raw();
    }
    // LWPOLYLINE requires R2000 or later for this writer's output contract.
    if (!added.empty() && !version.empty() && version < "AC1015")
        throw std::invalid_argument("Adding native entities requires an R2000 or later drawing");
    std::map<std::size_t, std::size_t> slotAt;
    for (std::size_t i = 0; i < slots_.size(); ++i) slotAt[slots_[i].first] = i;
    std::ostringstream output;
    for (std::size_t i = 0; i < source_.groups.size();) {
        if (i == entitiesEnd_) output << additions;
        const auto found = slotAt.find(i);
        if (found != slotAt.end()) {
            const auto index = found->second;
            if (deleted.count(index)) { i = slots_[index].second; continue; }
            const auto replacement = replacements.find(index);
            if (replacement != replacements.end()) { output << replacement->second; i = slots_[index].second; continue; }
        }
        const auto& g = source_.groups[i];
        output << g.first << '\n';
        output << (handseedIndex && i == *handseedIndex && !added.empty() ? hex(next) : g.second) << '\n';
        ++i;
    }
    return output.str();
}

std::string DXFDocument::serializeCurrent(const std::vector<std::pair<int, std::string>>& records) const
{
    std::map<std::size_t,std::string> replacements;
    std::vector<std::string> additions;
    for (const auto& [index,raw] : records) {
        if (index == -1) additions.push_back(raw);
        else {
            if (index < 0 || static_cast<std::size_t>(index) >= entities.size())
                throw std::invalid_argument("Drawing has an invalid source entity index");
            if (!replacements.emplace(static_cast<std::size_t>(index),raw).second)
                throw std::invalid_argument("Drawing has duplicate source entity indices");
        }
    }
    std::vector<std::size_t> removed;
    for (std::size_t i=0;i<entities.size();++i)
        if (!replacements.count(i)) removed.push_back(i);
    return serialize(replacements,removed,additions);
}

void DXFDocument::write(const std::string& path, const std::map<std::size_t, std::string>& replacements,
                        const std::vector<std::size_t>& removed, const std::vector<std::string>& added) const
{
    const auto text = serialize(replacements, removed, added); // validate before touching the file
    std::ofstream output(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot write DXF file: "+path);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    if (!output) throw std::runtime_error("Failed writing DXF file: "+path);
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
