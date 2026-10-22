#include <DXFCAD/BatchTransform.h>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <iostream>
using namespace DXFCAD;
namespace {
void check(bool value) { if (!value) throw std::runtime_error("Batch transform check failed"); }
void near(double a,double b) { check(std::abs(a-b)<1e-8); }
template<class F> void rejects(F f) {
    try { f(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid batch transform accepted");
}
}
int main() {
    try {
        const std::string line="0\nLINE\n5\nAB\n330\nF\n10\n1\n20\n2\n11\n3\n21\n2\n1001\nTEST\n1000\nretain me\n";
        const std::string circle="0\nCIRCLE\n5\nBC\n10\n10\n20\n2\n40\n3\n";
        const std::vector<BatchTransformInput> inputs{{line,""},{circle,""}};
        BatchTransformPlan move(inputs,"move",{4,5,6});
        check(!move.copies() && move.records().size()==2);
        auto start=parseEntity(move.records()[0])->geometry().segments[0].start;
        near(start.x,5); near(start.y,7); near(start.z,6);
        check(parseEntity(move.records()[0])->handle()=="AB");
        BatchTransformPlan copy(inputs,"copy",{4,5,6});
        check(copy.copies() && parseEntity(copy.records()[0])->handle().empty());
        check(copy.records()[0].find("retain me")!=std::string::npos);
        check(copy.records()[0].find("330\n")==std::string::npos);
        BatchTransformPlan rotate(inputs,"rotate",{},90,{});
        start=parseEntity(rotate.records()[0])->geometry().segments[0].start;
        near(start.x,-2); near(start.y,1);
        BatchTransformPlan scale(inputs,"scale",{},2,{1,2,0});
        near(parseEntity(scale.records()[0])->geometry().segments[0].end.x,5);
        near(parseEntity(scale.records()[1])->geometry().circles[0].radius,6);
        BatchTransformPlan mirror(inputs,"mirror",{},0,{});
        near(parseEntity(mirror.records()[0])->geometry().segments[0].start.y,-2);
        check(inputs[0].raw==line && inputs[1].raw==circle);
        check(BatchTransformPlan(inputs,"move",{},0,{},true).records().size()==2);
        check(BatchTransformPlan(inputs,"copy").records().size()==2);
        rejects([&]{BatchTransformPlan(inputs,"move");});
        rejects([&]{BatchTransformPlan({},"copy");});
        rejects([&]{BatchTransformPlan(inputs,"unknown");});
        rejects([&]{BatchTransformPlan(inputs,"scale",{},0);});
        rejects([&]{BatchTransformPlan(inputs,"scale",{},-1);});
        rejects([&]{BatchTransformPlan(inputs,"rotate",{},std::numeric_limits<double>::quiet_NaN());});
        rejects([&]{BatchTransformPlan(inputs,"copy",{std::numeric_limits<double>::infinity(),0,0});});
        rejects([&]{BatchTransformPlan({{line,""},{"0\nCIRCLE\n40\n-1\n",""}},"move",{1,0,0});});
        const std::string block="0\nBLOCK\n2\nSymbol\n10\n0\n20\n0\n0\nLINE\n10\n0\n20\n0\n11\n2\n21\n0\n0\nENDBLK\n";
        const std::string insert="0\nINSERT\n5\nCD\n2\nSymbol\n10\n1\n20\n2\n";
        const BatchTransformPlan instance({{insert,block}},"copy",{3,0,0});
        // INSERT geometry is supplied through resolved block parts.
        const auto entity=parseEntity(instance.records()[0],block);
        near(entity->blockParts()[0].origin.x,4);
        std::cout<<"Batch move, copy, rotate, scale, mirror, identity and validation passed\n";
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
