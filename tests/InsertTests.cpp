#include <DXFCAD/DXFDocument.h>
#include <DXFCAD/Drafting.h>
#include <DXFCAD/SnapGeometry.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok) { if (!ok) throw std::runtime_error("INSERT check failed"); }
void near(DXFCAD::Vector3D p,DXFCAD::Vector3D q) {
    check(std::hypot(p.x-q.x,p.y-q.y,p.z-q.z)<1e-8);
}
template<class F> void rejects(F f) {
    try { f(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid INSERT accepted");
}
std::string block(const std::string& name,const std::string& body,const std::string& base="10\n0\n20\n0\n") {
    return "0\nBLOCK\n2\n"+name+"\n70\n0\n"+base+body+"0\nENDBLK\n";
}
std::string drawing(const std::string& blocks,const std::string& inserts) {
    return "0\nSECTION\n2\nBLOCKS\n"+blocks+"0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"+inserts+"0\nENDSEC\n0\nEOF\n";
}
const std::string line="0\nLINE\n10\n1\n20\n2\n11\n3\n21\n2\n";
const std::string insert="0\nINSERT\n5\nAB\n330\nF\n2\nB\n10\n10\n20\n20\n41\n2\n42\n3\n43\n4\n50\n90\n1001\nAPP\n1000\nkeep\n";
}
int main() {
    using namespace DXFCAD;
    try {
        const auto blocks=block("B",line,"10\n1\n20\n2\n");
        const auto source=drawing(blocks,insert);
        const auto doc=DXFDocument::parse(source);
        check(doc.entities.size()==1 && doc.warnings.empty());
        const auto e=doc.entities.at(0);
        const auto parts=e->blockParts(); check(parts.size()==1);
        near(parts[0].transformPoint({1,2,0}),{10,20,0});
        near(parts[0].transformPoint({3,2,0}),{10,24,0});
        check(e->raw()==insert && e->context()==blocks);
        check(doc.serialize()==source);
        check(!SnapGeometry(*e).boundPoints().empty());
        near(e->snapPoints().at(0).position,{10,20,0});
        auto moved=doc.resolveEntity(e->translated({2,3,4}));
        near(moved->blockParts()[0].transformPoint({3,2,0}),{12,27,4});
        auto rotated=doc.resolveEntity(e->rotated(90,{0,0,0}));
        near(rotated->blockParts()[0].transformPoint({3,2,0}),{-24,10,0});
        auto scaled=doc.resolveEntity(e->scaled(2,{1,2,3}));
        near(scaled->blockParts()[0].transformPoint({3,2,0}),{19,46,-3});
        auto mirrored=doc.resolveEntity(e->mirrored(0,{0,0,0}));
        near(mirrored->blockParts()[0].transformPoint({3,2,1}),{10,-24,4});
        const auto replacement=e->translated({1,0,0});
        auto exported=DXFDocument::parse(doc.serialize({{0,replacement}}));
        check(exported.entities.size()==1 && exported.entities[0]->context()==blocks);
        check(exported.entities[0]->record().value(1000)=="keep");
        const auto copied=e->copied({5,0,0});
        check(parseEntity(copied)->handle().empty());
        exported=DXFDocument::parse(doc.serialize({}, {}, {copied}));
        check(exported.entities.size()==2 && exported.entities[1]->handle()!="AB");
        check(DXFDocument::parse(doc.serialize({}, {0})).entities.empty());
        const auto nested=block("Outer","0\nINSERT\n2\nb\n10\n5\n20\n0\n41\n-1\n");
        auto nestedDoc=DXFDocument::parse(drawing(blocks+nested,"0\nINSERT\n2\nouter\n10\n0\n20\n7\n"));
        check(nestedDoc.entities.size()==1 && nestedDoc.warnings.empty());
        near(nestedDoc.entities[0]->blockParts()[0].transformPoint({3,2,0}),{3,7,0});
        auto array=patchEntityFields(parseEntity(insert)->record(),{{70,2},{71,2},{44,5},{45,7}});
        auto arrayEntity=doc.resolveEntity(array);
        const auto instances=arrayEntity->blockParts(); check(instances.size()==4);
        near(instances[3].transformPoint({1,2,0}),{3,25,0});
        auto tilted=patchEntityFields(parseEntity(insert)->record(),{{210,0},{220,1},{230,0}});
        near(doc.resolveEntity(tilted)->blockParts()[0].transformPoint({3,2,1}),{-10,4,24});
        for (const auto& raw : {patchEntityFields(e->record(),{{41,0}}),
                patchEntityFields(e->record(),{{66,1}}),patchEntityFields(e->record(),{{70,0}}),
                patchEntityFields(e->record(),{{70,10001}})}) rejects([&]{doc.resolveEntity(raw);});
        rejects([&]{DXFDocument::empty().resolveEntity(insert);});
        auto cyclic=DXFDocument::parse(drawing(block("B","0\nINSERT\n2\nB\n10\n0\n20\n0\n"),insert));
        check(cyclic.entities.empty() && cyclic.warnings.size()==1 && cyclic.serialize().find("INSERT")!=std::string::npos);
        auto unsupported=DXFDocument::parse(drawing(block("B","0\nHATCH\n"),insert));
        check(unsupported.entities.empty() && unsupported.warnings.size()==1);
        auto styled=DXFDocument::parse(drawing(block("B","0\nLINE\n62\n1\n10\n0\n20\n0\n11\n1\n21\n0\n"),insert));
        check(styled.entities.empty() && styled.warnings.size()==1);
        std::cout<<"BLOCK resolution, affine instances, nesting, arrays, snaps, transforms and round-trip passed.\n";
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
