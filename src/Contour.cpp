#include <DXFCAD/Contour.h>
#include <DXFCAD/Drafting.h>
#include "PlanarCurves.h"
#include <algorithm>
#include <limits>
#include <numeric>
#include <set>

namespace DXFCAD {
ContourEditPlan::ContourEditPlan(const DXFDocument& source,
    const std::vector<std::pair<int,std::string>>& live,
    const std::vector<std::size_t>& selected,const std::vector<std::string>& results,bool preserveFirst)
{
    if (selected.empty()) throw std::invalid_argument("Select contours to replace");
    if (results.empty()) throw std::invalid_argument("Contour cleanup must retain at least one piece");
    source.serializeCurrent(live); // Reject corrupt ownership even outside the selection.
    std::set<std::size_t> unique;
    for (auto index : selected) {
        if (index>=live.size()) throw std::invalid_argument("Invalid selected contour index");
        if (!unique.insert(index).second) throw std::invalid_argument("Select each contour only once");
        Contour::length(live[index].second);
    }
    const auto original=parseEntity(live[selected.front()].second);
    const auto first=parseEntity(results.front());
    keepFirst_=preserveFirst && first->type()==original->type() && first->handle()==original->handle();
    for (std::size_t i=0;i<results.size();++i) {
        Contour::length(results[i]);
        records_.push_back(i==0 && keepFirst_ ? results[i] : parseEntity(results[i])->copied({0,0,0}));
    }
    std::vector<std::pair<int,std::string>> proposed;
    for (std::size_t i=0;i<live.size();++i) {
        if (!unique.count(i)) proposed.push_back(live[i]);
        else if (i==selected.front() && keepFirst_) proposed.emplace_back(live[i].first,records_.front());
    }
    for (std::size_t i=keepFirst_ ? 1 : 0;i<records_.size();++i) proposed.emplace_back(-1,records_[i]);
    drawingBytes_=source.serializeCurrent(proposed);
}

namespace {
using namespace Detail;
Vector3D minus3(Vector3D a,Vector3D b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vector3D cross3(Vector3D a,Vector3D b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
double size3(Vector3D p) { return std::hypot(p.x,p.y,p.z); }
void positiveTolerance(double value) {
    if (!std::isfinite(value) || value<=0) throw std::invalid_argument("Join/Close tolerance must be positive and finite");
}
// A line alone has no preferred plane. Choose a stable perpendicular normal.
Vector3D lineNormal(const EntityGeometry& g) {
    auto d=minus3(g.segments.at(0).end,g.segments.at(0).start);
    const double n=size3(d);
    if (!std::isfinite(n) || n<=tolerance) throw std::invalid_argument("Contour has a zero-length segment");
    d={d.x/n,d.y/n,d.z/n};
    const Vector3D preferred=std::abs(d.z)<0.9 ? Vector3D{0,0,1} : Vector3D{0,1,0};
    const double dot=d.x*preferred.x+d.y*preferred.y+d.z*preferred.z;
    return {preferred.x-dot*d.x,preferred.y-dot*d.y,preferred.z-dot*d.z};
}
DXFRecord opaque(const std::string& raw) {
    DXFRecord result;
    // Copy strips owner/handle/reactor identities before carrying application
    // metadata into a different entity type.
    const auto record=readDXFGroups(parseEntity(raw)->copied({0,0,0}));
    int depth=0; bool xdata=false;
    for (const auto& g : record.groups) {
        if (g.first==1001 && !depth) xdata=true;
        if (g.first==102 && !xdata) {
            if (!g.second.empty() && g.second.front()=='{') ++depth;
            else if (g.second=="}" && depth) --depth;
            result.groups.push_back(g);
        } else if (depth || xdata || g.first==999) result.groups.push_back(g);
    }
    return result;
}
std::string inherit(const std::string& generated,const std::string& source) {
    const auto original=parseEntity(source);
    const auto fields=nativeEntityFields(original->record());
    std::map<int,std::string> values{{8,original->layer()}};
    for (int code : {6,39,48,60,62,370,420,430,440})
        if (!fields.value(code).empty()) values[code]=fields.value(code);
    return patchEntityStringFields(readDXFGroups(generated),values)+opaque(source).raw();
}
class Path {
public:
    std::shared_ptr<DXFEntity> entity;
    std::string raw,type;
    EntityGeometry geometry;
    ExtrusionDirection extrusion;
    Plane plane;
    std::vector<Curve> curves;
    std::vector<double> cumulative{0};
    bool closed=false;
    explicit Path(const std::string& data)
        : entity(parseEntity(data)),raw(data),type(entity->type()),geometry(entity->geometry()),
          extrusion(getExtrusion()),plane(getNormal(),getOrigin()) {
        if (type!="LINE" && type!="ARC" && type!="CIRCLE" && type!="LWPOLYLINE")
            throw std::invalid_argument("Contour must be LINE, ARC, CIRCLE or LWPOLYLINE");
        curves=plane.curves(geometry);
        if (curves.empty()) throw std::invalid_argument("Contour has no segments");
        closed=type=="CIRCLE" || (type=="LWPOLYLINE" && readLWPolyline(raw).closed());
        for (const auto& c : curves) {
            const double next=cumulative.back()+c.extent();
            if (!std::isfinite(next) || next<=cumulative.back()) throw std::overflow_error("Contour length exceeds finite precision");
            cumulative.push_back(next);
        }
    }
    double length() const { return cumulative.back(); }
    double checked(double distance) const {
        if (!std::isfinite(distance) || distance<0 || distance>length())
            throw std::invalid_argument("Contour distance must be between zero and its length");
        if (distance<=tolerance) return 0;
        if (length()-distance<=tolerance) return closed ? 0 : length();
        return distance;
    }
    std::string slice(double first,double last) const {
        if (last-first<=tolerance || first<0 || last>length()*2 || last-first>length()+tolerance)
            throw std::invalid_argument("Contour piece must have positive length within one traversal");
        const auto fields=nativeEntityFields(entity->record());
        if (type=="LINE") {
            const auto a=plane.frame.toWorld(curves[0].at(first/length()),plane.elevation);
            const auto b=plane.frame.toWorld(curves[0].at(last/length()),plane.elevation);
            return patchEntityFields(entity->record(),{{10,a.x},{20,a.y},{30,a.z},{11,b.x},{21,b.y},{31,b.z}});
        }
        if (type=="ARC" || (type=="CIRCLE" && last-first<length()-tolerance)) {
            const double start=(type=="ARC" ? fields.number(50) : 0)+first/length()*std::abs(curves[0].sweep)*180/pi;
            const double end=start+(last-first)/length()*std::abs(curves[0].sweep)*180/pi;
            if (type=="ARC") return patchEntityFields(entity->record(),{{50,wrap(start*pi/180)*180/pi},{51,wrap(end*pi/180)*180/pi}});
            DXFRecord arc;
            arc.groups={{0,"ARC"},{100,"AcDbEntity"},{8,entity->layer()},{100,"AcDbCircle"}};
            for (int code : {10,20,30,40,210,220,230}) if (!fields.value(code).empty()) arc.groups.emplace_back(code,fields.value(code));
            arc.groups.emplace_back(100,"AcDbArc");
            return inherit(patchEntityFields(arc,{{50,wrap(start*pi/180)*180/pi},{51,wrap(end*pi/180)*180/pi}}),raw);
        }
        std::vector<LWPolylineVertex> vertices;
        std::vector<int> origins;
        const auto model=type=="LWPOLYLINE" ? readLWPolyline(raw) : LWPolyline();
        const bool variable=std::any_of(model.vertices().begin(),model.vertices().end(),[](const auto& v) {
            return v.hasWidths || v.startWidth!=0 || v.endWidth!=0;
        });
        auto addVertex=[&](LWPolylineVertex v,int origin) {
            if (origin>=0 && std::find(origins.begin(),origins.end(),origin)!=origins.end()) origin=-1;
            vertices.push_back(v); origins.push_back(origin);
        };
        for (int cycle=0;cycle<2;++cycle) for (std::size_t i=0;i<curves.size();++i) {
            const double begin=cumulative[i]+cycle*length(), end=cumulative[i+1]+cycle*length();
            double a=std::max(first,begin), b=std::min(last,end);
            if (b-a<=tolerance) continue;
            // A circle opened at one point is represented by two exact
            // semicircles; a full-turn ARC has ambiguous DXF end angles.
            const int count=type=="CIRCLE" && b-a>=length()-tolerance ? 2 : 1;
            for (int half=0;half<count;++half) {
                const double u=(a+(b-a)*half/count-begin)/(end-begin);
                const double v=(a+(b-a)*(half+1)/count-begin)/(end-begin);
                const auto p=curves[i].at(u), q=curves[i].at(v);
                LWPolylineVertex start{p.x,p.y,curves[i].circular() ? std::tan(curves[i].sweep*(v-u)/4) : 0};
                int origin=-1, endOrigin=-1;
                if (type=="LWPOLYLINE") {
                    const auto original=model.vertices()[i];
                    start.hasWidths=variable;
                    start.startWidth=original.startWidth+(original.endWidth-original.startWidth)*u;
                    start.endWidth=original.startWidth+(original.endWidth-original.startWidth)*v;
                    if (u==0) origin=static_cast<int>(i);
                    if (v==1) endOrigin=static_cast<int>((i+1)%model.vertices().size());
                }
                if (!vertices.empty()) { vertices.pop_back(); origins.pop_back(); }
                addVertex(start,origin);
                auto lastVertex=type=="LWPOLYLINE" && endOrigin>=0 ? model.vertices()[endOrigin] : LWPolylineVertex{};
                lastVertex.x=q.x; lastVertex.y=q.y;
                addVertex(lastVertex,endOrigin);
            }
        }
        const LWPolyline result(vertices,false,model.constantWidth(),plane.elevation,extrusion);
        return type=="LWPOLYLINE" ? updateLWPolyline(raw,result,origins) : inherit(createLWPolyline(result),raw);
    }
private:
    ExtrusionDirection getExtrusion() const {
        const auto fields=nativeEntityFields(entity->record());
        return {fields.number(210),fields.number(220),fields.number(230,1)};
    }
    Vector3D getNormal() const {
        if (type=="LWPOLYLINE") return OCSFrame(extrusion).normal();
        if (type=="LINE") return lineNormal(geometry);
        return normal(geometry);
    }
    Vector3D getOrigin() const {
        if (!geometry.segments.empty()) return geometry.segments.front().start;
        if (!geometry.circles.empty()) return geometry.circles.front().center;
        throw std::invalid_argument("Contour has no geometry");
    }
};
std::vector<std::string> identities(std::vector<std::string> pieces,const Path& path) {
    for (std::size_t i=0;i<pieces.size();++i) {
        const auto entity=parseEntity(pieces[i]); entity->geometry();
        if (i || entity->type()!=path.type) pieces[i]=entity->copied({0,0,0});
    }
    return pieces;
}
}
double Contour::length(const std::string& raw) { return Path(raw).length(); }
double Contour::distanceAtPoint(const std::string& raw,Vector3D pick) {
    const Path path(raw);
    // Picking may be near a centerline, but must remain in its plane.
    const auto p=path.plane.point(pick);
    double best=std::numeric_limits<double>::infinity(), distance=0;
    bool ambiguous=false;
    for (std::size_t i=0;i<path.curves.size();++i) {
        const auto& c=path.curves[i];
        std::vector<double> candidates{0,1};
        if (!c.circular()) candidates.push_back(std::clamp(c.parameter(p),0.0,1.0));
        else if (Detail::length(sub(p,c.c))>tolerance) {
            const double t=c.parameter(p);
            if (t>=0 && t<=1) candidates.push_back(t);
        }
        const bool center=c.circular() && Detail::length(sub(p,c.c))<=tolerance;
        for (double t : candidates) {
            const double gap=Detail::length(sub(c.at(t),p));
            double value=path.cumulative[i]+t*c.extent();
            if (path.closed && path.length()-value<=tolerance) value=0;
            if (gap<best-tolerance) { best=gap; distance=value; ambiguous=center; }
            else if (std::abs(gap-best)<=tolerance && std::abs(value-distance)>tolerance) ambiguous=true;
        }
    }
    if (ambiguous) throw std::invalid_argument("Contour pick is ambiguous; pick away from crossings or equidistant segments");
    return distance;
}
double Contour::distanceAtRay(const std::string& raw,Vector3D start,Vector3D end) {
    const Path path(raw);
    if (path.type=="LINE") {
        auto unit=[](Vector3D v) {
            const double n=size3(v);
            if (!std::isfinite(n) || n<=tolerance) throw std::invalid_argument("Picking needs a finite nonzero view ray");
            return Vector3D{v.x/n,v.y/n,v.z/n};
        };
        auto dot=[](Vector3D a,Vector3D b) { return a.x*b.x+a.y*b.y+a.z*b.z; };
        const auto a=path.geometry.segments[0].start;
        const auto u=unit(minus3(path.geometry.segments[0].end,a)),v=unit(minus3(end,start)),w=minus3(a,start);
        const double b=dot(u,v), denominator=1-b*b;
        if (denominator<1e-12) throw std::invalid_argument("View ray is parallel to the contour; change the view");
        const double distance=(b*dot(v,w)-dot(u,w))/denominator;
        if (!std::isfinite(distance)) throw std::overflow_error("Picked contour distance exceeds finite range");
        return std::clamp(distance,0.0,path.length());
    }
    const auto p=path.plane.frame.intersectLine(start,end,path.plane.elevation);
    return distanceAtPoint(raw,path.plane.frame.toWorld(p,path.plane.elevation));
}
std::vector<std::string> Contour::split(const std::string& raw,const std::vector<double>& distances) {
    const Path path(raw);
    std::vector<double> cuts;
    for (double value : distances) {
        const double d=path.checked(value);
        if (path.closed || (d>tolerance && d<path.length()-tolerance)) cuts.push_back(d);
    }
    std::sort(cuts.begin(),cuts.end());
    cuts.erase(std::unique(cuts.begin(),cuts.end(),[](double a,double b){return std::abs(a-b)<=tolerance;}),cuts.end());
    if (cuts.empty()) throw std::invalid_argument("Split needs at least one interior cut (or a seam on a closed contour)");
    if (!path.closed) { cuts.insert(cuts.begin(),0); cuts.push_back(path.length()); }
    else cuts.push_back(cuts.front()+path.length());
    std::vector<std::string> result;
    for (std::size_t i=1;i<cuts.size();++i) result.push_back(path.slice(cuts[i-1],cuts[i]));
    return identities(std::move(result),path);
}
std::vector<std::string> Contour::breakBetween(const std::string& raw,double first,double second) {
    const Path path(raw);
    first=path.checked(first); second=path.checked(second);
    if (std::abs(first-second)<=tolerance) throw std::invalid_argument("Break needs two distinct distances; use Split to open a seam");
    std::vector<std::string> result;
    if (path.closed) result.push_back(path.slice(second,first+(first<second ? path.length() : 0)));
    else {
        if (first>second) std::swap(first,second);
        if (first>tolerance) result.push_back(path.slice(0,first));
        if (second<path.length()-tolerance) result.push_back(path.slice(second,path.length()));
        if (result.empty()) throw std::invalid_argument("Break would remove the entire contour");
    }
    return identities(std::move(result),path);
}

std::string Contour::close(const std::string& raw,double endpointTolerance) {
    positiveTolerance(endpointTolerance);
    const Path path(raw);
    if (path.type!="LWPOLYLINE") throw std::invalid_argument("Close requires a LWPOLYLINE; Join primitives first");
    const auto model=readLWPolyline(raw);
    if (model.closed()) return raw;
    auto vertices=model.vertices();
    const bool variable=std::any_of(vertices.begin(),vertices.end(),[](const auto& v) {
        return v.hasWidths || v.startWidth!=0 || v.endWidth!=0;
    });
    std::vector<int> origins(vertices.size()); std::iota(origins.begin(),origins.end(),0);
    if (Detail::length({vertices.back().x-vertices.front().x,vertices.back().y-vertices.front().y})<=endpointTolerance) {
        vertices.pop_back(); origins.pop_back();
        if (vertices.size()<2) throw std::invalid_argument("Closing would leave fewer than two vertices");
    } else vertices.back().bulge=0; // A formerly unused bulge must not bend the new closing edge.
    if (variable) for (auto& v : vertices) v.hasWidths=true;
    return updateLWPolyline(raw,LWPolyline(vertices,true,model.constantWidth(),model.elevation(),model.extrusion()),origins);
}

std::string Contour::join(const std::vector<std::string>& entities,double endpointTolerance) {
    positiveTolerance(endpointTolerance);
    if (entities.size()<2 || entities.size()>10000) throw std::invalid_argument("Join requires 2..10000 open contours");
    std::vector<Path> paths;
    for (const auto& raw : entities) {
        paths.emplace_back(raw);
        if (paths.back().closed) throw std::invalid_argument("Join requires open LINE, ARC or LWPOLYLINE entities");
    }
    const auto firstFields=nativeEntityFields(paths[0].entity->record());
    const auto firstOpaque=opaque(entities[0]).raw();
    for (std::size_t i=1;i<paths.size();++i) {
        const auto fields=nativeEntityFields(paths[i].entity->record());
        if (paths[i].entity->layer()!=paths[0].entity->layer() || fields.value(6,"BYLAYER")!=firstFields.value(6,"BYLAYER"))
            throw std::invalid_argument("Joined contours must share layer and display properties");
        for (const auto& entry : std::map<int,double>{{39,0},{48,1},{60,0},{62,256},{370,-1},{420,-1},{440,0}})
            if (fields.number(entry.first,entry.second)!=firstFields.number(entry.first,entry.second))
                throw std::invalid_argument("Joined contours must share layer and display properties");
        const auto data=opaque(entities[i]).raw();
        if (!data.empty() && data!=firstOpaque) throw std::invalid_argument("Join would discard conflicting application metadata");
    }
    Vector3D n=paths[0].plane.frame.normal();
    const auto origin=paths[0].geometry.segments.front().start;
    bool curvedPlane=false;
    for (const auto& path : paths) if (path.type!="LINE") { n=path.plane.frame.normal(); curvedPlane=true; break; }
    if (!curvedPlane) {
        const auto d=minus3(paths[0].geometry.segments.front().end,origin);
        const double dn=size3(d);
        bool found=false;
        for (const auto& path : paths) {
            for (const auto p : {path.geometry.segments.front().start,path.geometry.segments.front().end}) {
                const auto other=minus3(p,origin);
                const double on=size3(other);
                if (on<=tolerance) continue;
                const auto cross=cross3({d.x/dn,d.y/dn,d.z/dn},{other.x/on,other.y/on,other.z/on});
                if (size3(cross)>1e-10) { n=cross; found=true; break; }
            }
            if (found) break;
        }
    }
    const Plane plane(n,origin);
    std::vector<std::vector<Curve>> curves;
    std::vector<Point2D> endpoints;
    for (const auto& path : paths) {
        curves.push_back(plane.curves(path.geometry));
        endpoints.push_back(curves.back().front().a); endpoints.push_back(curves.back().back().b);
    }
    // Endpoint graph rejects disconnected selections, forks, duplicate edges
    // and tolerance clusters whose members are not mutually within tolerance.
    std::vector<int> nodes(endpoints.size()); std::iota(nodes.begin(),nodes.end(),0);
    auto root=[&](int index) { while (nodes[index]!=index) index=nodes[index]; return index; };
    for (std::size_t i=0;i<endpoints.size();++i) for (std::size_t j=0;j<i;++j)
        if (Detail::length(sub(endpoints[i],endpoints[j]))<=endpointTolerance) nodes[root(static_cast<int>(i))]=root(static_cast<int>(j));
    std::map<int,std::vector<int>> incident;
    for (std::size_t i=0;i<endpoints.size();++i) {
        nodes[i]=root(static_cast<int>(i)); incident[nodes[i]].push_back(static_cast<int>(i/2));
        for (std::size_t j=0;j<i;++j) if (nodes[i]==root(static_cast<int>(j)) && Detail::length(sub(endpoints[i],endpoints[j]))>endpointTolerance)
            throw std::invalid_argument("Join endpoint tolerance produces an ambiguous cluster");
    }
    std::vector<int> ends;
    for (std::size_t i=0;i<paths.size();++i) {
        if (nodes[2*i]==nodes[2*i+1]) throw std::invalid_argument("Join tolerance collapses an input contour's endpoints");
        for (std::size_t j=0;j<i;++j) if (paths[i].type=="LINE" && paths[j].type=="LINE"
            && ((nodes[2*i]==nodes[2*j] && nodes[2*i+1]==nodes[2*j+1]) ||
                (nodes[2*i]==nodes[2*j+1] && nodes[2*i+1]==nodes[2*j])))
            throw std::invalid_argument("Join selection contains duplicate or overlapping lines");
    }
    for (const auto& entry : incident) {
        if (entry.second.size()>2) throw std::invalid_argument("Join selection branches at an endpoint");
        if (entry.second.size()==1) ends.push_back(entry.first);
    }
    if (!ends.empty() && ends.size()!=2) throw std::invalid_argument("Join selection is disconnected");
    const bool closed=ends.empty();
    int current=closed ? nodes[0] : ends[0];
    std::vector<bool> used(paths.size(),false);
    std::vector<LWPolylineVertex> vertices;
    std::vector<int> origins;
    for (std::size_t step=0;step<paths.size();++step) {
        int index=-1;
        for (int candidate : incident[current]) if (!used[candidate]) { index=candidate; break; }
        if (index<0) throw std::invalid_argument("Join selection is disconnected or contains a separate loop");
        used[index]=true;
        const bool reverse=nodes[index*2]!=current;
        const auto& source=curves[index];
        const auto model=paths[index].type=="LWPOLYLINE" ? readLWPolyline(paths[index].raw) : LWPolyline();
        bool variable=false;
        for (const auto& v : model.vertices()) if (v.hasWidths || v.startWidth!=0 || v.endWidth!=0) variable=true;
        for (std::size_t j=0;j<source.size();++j) {
            const auto k=reverse ? source.size()-1-j : j;
            auto c=source[k];
            if (reverse) { std::swap(c.a,c.b); c.sweep=-c.sweep; }
            LWPolylineVertex vertex{c.a.x,c.a.y,c.circular() ? std::tan(c.sweep/4) : 0};
            if (paths[index].type=="LWPOLYLINE") {
                const auto& v=model.vertices()[k];
                vertex.startWidth=variable ? (reverse ? v.endWidth : v.startWidth) : model.constantWidth();
                vertex.endWidth=variable ? (reverse ? v.startWidth : v.endWidth) : model.constantWidth();
            }
            vertex.hasWidths=true;
            int vertexOrigin=index==0 && paths[0].type=="LWPOLYLINE" ? static_cast<int>(reverse ? k+1 : k) : -1;
            if (!vertices.empty()) {
                vertex.x=vertices.back().x; vertex.y=vertices.back().y;
                if (vertexOrigin<0) vertexOrigin=origins.back();
                vertices.pop_back(); origins.pop_back();
            }
            vertices.push_back(vertex);
            origins.push_back(vertexOrigin);
            vertices.push_back({c.b.x,c.b.y});
            origins.push_back(index==0 && paths[0].type=="LWPOLYLINE" ? static_cast<int>(reverse ? k : k+1) : -1);
        }
        current=nodes[index*2+(reverse ? 0 : 1)];
    }
    if (closed) { vertices.pop_back(); origins.pop_back(); }
    ExtrusionDirection extrusion{plane.frame.normal().x,plane.frame.normal().y,plane.frame.normal().z};
    const LWPolyline model(vertices,closed,0,plane.elevation,extrusion);
    if (paths[0].type=="LWPOLYLINE") return updateLWPolyline(parseEntity(entities[0])->copied({0,0,0}),model,origins);
    return inherit(createLWPolyline(model),entities[0]);
}

std::vector<std::string> Contour::trimInterior(const std::string& raw,
    const std::vector<std::string>& boundaries,double pickDistance) {
    const Path path(raw);
    pickDistance=path.checked(pickDistance);
    if (boundaries.empty()) throw std::invalid_argument("Select at least one finite trimming boundary");
    std::vector<Path> cutters;
    for (const auto& record : boundaries) cutters.emplace_back(record);
    auto n=path.plane.frame.normal();
    if (path.type=="LINE") {
        const auto a=minus3(path.geometry.segments[0].end,path.geometry.segments[0].start);
        for (const auto& cutter : cutters) {
            if (cutter.type!="LINE") { n=cutter.plane.frame.normal(); break; }
            const auto b=minus3(cutter.geometry.segments[0].end,cutter.geometry.segments[0].start);
            const double al=size3(a),bl=size3(b);
            const auto cross=cross3({a.x/al,a.y/al,a.z/al},{b.x/bl,b.y/bl,b.z/bl});
            if (size3(cross)>1e-10) { n=cross; break; }
        }
    }
    const auto origin=path.geometry.segments.empty() ? path.geometry.circles[0].center : path.geometry.segments[0].start;
    const Plane plane(n,origin);
    const auto curves=plane.curves(path.geometry);
    std::vector<double> cuts;
    auto crossesJoint=[](const std::vector<Curve>& pieces,bool closed,std::size_t index,double t,Point2D p,Point2D direction) {
        const double eps=tolerance/pieces[index].extent();
        if (t>eps && t<1-eps) return true;
        const std::size_t vertex=t<=eps ? index : index+1;
        if (!closed && (vertex==0 || vertex==pieces.size())) return true; // Finite open endpoints may cut.
        const auto& before=pieces[(vertex+pieces.size()-1)%pieces.size()];
        const auto& after=pieces[vertex%pieces.size()];
        auto side=[&](Point2D q) {
            const auto delta=sub(q,p);
            const double norm=Detail::length(delta);
            return norm>0 ? cross(direction,mul(delta,1/norm)) : 0;
        };
        const double a=side(before.at(1-1e-5)),b=side(after.at(1e-5));
        return (a>1e-10 && b< -1e-10) || (a< -1e-10 && b>1e-10);
    };
    for (const auto& cutter : cutters) {
        const auto boundaryCurves=plane.curves(cutter.geometry);
        for (std::size_t j=0;j<boundaryCurves.size();++j) {
        const auto& boundary=boundaryCurves[j];
        for (std::size_t i=0;i<curves.size();++i) {
            const auto& c=curves[i];
            if (c.circular() && boundary.circular() && Detail::length(sub(c.c,boundary.c))<=tolerance
                && std::abs(c.radius-boundary.radius)<=tolerance)
                throw std::invalid_argument("Coincident circular boundaries have no unique trim intersection");
            if (!c.circular() && !boundary.circular()) {
                const auto u=mul(sub(c.b,c.a),1/c.extent()), v=mul(sub(boundary.b,boundary.a),1/boundary.extent());
                if (std::abs(cross(u,v))<1e-12 && std::abs(cross(sub(boundary.a,c.a),u))<=tolerance) {
                    const double a=c.parameter(boundary.a), b=c.parameter(boundary.b);
                    if (std::min(1.0,std::max(a,b))-std::max(0.0,std::min(a,b))>tolerance/c.extent())
                        throw std::invalid_argument("Overlapping boundaries have no unique trim intersection");
                }
            }
            for (const auto p : intersections(c,boundary)) {
                const double t=c.parameter(p), u=boundary.parameter(p);
                if (t < -tolerance/c.extent() || t>1+tolerance/c.extent() ||
                    u < -tolerance/boundary.extent() || u>1+tolerance/boundary.extent()) continue;
                auto tangent=[](const Curve& curve,Point2D p) {
                    return curve.circular() ? Point2D{-(p.y-curve.c.y)/curve.radius,(p.x-curve.c.x)/curve.radius}
                        : mul(sub(curve.b,curve.a),1/curve.extent());
                };
                if (std::abs(cross(tangent(c,p),tangent(boundary,p)))<=1e-10) continue; // Touching alone does not cut.
                if (!crossesJoint(curves,path.closed,i,t,p,tangent(boundary,p)) ||
                    !crossesJoint(boundaryCurves,cutter.closed,j,u,p,tangent(c,p))) continue;
                double distance=path.cumulative[i]+std::clamp(t,0.0,1.0)*c.extent();
                if (path.closed && path.length()-distance<=tolerance) distance=0;
                if (path.closed || (distance>tolerance && distance<path.length()-tolerance)) cuts.push_back(distance);
            }
        }
        }
    }
    std::sort(cuts.begin(),cuts.end());
    cuts.erase(std::unique(cuts.begin(),cuts.end(),[](double a,double b){return std::abs(a-b)<=tolerance;}),cuts.end());
    if (cuts.empty() || (path.closed && cuts.size()<2))
        throw std::invalid_argument("No distinct boundary crossings enclose a removable contour section");
    for (double cut : cuts) if (std::abs(cut-pickDistance)<=tolerance)
        throw std::invalid_argument("Pick inside the section to remove, away from boundary intersections");
    const auto next=std::upper_bound(cuts.begin(),cuts.end(),pickDistance);
    const double first=next==cuts.begin() ? (path.closed ? cuts.back() : 0) : *(next-1);
    const double second=next==cuts.end() ? (path.closed ? cuts.front() : path.length()) : *next;
    return breakBetween(raw,first,second);
}
}
