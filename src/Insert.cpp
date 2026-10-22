#include <DXFCAD/DXFDocument.h>
#include <DXFCAD/Drafting.h>
#include <DXFCAD/OCSFrame.h>
#include <DXFCAD/SnapGeometry.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <set>
#include <stdexcept>

namespace DXFCAD {
namespace {
std::string key(std::string name) {
    for (auto& c : name) if (static_cast<unsigned char>(c) < 128)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return name;
}
using Blocks = std::map<std::string, std::vector<DXFRecord>>;
Blocks readBlocks(const std::string& context) {
    Blocks blocks;
    std::vector<DXFRecord> records;
    for (const auto& g : readDXFGroups(context).groups) {
        if (g.first == 0) records.emplace_back();
        if (records.empty()) throw std::invalid_argument("Invalid INSERT block context");
        records.back().groups.push_back(g);
    }
    std::vector<DXFRecord> block;
    for (const auto& r : records) {
        if (r.value(0) == "BLOCK") {
            if (!block.empty()) throw std::invalid_argument("Unterminated BLOCK");
            block.push_back(r);
        } else if (!block.empty()) {
            block.push_back(r);
            if (r.value(0) == "ENDBLK") {
                auto name = key(nativeEntityFields(block.front()).value(2));
                if (name.empty()) throw std::invalid_argument("Empty BLOCK name");
                if (blocks.count(name)) blocks[name].clear();
                else blocks.emplace(name, std::move(block));
                block.clear();
            }
        } else throw std::invalid_argument("Unexpected record in INSERT block context");
    }
    if (!block.empty()) throw std::invalid_argument("Unterminated BLOCK");
    return blocks;
}
Vector3D point(const DXFRecord& r) {
    return {r.number(10),r.number(20),r.number(30)};
}
BlockPart identity() { return {{},{0,0,0},{1,0,0},{0,1,0},{0,0,1}}; }
Vector3D vector(const BlockPart& t, Vector3D p) {
    auto copy=t; copy.origin={0,0,0};
    return copy.transformPoint(p);
}
BlockPart compose(const BlockPart& a,const BlockPart& b) {
    return {b.entity,a.transformPoint(b.origin),vector(a,b.xAxis),vector(a,b.yAxis),vector(a,b.zAxis)};
}
class InsertEntity final : public DXFEntity {
public:
    InsertEntity(DXFRecord record, const std::string& context) : DXFEntity(std::move(record)) {
        if (context.empty()) return; // Metadata-only parsing; geometry requires dependencies.
        const auto blocks=readBlocks(context);
        std::set<std::string> selected, active;
        std::function<void(std::string)> visit = [&](std::string name) {
            name=key(name);
            if (active.count(name)) throw std::invalid_argument("Cyclic BLOCK reference: "+name);
            if (selected.count(name)) return;
            if (active.size() >= 32) throw std::invalid_argument("BLOCK nesting exceeds 32 levels");
            const auto found=blocks.find(name);
            if (found==blocks.end()) throw std::invalid_argument("Missing BLOCK: "+name);
            if (found->second.empty()) throw std::invalid_argument("Ambiguous BLOCK name: "+name);
            active.insert(name);
            for (const auto& r : found->second) if (r.value(0)=="INSERT") visit(nativeEntityFields(r).value(2));
            active.erase(name); selected.insert(name);
        };
        visit(nativeEntityFields(record_).value(2));
        for (const auto& name : selected) for (const auto& r : blocks.at(name)) context_+=r.raw();
    }
    std::string context() const override { return context_; }
    std::string translated(const Vector3D& offset) const override { return translatedPoints(offset,{10},true); }
    std::vector<BlockPart> blockParts() const override {
        const auto blocks=readBlocks(context_);
        const auto root=nativeEntityFields(record_);
        std::vector<BlockPart> result;
        std::size_t expanded=0;
        std::function<void(const DXFRecord&,const BlockPart&,std::size_t)> expand;
        expand = [&](const DXFRecord& raw,const BlockPart& parent,std::size_t depth) {
            if (++expanded > 10000 || depth > 32) throw std::invalid_argument("INSERT expansion limit exceeded");
            const auto fields=nativeEntityFields(raw);
            if (fields.value(10).empty() || fields.value(20).empty())
                throw std::invalid_argument("INSERT point lacks X/Y");
            if (fields.integer(66)) throw std::invalid_argument("INSERT attributes are not supported yet");
            const auto found=blocks.find(key(fields.value(2)));
            if (found==blocks.end()) throw std::invalid_argument("Missing BLOCK: "+fields.value(2));
            const auto& block=found->second;
            const auto header=nativeEntityFields(block.front());
            if ((header.integer(70)&(4|8|16)) || !header.value(1).empty())
                throw std::invalid_argument("External BLOCK references are not supported yet");
            const auto base=point(header), insertion=point(fields);
            const double sx=fields.number(41,1), sy=fields.number(42,1), sz=fields.number(43,1);
            if (!sx || !sy || !sz) throw std::invalid_argument("INSERT scales must be nonzero");
            const int columns=fields.integer(70,1), rows=fields.integer(71,1);
            if (columns < 1 || rows < 1 || columns > 10000/rows)
                throw std::invalid_argument("Invalid or excessive INSERT array size");
            const double angle=std::fmod(fields.number(50),360)*std::acos(-1.0)/180;
            const double c=std::cos(angle), s=std::sin(angle);
            const OCSFrame frame({fields.number(210),fields.number(220),fields.number(230,1)});
            const auto x=frame.toWorld({c,s},0), y=frame.toWorld({-s,c},0), z=frame.normal();
            BlockPart local{{},{},{x.x*sx,x.y*sx,x.z*sx},{y.x*sy,y.y*sy,y.z*sy},{z.x*sz,z.y*sz,z.z*sz}};
            const auto b=vector(local,base);
            for (int row=0;row<rows;++row) for (int col=0;col<columns;++col) {
                const auto p=frame.toWorld({insertion.x+c*col*fields.number(44)-s*row*fields.number(45),
                    insertion.y+s*col*fields.number(44)+c*row*fields.number(45)},insertion.z);
                local.origin={p.x-b.x,p.y-b.y,p.z-b.z};
                const auto transform=compose(parent,local);
                for (std::size_t i=1;i+1<block.size();++i) {
                    const auto child=nativeEntityFields(block[i]);
                    if (child.integer(60)) continue;
                    if (child.value(8,"0")!="0" && key(child.value(8))!=key(layer()))
                        throw std::invalid_argument("BLOCK child layers must inherit the INSERT layer in this version");
                    const int color=child.integer(62,256);
                    if ((color!=0 && color!=256) || child.integer(420,-1)>=0)
                        throw std::invalid_argument("BLOCK child colors must be BYBLOCK or BYLAYER in this version");
                    if (color==256 && (root.integer(62,256)!=256 || root.integer(420,-1)>=0))
                        throw std::invalid_argument("BYLAYER block children require a BYLAYER INSERT color in this version");
                    if (child.value(0)=="INSERT") expand(block[i],transform,depth+1);
                    else {
                        if (++expanded > 10000) throw std::invalid_argument("INSERT expansion limit exceeded");
                        auto part=transform;
                        part.entity=parseEntity(block[i].raw());
                        part.entity->geometry();
                        result.push_back(std::move(part));
                    }
                }
            }
        };
        expand(record_,identity(),0);
        return result;
    }
    EntityGeometry geometry() const override {
        // Conservative point bounds support snapping's spatial filter without
        // pretending nonuniformly scaled circles are still circles.
        EntityGeometry result;
        for (const auto& part : blockParts()) {
            for (const auto& p : SnapGeometry(*part.entity).boundPoints()) result.bounds.push_back(part.transformPoint(p));
        }
        return result;
    }
    std::vector<DXFSnapPoint> snapPoints() const override {
        const auto f=nativeEntityFields(record_);
        const OCSFrame frame({f.number(210),f.number(220),f.number(230,1)});
        std::vector<DXFSnapPoint> result{{frame.toWorld({f.number(10),f.number(20)},f.number(30)),"insertion"}};
        for (const auto& part : blockParts()) for (const auto& snap : part.entity->snapPoints())
            result.push_back({part.transformPoint(snap.position),snap.kind});
        return result;
    }
private:
    std::string context_;
};
}
Vector3D BlockPart::transformPoint(const Vector3D& p) const {
    Vector3D q{origin.x+xAxis.x*p.x+yAxis.x*p.y+zAxis.x*p.z,
        origin.y+xAxis.y*p.x+yAxis.y*p.y+zAxis.y*p.z,
        origin.z+xAxis.z*p.x+yAxis.z*p.y+zAxis.z*p.z};
    if (!std::isfinite(q.x)||!std::isfinite(q.y)||!std::isfinite(q.z))
        throw std::overflow_error("INSERT transform exceeds finite range");
    return q;
}
std::shared_ptr<DXFEntity> makeInsertEntity(DXFRecord record,const std::string& context) {
    return std::make_shared<InsertEntity>(std::move(record),context);
}
}
