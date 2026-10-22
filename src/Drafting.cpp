#include <DXFCAD/Drafting.h>
#include <cmath>
#include <algorithm>
#include <limits>
#include <sstream>
#include <cctype>
#include <stdexcept>
namespace DXFCAD {
namespace {
void positiveFinite(double value, const char* message) {
    if (!std::isfinite(value) || value<=0) throw std::invalid_argument(message);
}

}
namespace {
struct Corner {
    LWPolyline model;
    std::size_t previous, next;
    double ux,uy,vx,vy,la,lb,theta,turn;
};
Corner corner(const std::string& raw,std::size_t index) {
    auto model=readLWPolyline(raw);
    model.worldSegments();
    const auto& vertices=model.vertices(); const auto n=vertices.size();
    if (index>=n || n<3 || (!model.closed() && (index==0 || index==n-1)))
        throw std::invalid_argument("Choose an interior polyline corner");
    const auto previous=(index+n-1)%n, next=(index+1)%n;
    const auto a=vertices[previous], b=vertices[index], c=vertices[next];
    if (a.bulge!=0 || b.bulge!=0) throw std::invalid_argument("Corner editing requires adjacent straight segments");
    const double la=std::hypot(a.x-b.x,a.y-b.y), lb=std::hypot(c.x-b.x,c.y-b.y);
    positiveFinite(la,"Corner has a zero-length or unbounded segment"); positiveFinite(lb,"Corner has a zero-length or unbounded segment");
    const double ux=(a.x-b.x)/la, uy=(a.y-b.y)/la, vx=(c.x-b.x)/lb, vy=(c.y-b.y)/lb;
    const double turn=-ux*vy+uy*vx;
    if (std::abs(turn)<1e-12) throw std::invalid_argument("Corner segments are collinear");
    return {model,previous,next,ux,uy,vx,vy,la,lb,std::acos(std::clamp(ux*vx+uy*vy,-1.0,1.0)),turn};
}
std::string replaceCorner(const std::string& raw,std::size_t index,const Corner& c,double da,double db,double bulge) {
    positiveFinite(da,"Corner distance must be positive and finite"); positiveFinite(db,"Corner distance must be positive and finite");
    if (da>=c.la-1e-9 || db>=c.lb-1e-9) throw std::invalid_argument("Corner distances must fit within adjacent segments");
    auto vertices=c.model.vertices();
    auto& previous=vertices[c.previous]; const auto original=vertices[index];
    auto variable=[](const LWPolylineVertex& v) { return v.hasWidths || v.startWidth!=0 || v.endWidth!=0; };
    const bool pv=variable(previous), ov=variable(original);
    const double width=c.model.constantWidth();
    const double inStart=pv ? previous.startWidth : width, inEnd=pv ? previous.endWidth : width;
    const double outStart=ov ? original.startWidth : width, outEnd=ov ? original.endWidth : width;
    const double inWidth=inStart+(inEnd-inStart)*(1-da/c.la), outWidth=outStart+(outEnd-outStart)*db/c.lb;
    if (pv) previous.endWidth=inWidth;
    auto a=original, b=original;
    a.x+=c.ux*da; a.y+=c.uy*da; a.bulge=bulge;
    a.hasWidths=pv || ov;
    a.startWidth=a.hasWidths ? inWidth : 0; a.endWidth=a.hasWidths ? outWidth : 0;
    b.x+=c.vx*db; b.y+=c.vy*db;
    if (ov) b.startWidth=outWidth;
    vertices[index]=a;
    vertices.insert(vertices.begin()+index+1,b);
    std::vector<int> origins;
    for (std::size_t i=0; i<c.model.vertices().size(); ++i) origins.push_back(static_cast<int>(i));
    origins.insert(origins.begin()+index+1,-1);
    return updateLWPolyline(raw,LWPolyline(vertices,c.model.closed(),width,c.model.elevation(),c.model.extrusion()),origins);
}
}
std::string Drafting::properties(const std::string& raw,const std::string& layer,int color,bool visible,const std::string& context) {
    const auto entity=parseEntity(raw,context);
    entity->translated({0,0,0});
    if (layer.empty() || layer.find_first_of("\r\n")!=std::string::npos) throw std::invalid_argument("Layer name must be nonempty and contain no newlines");
    if (color < -1 || color>256) throw std::invalid_argument("Entity ACI color must be 0..256, or -1 to retain its color");
    std::map<int,std::string> values{{8,layer},{60,visible ? "0" : "1"}};
    if (color>=0) values[62]=std::to_string(color);
    return patchEntityStringFields(entity->record(),values,color>=0 ? std::set<int>{420,430} : std::set<int>{});
}
std::string Drafting::setLayer(const std::string& drawing,const std::string& name,int color,bool on) {
    if (name.empty() || name.find_first_of("\r\n")!=std::string::npos) throw std::invalid_argument("Invalid layer name");
    if (color<1 || color>255) throw std::invalid_argument("Layer ACI color must be between 1 and 255");
    DXFDocument::parse(drawing);
    const auto all=readDXFGroups(drawing);
    if (all.groups.empty()) throw std::invalid_argument("Drawing is empty");
    std::vector<DXFRecord> records;
    for (const auto& g : all.groups) {
        if (g.first==0 || records.empty()) records.emplace_back();
        records.back().groups.push_back(g);
    }
    const auto absent=std::numeric_limits<std::size_t>::max();
    std::size_t table=absent, tableEnd=absent, tablesEnd=absent, entitiesBegin=absent, found=absent;
    int count=0; std::string section, version;
    for (std::size_t i=0; i<all.groups.size(); ++i)
        if (all.groups[i].first==9 && all.groups[i].second=="$ACADVER" && i+1<all.groups.size()) version=all.groups[i+1].second;
    bool inLayer=false;
    for (std::size_t i=0; i<records.size(); ++i) {
        const auto fields=nativeEntityFields(records[i]); const auto type=fields.value(0);
        if (type=="SECTION") { section=fields.value(2); if (section=="ENTITIES") entitiesBegin=i; }
        if (section=="TABLES" && type=="TABLE" && fields.value(2)=="LAYER") { table=i; inLayer=true; }
        if (section=="TABLES" && inLayer && type=="LAYER") { ++count; if (fields.value(2)==name) found=i; }
        if (inLayer && type=="ENDTAB") { tableEnd=i; inLayer=false; }
        if (type=="ENDSEC") { if (section=="TABLES") tablesEnd=i; section.clear(); }
    }
    if (found!=absent) {
        const auto fields=nativeEntityFields(records[found]);
        const int flags=on ? fields.integer(70)&~1 : fields.integer(70);
        records[found]=readDXFGroups(patchEntityStringFields(records[found],{{62,std::to_string(on ? color : -color)},{70,std::to_string(flags)}},{420,430}));
    } else {
        if (!version.empty() && version<"AC1015") throw std::invalid_argument("New layers require R2000 or later");
        if (name.size()>255 || name.find_first_not_of(" ")==std::string::npos) throw std::invalid_argument("Invalid new layer name");
        for (unsigned char c : name) if (c<128 && !(std::isalnum(c) || c==' ' || c=='_' || c=='-' || c=='.' || c=='$'))
            throw std::invalid_argument("New layer names allow letters, digits, spaces, underscore, hyphen, dot, dollar sign, and Unicode");
        auto upper=[](std::string value) { for (auto& c : value) c=static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return value; };
        for (const auto& [existing,layer] : DXFDocument::parse(drawing).layers)
            if (existing!=name && upper(existing)==upper(name)) throw std::invalid_argument("Layer already exists as "+existing+"; choose that spelling");
        unsigned long long next=1;
        for (const auto& g : all.groups) if (g.first==5 || g.first==105) {
            try {
                std::size_t used=0; const auto n=std::stoull(g.second,&used,16);
                if (used==g.second.size() && n>=next) {
                    if (n==std::numeric_limits<unsigned long long>::max()) throw std::overflow_error("DXF handle space exhausted");
                    next=n+1;
                }
            } catch (const std::invalid_argument&) {} catch (const std::out_of_range&) {}
        }
        auto hex=[](unsigned long long n) { std::ostringstream out; out<<std::uppercase<<std::hex<<n; return out.str(); };
        auto take=[&] { if (next==std::numeric_limits<unsigned long long>::max()) throw std::overflow_error("DXF handle space exhausted"); return hex(next++); };
        auto makeLayer=[&](const std::string& layerName,int layerColor,bool layerOn,const std::string& owner) {
            DXFRecord result;
            result.groups={{0,"LAYER"},{5,take()}};
            if (!owner.empty()) result.groups.emplace_back(330,owner);
            for (const auto& g : DXFRecord{{{100,"AcDbSymbolTableRecord"},{100,"AcDbLayerTableRecord"},{2,layerName},{70,"0"},{62,std::to_string(layerOn ? layerColor : -layerColor)},{6,"CONTINUOUS"}}}.groups) result.groups.push_back(g);
            return result;
        };
        if (table!=absent && tableEnd!=absent) {
            const auto owner=nativeEntityFields(records[table]).value(5);
            records[table]=readDXFGroups(patchEntityFields(records[table],{{70,static_cast<double>(count+1)}}));
            records.insert(records.begin()+tableEnd,makeLayer(name,color,on,owner));
        } else {
            const auto owner=take();
            std::vector<DXFRecord> extra;
            const bool zero=name!="0";
            extra.push_back(DXFRecord{{{0,"TABLE"},{2,"LAYER"},{5,owner},{100,"AcDbSymbolTable"},{70,zero ? "2" : "1"}}});
            if (zero) extra.push_back(makeLayer("0",7,true,owner));
            extra.push_back(makeLayer(name,color,on,owner));
            extra.push_back(DXFRecord{{{0,"ENDTAB"}}});
            if (tablesEnd!=absent) records.insert(records.begin()+tablesEnd,extra.begin(),extra.end());
            else {
                extra.insert(extra.begin(),DXFRecord{{{0,"SECTION"},{2,"TABLES"}}});
                extra.push_back(DXFRecord{{{0,"ENDSEC"}}});
                const auto at=entitiesBegin!=absent ? entitiesBegin : records.size()-1;
                records.insert(records.begin()+at,extra.begin(),extra.end());
            }
        }
        for (auto& record : records) for (std::size_t i=0; i+1<record.groups.size(); ++i)
            if (record.groups[i].first==9 && record.groups[i].second=="$HANDSEED" && record.groups[i+1].first==5) record.groups[i+1].second=hex(next);
    }
    DXFRecord output;
    for (const auto& record : records) output.groups.insert(output.groups.end(),record.groups.begin(),record.groups.end());
    const auto result=output.raw();
    DXFDocument::parse(result);
    return result;
}
TextGeometry Drafting::textInfo(const std::string& raw) {
    const auto entity=parseEntity(raw);
    if (entity->type()!="TEXT" && entity->type()!="MTEXT") throw std::invalid_argument("Select TEXT or MTEXT");
    return *entity->geometry().text;
}
std::string Drafting::editText(const std::string& raw,const std::string& content,double height,double width) {
    const auto info=textInfo(raw);
    positiveFinite(height,"Text height must be positive and finite");
    if (content.empty()) throw std::invalid_argument("Text content must not be empty");
    if (info.multiline && (!std::isfinite(width) || width<0)) throw std::invalid_argument("MTEXT width must be finite and nonnegative");
    std::string encoded;
    for (std::size_t i=0; i<content.size(); ++i) {
        char c=content[i];
        if (c=='\r') { if (i+1<content.size() && content[i+1]=='\n') ++i; c='\n'; }
        if (c=='\n') {
            if (!info.multiline) throw std::invalid_argument("Use MTEXT for multiline content");
            encoded+="\\P";
        } else {
            if (static_cast<unsigned char>(c)<32 || c==127) throw std::invalid_argument("Unsupported text control character");
            if (info.multiline && (c=='\\' || c=='{' || c=='}')) encoded+='\\';
            encoded+=c;
        }
    }
    if (!info.multiline && encoded.size()>250) throw std::invalid_argument("Use MTEXT for content longer than 250 bytes");
    DXFRecord replacement;
    std::size_t position=0;
    while (info.multiline && encoded.size()-position>250) {
        std::size_t end=position+250;
        while (end>position && (static_cast<unsigned char>(encoded[end])&0xc0)==0x80) --end;
        if (end==position) throw std::invalid_argument("Invalid text encoding");
        replacement.groups.emplace_back(3,encoded.substr(position,end-position)); position=end;
    }
    replacement.groups.emplace_back(1,encoded.substr(position));
    const auto record=parseEntity(raw)->record();
    DXFRecord result;
    int depth=0; bool xdata=false, inserted=false;
    auto insert=[&] { if (!inserted) { result.groups.insert(result.groups.end(),replacement.groups.begin(),replacement.groups.end()); inserted=true; } };
    for (const auto& g : record.groups) {
        if (g.first==1001 && depth==0) { insert(); xdata=true; }
        if (!xdata && g.first==102) {
            if (!g.second.empty() && g.second.front()=='{') ++depth;
            else if (g.second=="}" && depth) --depth;
        } else if (!xdata && !depth && (g.first==1 || (info.multiline && g.first==3))) { insert(); continue; }
        result.groups.push_back(g);
    }
    insert();
    std::map<int,double> numbers{{40,height}};
    if (info.multiline) numbers[41]=width;
    const auto updated=patchEntityFields(result,numbers);
    parseEntity(updated)->geometry();
    return updated;
}
std::string Drafting::createText(const std::string& content,double height,const Vector3D& origin,bool multiline,double width,const std::string& layer) {
    if (layer.empty() || layer.find_first_of("\r\n")!=std::string::npos) throw std::invalid_argument("Invalid text layer name");
    DXFRecord record;
    record.groups={{0,multiline ? "MTEXT" : "TEXT"},{100,"AcDbEntity"},{8,layer},{100,multiline ? "AcDbMText" : "AcDbText"},
                   {10,"0"},{20,"0"},{30,"0"},{40,"1"},{1,""}};
    if (multiline) { record.groups.emplace_back(41,"0"); record.groups.emplace_back(71,"1"); }
    else { record.groups.emplace_back(100,"AcDbText"); record.groups.emplace_back(73,"0"); }
    const auto raw=patchEntityFields(record,{{10,origin.x},{20,origin.y},{30,origin.z}});
    return editText(raw,content,height,width);
}
std::string Drafting::chamfer(const std::string& raw,std::size_t index,double incoming,double outgoing) {
    return replaceCorner(raw,index,corner(raw,index),incoming,outgoing,0);
}
std::string Drafting::fillet(const std::string& raw,std::size_t index,double radius) {
    positiveFinite(radius,"Fillet radius must be positive and finite");
    const auto c=corner(raw,index);
    const double distance=radius/std::tan(c.theta/2), sweep=std::acos(-1.0)-c.theta;
    const double bulge=(c.turn>0 ? 1 : -1)*std::tan(sweep/4);
    return replaceCorner(raw,index,c,distance,distance,bulge);
}
std::string Drafting::offset(const std::string& raw, double distance)
{
    if (!std::isfinite(distance) || distance==0) throw std::invalid_argument("Offset distance must be finite and nonzero");
    const auto entity=parseEntity(raw);
    entity->geometry();
    const auto fields=nativeEntityFields(entity->record());
    if (entity->type()=="CIRCLE" || entity->type()=="ARC") {
        const double r=fields.number(40)+distance;
        positiveFinite(r,"Offset radius must remain positive and finite");
        return patchEntityFields(entity->record(),{{40,r}});
    }
    if (entity->type()=="LINE") {
        const auto edge=entity->geometry().segments.at(0);
        if (std::abs(edge.start.z-edge.end.z)>1e-7) throw std::invalid_argument("LINE offset requires a world XY plane");
        const double dx=edge.end.x-edge.start.x, dy=edge.end.y-edge.start.y, length=std::hypot(dx,dy);
        positiveFinite(length,"Cannot offset a zero-length or unbounded LINE");
        return entity->translated({-dy/length*distance,dx/length*distance,0});
    }
    if (entity->type()!="LWPOLYLINE") throw std::invalid_argument("Offset supports LINE, ARC, CIRCLE and LWPOLYLINE");
    return offsetPolyline(raw,distance);
}
std::string Drafting::trimLine(const std::string& raw, const std::string& boundary, bool start, bool extend)
{
    return trim(raw,boundary,start,extend);
}
}
