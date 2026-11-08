#include "Entities/EntityCoordinates.h"
#include "Entities/DXFLine.h"
#include "Entities/DXFCircle.h"
#include "Entities/DXFArc.h"
#include "Entities/DXFText.h"
#include "Entities/DXFMText.h"
#include "Entities/DXFDimension.h"
#include <DXFCAD/Drawing.h>
#include <DXFCAD/ParametricCurves.h>
#include <DXFCAD/DXFPolyline.h>
#include <DXFCAD/SnapGeometry.h>
#include <DXFCAD/Drafting.h>
#include <DXFCAD/DXFLeader.h>
#include <DXFCAD/CartesianFrame.h>

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
std::shared_ptr<DXFEntity> makeInsertEntity(GroupSequence record, const std::string& context);
namespace {
using Detail::numeric;
using Detail::extrusion;
// Display dependency caches cross Unicode-only FreeCAD/Python string APIs.
// Decode declared pre-2007 Windows-1252 resources there; source_ and native
// entity records stay byte-for-byte authoritative for DXF export.
std::string displayContext(const std::string& context,const GroupSequence& source)
{
    if (std::none_of(context.begin(),context.end(),[](unsigned char c) { return c>=0x80; })) return context;
    std::string version,codepage;
    bool header=false;
    for (std::size_t i=0;i+1<source.groups.size();++i) {
        const auto& group=source.groups[i];
        if (group.first==0 && group.second=="SECTION") header=source.groups[i+1]==std::make_pair(2,std::string("HEADER"));
        if (group.first==0 && group.second=="ENDSEC" && header) break;
        if (!header || group.first!=9) continue;
        if (group.second=="$ACADVER") version=source.groups[i+1].second;
        if (group.second=="$DWGCODEPAGE") codepage=source.groups[i+1].second;
    }
    if (codepage!="ANSI_1252" || (!version.empty() && version>="AC1021")) return context;
    constexpr unsigned special[]={0x20ac,0x81,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,
        0x2c6,0x2030,0x160,0x2039,0x152,0x8d,0x17d,0x8f,
        0x90,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,
        0x2dc,0x2122,0x161,0x203a,0x153,0x9d,0x17e,0x178};
    std::string result;
    for (unsigned char c : context) {
        const unsigned cp=c>=0x80 && c<0xa0 ? special[c-0x80] : c;
        if (cp<0x80) result+=static_cast<char>(cp);
        else if (cp<0x800) {
            result+=static_cast<char>(0xc0|(cp>>6)); result+=static_cast<char>(0x80|(cp&63));
        } else {
            result+=static_cast<char>(0xe0|(cp>>12)); result+=static_cast<char>(0x80|((cp>>6)&63));
            result+=static_cast<char>(0x80|(cp&63));
        }
    }
    return result;
}
using Detail::point;
using Detail::radius;
std::string trim(const std::string& s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? "" : s.substr(first, s.find_last_not_of(" \t\r\n")-first+1);
}
int integral(const std::string& s)
{
    const double value = numeric(s);
    if (value != std::trunc(value) || value < std::numeric_limits<int>::min()
        || value > std::numeric_limits<int>::max()) throw std::invalid_argument("Invalid DXF integer");
    return static_cast<int>(value);
}
GroupSequence groups(const std::string& ascii)
{
    if (ascii.find("AutoCAD Binary DXF") == 0) throw std::invalid_argument("Binary DXF is not supported");
    std::istringstream input(ascii);
    std::string code, value;
    GroupSequence result;
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
// Application-defined 102 groups and XDATA are opaque, even if they contain
// codes also used by ordinary entity geometry.
GroupSequence nativeFields(const GroupSequence& record)
{
    GroupSequence result;
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
std::string patchedStrings(const GroupSequence& record, const std::map<int,std::string>& formatted, const std::set<int>& removed = {})
{
    if (record.value(0)=="POLYLINE") {
        auto at=record.groups.begin()+1;
        while (at!=record.groups.end() && at->first!=0) ++at;
        if (at!=record.groups.end()) {
            const GroupSequence header{{record.groups.begin(),at}},children{{at,record.groups.end()}};
            return patchedStrings(header,formatted,removed)+children.raw();
        }
    }
    GroupSequence result;
    std::set<int> written;
    auto missing = [&] { for (const auto& g : formatted) if (!written.count(g.first)) {
        result.groups.push_back(g); written.insert(g.first);
    }};
    const auto fields=nativeFields(record);
    const auto type=fields.value(0);
    const bool entityRecord=type=="LINE" || type=="ARC" || type=="CIRCLE" || type=="LWPOLYLINE" || type=="TEXT" || type=="MTEXT" || type=="INSERT" || type=="ELLIPSE" || type=="SPLINE";
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
std::string patched(const GroupSequence& record, const std::map<int,double>& values)
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

std::shared_ptr<DXFEntity> entity(GroupSequence record, const std::string& context = "")
{
    const auto type = record.value(0);
    if (type == "LWPOLYLINE") return std::make_shared<DXFLWPolyline>(std::move(record));
    if (type == "POLYLINE") return std::make_shared<DXFPolyline>(std::move(record));
    if (type == "ELLIPSE") return std::make_shared<DXFEllipse>(std::move(record));
    if (type == "SPLINE") return std::make_shared<DXFSpline>(std::move(record));
    if (type == "LINE") return std::make_shared<Detail::DXFLine>(std::move(record));
    if (type == "ARC") return std::make_shared<Detail::DXFArc>(std::move(record));
    if (type == "CIRCLE") return std::make_shared<Detail::DXFCircle>(std::move(record));
    if (type == "TEXT") return std::make_shared<Detail::DXFText>(std::move(record));
    if (type == "MTEXT") return std::make_shared<Detail::DXFMText>(std::move(record));
    if (type == "DIMENSION") return std::make_shared<Detail::DXFDimension>(std::move(record), context);
    if (type == "LEADER") return std::make_shared<DXFLeader>(std::move(record), context);
    if (type == "INSERT") return makeInsertEntity(std::move(record), context);
    throw std::invalid_argument("Unsupported DXF entity " + type);
}
}

GroupSequence readDXFGroups(const std::string& ascii) { return groups(ascii); }
GroupSequence nativeEntityFields(const GroupSequence& record) { return nativeFields(record); }
std::string patchEntityFields(const GroupSequence& record, const std::map<int,double>& values) { return patched(record,values); }

std::string patchEntityStringFields(const GroupSequence& record, const std::map<int,std::string>& values, const std::set<int>& removed) {
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

std::string GroupSequence::value(int code, const std::string& fallback) const
{
    for (const auto& group : groups) if (group.first == code) return group.second;
    return fallback;
}

DXFViewport::DXFViewport(GroupSequence record, std::string layout,
                         const std::map<std::string,std::string>& layerHandles)
    : record_(std::move(record)), layout_(std::move(layout))
{
    const auto fields = nativeFields(record_);
    if (fields.value(0) != "VIEWPORT" ||
        (fields.integer(67)!=1 && (!fields.value(67).empty() || layout_.empty())))
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
ViewFrame DXFViewport::modelView() const
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
    if (type()=="POLYLINE") throw std::invalid_argument("Compound POLYLINE requires per-vertex native transformation; explode it first");
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
    if (type()=="ELLIPSE" || type()=="SPLINE") {
        return affineTransformedCurve(*this,{{},world({0,0,0}),reflect({1,0,0}),reflect({0,1,0}),reflect({0,0,1})});
    }
    const auto oldNormal=extrusion(fields);
    auto normal=reflect({oldNormal.x,oldNormal.y,oldNormal.z});
    // A reflected frame changes handedness. Reversing its normal preserves
    // arc/bulge parameter direction and reflects both axes of text outlines.
    if (type()!="LINE") normal={-normal.x,-normal.y,-normal.z};
    const CartesianFrame oldFrame(oldNormal), frame({normal.x,normal.y,normal.z});
    auto ocs = [&](Vector3D p) { return frame.fromWorld(world(oldFrame.toWorld({p.x,p.y},p.z))); };
    if (type()=="LWPOLYLINE") {
        const auto previous=readLWPolyline(raw());
        auto vertices=previous.vertices();
        double elevation=previous.elevation();
        for (auto& v : vertices) { const auto p=ocs({v.x,v.y,previous.elevation()}); v.x=p.x; v.y=p.y; elevation=p.z; }
        auto result=updateLWPolyline(raw(),DXFLWPolyline(vertices,previous.closed(),previous.constantWidth(),elevation,{normal.x,normal.y,normal.z}));
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
    if (type()=="POLYLINE") throw std::invalid_argument("Compound POLYLINE requires per-vertex native transformation; explode it first");
    if (!std::isfinite(factor) || factor<=0) throw std::invalid_argument("Scale factor must be positive and finite");
    finitePoint(pivot);
    translated({0,0,0});
    if (factor==1) return raw();
    const auto fields=nativeFields(record_);
    const CartesianFrame frame(extrusion(fields));
    auto world = [&](const Vector3D& p) {
        Vector3D result{pivot.x+(p.x-pivot.x)*factor,pivot.y+(p.y-pivot.y)*factor,pivot.z+(p.z-pivot.z)*factor};
        finitePoint(result); return result;
    };
    auto ocs = [&](const Vector3D& p) { return frame.fromWorld(world(frame.toWorld({p.x,p.y},p.z))); };
    if (type()=="ELLIPSE" || type()=="SPLINE") {
        return affineTransformedCurve(*this,{{},world({0,0,0}),{factor,0,0},{0,factor,0},{0,0,factor}});
    }
    if (type()=="LWPOLYLINE") {
        const auto previous=readLWPolyline(raw());
        const auto center=frame.fromWorld(pivot);
        auto model=previous.scaledInPlane(factor,{center.x,center.y});
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
    if (type()=="POLYLINE") throw std::invalid_argument("Compound POLYLINE requires per-vertex native transformation; explode it first");
    if (!std::isfinite(degrees)) throw std::invalid_argument("Rotation angle must be finite");
    finitePoint(pivot);
    translated({0,0,0}); // Read-only entities cannot be transformed.
    const auto fields = nativeFields(record_);
    const double pi=std::acos(-1.0), angle=std::fmod(degrees,360)*pi/180;
    if (angle == 0) return raw();
    const auto oldNormal=extrusion(fields);
    const auto normal=rotateVector({oldNormal.x,oldNormal.y,oldNormal.z},angle);
    const CartesianFrame oldFrame(oldNormal), frame({normal.x,normal.y,normal.z});
    auto world = [&](const Vector3D& p) {
        auto v=rotateVector({p.x-pivot.x,p.y-pivot.y,p.z-pivot.z},angle);
        Vector3D result{v.x+pivot.x,v.y+pivot.y,p.z}; finitePoint(result); return result;
    };
    auto ocs = [&](const Vector3D& p) { return frame.fromWorld(world(oldFrame.toWorld({p.x,p.y},p.z))); };
    if (type()=="ELLIPSE" || type()=="SPLINE") {
        return affineTransformedCurve(*this,{{},world({0,0,0}),rotateVector({1,0,0},angle),rotateVector({0,1,0},angle),{0,0,1}});
    }
    if (type()=="LWPOLYLINE") {
        const auto previous=readLWPolyline(raw());
        auto vertices=previous.vertices();
        double elevation=previous.elevation();
        for (auto& vertex : vertices) {
            const auto p=ocs({vertex.x,vertex.y,previous.elevation()});
            vertex.x=p.x; vertex.y=p.y; elevation=p.z;
        }
        return updateLWPolyline(raw(),DXFLWPolyline(vertices,previous.closed(),previous.constantWidth(),elevation,{normal.x,normal.y,normal.z}));
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
    GroupSequence result;
    int depth = 0, skipDepth = 0;
    bool xdata = false;
    for (const auto& g : source.groups) {
        if (g.first == 0) { xdata=false; depth=0; skipDepth=0; }
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
    const auto delta = ocs ? CartesianFrame(extrusion(fields)).fromWorld(offset) : offset;
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
    GroupSequence result;
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

std::vector<SnapPoint> DXFEntity::snapPoints() const
{
    const auto derived = geometry();
    std::vector<SnapPoint> result;
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
double GroupSequence::number(int code, double fallback) const
{
    const auto text = value(code);
    return text.empty() ? fallback : numeric(text);
}
int GroupSequence::integer(int code, int fallback) const
{
    const auto text = value(code);
    return text.empty() ? fallback : integral(text);
}
std::string GroupSequence::raw() const
{
    std::ostringstream result;
    for (const auto& [code, value] : groups) result << code << '\n' << value << '\n';
    return result.str();
}
std::shared_ptr<DXFEntity> parseEntity(const std::string& raw, const std::string& context)
{
    auto record = groups(raw);
    if (record.groups.empty() || record.groups.front().first != 0) throw std::invalid_argument("Missing DXF entity type");
    if (record.value(0)!="POLYLINE") for (std::size_t i = 1; i < record.groups.size(); ++i)
        if (record.groups[i].first == 0) throw std::invalid_argument("Expected exactly one DXF entity");
    return entity(std::move(record), context);
}
DXFLWPolyline readLWPolyline(const std::string& raw)
{
    return DXFLWPolyline::fromRaw(raw);
}

std::string updateLWPolyline(const std::string& raw, const DXFLWPolyline& polyline,
                             const std::vector<int>& origins)
{
    const auto result = DXFLWPolyline::fromRaw(raw).withGeometry(polyline, origins);
    result.geometry();
    return result.raw();
}

std::string createLWPolyline(const DXFLWPolyline& p, const std::string& layer)
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
    return createLWPolyline(DXFLWPolyline({{first.x,first.y},{b.x,first.y},{b.x,b.y},{first.x,b.y}},true,0,first.z),layer);
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
    const auto p = CartesianFrame({0,0,1}).intersectLine(start,end,elevation);
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

Drawing Drawing::read(const std::string& path)
{
    std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open DXF file: " + path);
    std::ostringstream text; text << input.rdbuf();
    return parse(text.str());
}
Drawing Drawing::parse(const std::string& ascii)
{
    const auto all = groups(ascii);
    Drawing result;
    result.source_ = all;
    result.layers.emplace("0", DXFLayer{"0"});
    std::map<std::string,std::string> layoutNames, ownerLayouts, layerHandles;
    const auto handleKey=[](std::string value) {
        value=trim(value);
        std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return value;
    };
    std::string metadataSection;
    // LAYOUT names belong to AcDbLayout, not the preceding plot settings.
    for (std::size_t i = 0; i < all.groups.size();) {
        GroupSequence record;
        record.groups.push_back(all.groups[i++]);
        while (i < all.groups.size() && all.groups[i].first != 0) record.groups.push_back(all.groups[i++]);
        const auto fields = nativeFields(record);
        if (fields.value(0) == "SECTION") metadataSection = fields.value(2);
        if (fields.value(0) == "ENDSEC") metadataSection.clear();
        if (metadataSection == "OBJECTS" && fields.value(0) == "LAYOUT") {
            bool layoutSubclass = false;
            for (const auto& [code,value] : fields.groups) {
                if (code == 100) layoutSubclass = value == "AcDbLayout";
                if (code == 1 && layoutSubclass) layoutNames[handleKey(fields.value(5))] = value;
            }
            // ModelType belongs to AcDbPlotSettings, whose group codes can
            // overlap those of AcDbLayout. Never read subclass fields globally.
            bool plotSubclass=false,model=false;
            for (const auto& group : fields.groups) {
                if (group.first==100) plotSubclass=group.second=="AcDbPlotSettings";
                if (plotSubclass && group.first==70) model=(integral(group.second)&1024)!=0;
            }
            const auto name=layoutNames.find(handleKey(fields.value(5)));
            if (!model && name!=layoutNames.end() && name->second!="Model") {
                try { result.layouts.emplace_back(record); }
                catch (const std::exception& error) {
                    result.warnings.push_back("LAYOUT "+fields.value(5)+": "+error.what());
                }
            }
        }
        if (metadataSection == "TABLES" && fields.value(0) == "BLOCK_RECORD") ownerLayouts[handleKey(fields.value(5))] = handleKey(fields.value(340));
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
        GroupSequence block;
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
    for (std::size_t j=0; j<all.groups.size(); ++j) if (all.groups[j]==std::make_pair(0,std::string("LAYER"))) {
        GroupSequence layer;
        layer.groups.push_back(all.groups[j]);
        while (j+1<all.groups.size() && all.groups[j+1].first!=0) layer.groups.push_back(all.groups[++j]);
        insertContext+=layer.raw();
    }
    std::map<std::string,std::string> leaderStyles;
    for (std::size_t j=0; j<all.groups.size(); ++j) if (all.groups[j]==std::make_pair(0,std::string("DIMSTYLE"))) {
        GroupSequence style;
        style.groups.push_back(all.groups[j]);
        while (j+1<all.groups.size() && all.groups[j+1].first!=0) style.groups.push_back(all.groups[++j]);
        leaderStyles[nativeFields(style).value(2)]=style.raw();
    }
    const auto paperLayout=[&](const std::string& name) -> DXFLayout& {
        const std::string resolved=name.empty() ? "Paper space" : name;
        for (auto& layout : result.layouts) if (layout.name()==resolved) return layout;
        result.layouts.emplace_back(GroupSequence{},resolved);
        return result.layouts.back();
    };
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
            GroupSequence record;
            record.groups.push_back(all.groups[i++]);
            while (i < all.groups.size() && all.groups[i].first != 0) record.groups.push_back(all.groups[i++]);
            if (section=="ENTITIES" && value=="POLYLINE") {
                // A compound polyline owns its VERTEX/SEQEND sequence. Its
                // complete source range is one stable document slot.
                while (i<all.groups.size() && all.groups[i].first==0 &&
                       (all.groups[i].second=="VERTEX" || all.groups[i].second=="SEQEND")) {
                    const bool last=all.groups[i].second=="SEQEND";
                    record.groups.push_back(all.groups[i++]);
                    while (i<all.groups.size() && all.groups[i].first!=0) record.groups.push_back(all.groups[i++]);
                    if (last) break;
                }
            }
            if (section == "TABLES") {
                const int color = record.integer(62, 7);
                DXFLayer layer{record.value(2), std::abs(color), record.integer(420, -1),
                               color >= 0 && !(record.integer(70)&1)};
                result.layers[layer.name] = layer;
            } else {
                try {
                    const auto fields = nativeFields(record);
                    std::string layout = fields.value(410);
                    const auto owner = ownerLayouts.find(handleKey(fields.value(330)));
                    if (layout.empty() && owner != ownerLayouts.end()) {
                        const auto name = layoutNames.find(owner->second);
                        if (name != layoutNames.end()) layout = name->second;
                    }
                    const bool paper=fields.integer(67)==1 || (!layout.empty() && layout!="Model");
                    if (record.value(0) == "VIEWPORT" && paper) {
                        result.viewports.emplace_back(record, layout, layerHandles);
                        paperLayout(layout).viewports_.push_back(result.viewports.back());
                        const auto issue=result.viewports.back().navigationIssue();
                        if (!issue.empty() && result.viewports.back().id()!=1)
                            result.warnings.push_back("VIEWPORT "+record.value(5)+": "+issue+"; native record retained");
                        continue;
                    }
                    const auto found = blocks.find(record.value(2));
                    const auto context=record.value(0) == "LEADER" ? leaderStyles[fields.value(3,"Standard")] :
                        record.value(0) == "INSERT" ? insertContext :
                        record.value(0) == "DIMENSION" && found != blocks.end() ? found->second : "";
                    auto native = entity(record,displayContext(context,all));
                    native->geometry(); // Validate before document/UI mutation.
                    if (paper) {
                        paperLayout(layout).entities_.push_back(std::move(native));
                        continue;
                    }
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
    std::stable_sort(result.layouts.begin(),result.layouts.end(),[](const DXFLayout& a,const DXFLayout& b) {
        return a.tabOrder()<b.tabOrder();
    });
    return result;
}

Drawing Drawing::empty(int units)
{
    if (units < 0 || units > 24) throw std::invalid_argument("Invalid DXF insertion units");
    return parse("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n"+
        std::to_string(units)+"\n9\n$HANDSEED\n5\n1\n0\nENDSEC\n0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n1\n"
        "0\nLAYER\n2\n0\n70\n0\n62\n7\n6\nCONTINUOUS\n0\nENDTAB\n0\nENDSEC\n"
        "0\nSECTION\n2\nENTITIES\n0\nENDSEC\n0\nEOF\n");
}

std::shared_ptr<DXFEntity> Drawing::resolveEntity(const std::string& raw) const
{
    const auto fields=nativeFields(groups(raw));
    if (fields.value(0)!="INSERT" && fields.value(0)!="LEADER") return parseEntity(raw);
    GroupSequence context;
    bool inBlocks=false;
    for (std::size_t i=0;i<source_.groups.size();) {
        GroupSequence record;
        record.groups.push_back(source_.groups[i++]);
        while (i<source_.groups.size() && source_.groups[i].first!=0) record.groups.push_back(source_.groups[i++]);
        if (record.value(0)=="SECTION") { inBlocks=record.value(2)=="BLOCKS"; continue; }
        if (record.value(0)=="ENDSEC") { inBlocks=false; continue; }
        if (fields.value(0)=="LEADER" && record.value(0)=="DIMSTYLE" && nativeFields(record).value(2)==fields.value(3,"Standard"))
            return parseEntity(raw,displayContext(record.raw(),source_));
        if (inBlocks || record.value(0)=="LAYER") context.groups.insert(context.groups.end(),record.groups.begin(),record.groups.end());
    }
    auto result=parseEntity(raw,fields.value(0)=="LEADER" ? "" : displayContext(context.raw(),source_));
    result->geometry();
    return result;
}

std::string Drawing::serialize(const std::map<std::size_t, std::string>& replacements,
                                  const std::vector<std::size_t>& removed,
                                  const std::vector<std::string>& added) const
{
    if (source_.groups.empty()) throw std::invalid_argument("Use Drawing::empty() to create a drawing");
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
        if (e->type() != "LWPOLYLINE" && e->type() != "LINE" && e->type() != "CIRCLE" && e->type() != "ARC" && e->type() != "TEXT" && e->type() != "MTEXT" && e->type() != "INSERT" && e->type() != "ELLIPSE" && e->type() != "SPLINE") throw std::invalid_argument("Unsupported new entity type");
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

std::string Drawing::serializeCurrent(const std::vector<std::pair<int, std::string>>& records) const
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

void Drawing::write(const std::string& path, const std::map<std::size_t, std::string>& replacements,
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
    const auto* leader=dynamic_cast<const DXFLeader*>(&e);
    const int index = leader ? leader->colorIndex() : e.record().integer(62, 256);
    const int rgb = leader && !leader->context().empty() ? (index==256 ? layer.trueColor : -1) :
        e.record().integer(420, index == 256 ? layer.trueColor : -1);
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
