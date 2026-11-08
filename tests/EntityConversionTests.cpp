#include <DXFCAD/EntityConversion.h>
#include <DXFCAD/Drafting.h>
#include <DXFCAD/Contour.h>
#include <DXFCAD/ParametricCurves.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace DXFCAD;
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void close(double a,double b) { check(std::abs(a-b)<1e-8,"Native conversion geometry mismatch"); }
int main()
{
    try {
        const auto raw=Drafting::properties(createLWPolyline(DXFLWPolyline({{0,0,1},{2,0},{3,0}},false,.1),"Example"),"Example",4,true)+"1001\nSynthetic\n1000\npreserve\n";
        const auto before=parseEntity(raw)->raw();
        const auto pieces=EntityConversion::explode(*parseEntity(raw));
        check(pieces.size()==2,"Polyline segment count");
        check(parseEntity(pieces[0])->type()=="ARC" && parseEntity(pieces[1])->type()=="LINE","Analytic bulge lost");
        close(parseEntity(pieces[0])->geometry().segments.at(0).arc->radius,1);
        for (const auto& piece : pieces) {
            const auto e=parseEntity(piece); check(e->handle().empty(),"Exploded identity retained");
            check(e->layer()=="Example" && e->record().integer(62)==4,"Properties changed");
            check(piece.find("preserve")!=std::string::npos,"Application data lost");
        }
        check(before==parseEntity(raw)->raw(),"Conversion modified source");
        const std::string legacy="0\nPOLYLINE\n5\nA0\n8\nExample\n66\n1\n70\n0\n10\n0\n20\n0\n30\n2\n1001\nSynthetic\n1000\nparent\n0\nVERTEX\n5\nA1\n8\nExample\n10\n0\n20\n0\n42\n1\n0\nVERTEX\n5\nA2\n8\nExample\n10\n2\n20\n0\n0\nVERTEX\n5\nA3\n8\nExample\n10\n3\n20\n0\n0\nSEQEND\n5\nA4\n8\nExample\n";
        const auto legacyDocument=Drawing::parse("0\nSECTION\n2\nENTITIES\n"+legacy+"0\nLINE\n8\n0\n10\n0\n20\n0\n11\n1\n21\n0\n0\nENDSEC\n0\nEOF\n");
        check(legacyDocument.entities.size()==2 && legacyDocument.warnings.empty(),"Compound polyline must occupy one document slot");
        const auto styledLegacy=Drafting::properties(legacy, "Other",3,true);
        check(parseEntity(styledLegacy)->geometry().segments.size()==2,"Parent property edit altered compound geometry");
        check(styledLegacy.find("0\nVERTEX\n5\nA1\n8\nExample")!=std::string::npos,"Parent properties changed vertex metadata");
        const auto oldPieces=EntityConversion::explode(*legacyDocument.entities[0]);
        check(oldPieces.size()==2 && parseEntity(oldPieces[0])->type()=="ARC","Legacy polyline bulge lost");
        close(parseEntity(oldPieces[0])->geometry().segments.at(0).start.z,2);
        check(oldPieces[0].find("parent")!=std::string::npos && oldPieces[0].find("VERTEX")==std::string::npos,"Compound attributes contaminated exploded record");
        const auto without=Drawing::parse(legacyDocument.serialize({}, {0}));
        check(without.entities.size()==1 && without.warnings.empty(),"Compound slot removal left orphan vertices");
        const auto copiedLegacy=parseEntity(legacy)->copied({2,3,4});
        check(copiedLegacy.find("A0")==std::string::npos && copiedLegacy.find("A4")==std::string::npos,"Compound copy retained identity");
        const auto movedLegacy=parseEntity(copiedLegacy)->geometry().segments[0];
        close(movedLegacy.start.x,2); close(movedLegacy.start.y,3); close(movedLegacy.start.z,6);
        const auto clockwise=EntityConversion::explode(*parseEntity(createLWPolyline(DXFLWPolyline({{0,0,-1},{2,0}}))));
        close(parseEntity(clockwise.at(0))->geometry().segments.at(0).arc->radius,1);
        const auto circle=createCircle({2,3,0},2);
        const auto full=EntityConversion::circleAsArc(circle);
        close(Contour::length(full),4*std::acos(-1.0));
        check(Contour::split(full,{std::acos(-1.0)}).size()==2,"Full-turn ARC could not split");
        bool rejected=false;
        try { parseEntity("0\nARC\n10\n0\n20\n0\n40\n1\n50\n0\n51\n0\n")->geometry(); }
        catch (const std::invalid_argument&) { rejected=true; }
        check(rejected,"Ambiguous equal-angle ARC accepted");
        const auto eraw=readDXFGroups("0\nELLIPSE\n8\n0\n10\n2\n20\n3\n11\n2\n21\n0\n40\n.5\n41\n0\n42\n6.283185307179586\n");
        const DXFEllipse ellipse(eraw);
        close(ellipse.atParameter(0).x,4); close(ellipse.atParameter(std::acos(-1.0)/2).y,4);
        close(ellipse.radiusOfCurvature(0),.5); close(ellipse.radiusOfCurvature(std::acos(-1.0)/2),4);
        check(parseEntity(eraw.raw())->type()=="ELLIPSE","Native ellipse factory missing");
        const auto sraw=readDXFGroups("0\nSPLINE\n8\n0\n70\n4\n71\n2\n72\n6\n73\n3\n40\n0\n40\n0\n40\n0\n40\n1\n40\n1\n40\n1\n10\n1\n20\n0\n10\n1\n20\n1\n10\n0\n20\n1\n41\n1\n41\n0.7071067811865476\n41\n1\n");
        const DXFSpline spline(sraw);
        const auto middle=spline.atParameter(.5);
        close(middle.x,std::sqrt(.5)); close(middle.y,std::sqrt(.5));
        close(spline.atParameter(0).x,1); close(spline.atParameter(1).y,1);
        const DXFSpline moved(readDXFGroups(spline.copied({2,3,0})));
        close(moved.atParameter(.5).x,middle.x+2); close(moved.atParameter(.5).y,middle.y+3);
        const DXFEllipse rotatedEllipse(readDXFGroups(ellipse.rotated(90,{0,0,0})));
        close(rotatedEllipse.atParameter(rotatedEllipse.startParameter()).x,-3);
        close(rotatedEllipse.atParameter(rotatedEllipse.startParameter()).y,4);
        const DXFEllipse scaledEllipse(readDXFGroups(ellipse.scaled(2,{0,0,0})));
        close(scaledEllipse.atParameter(scaledEllipse.startParameter()).x,8);
        const DXFSpline rotatedSpline(readDXFGroups(spline.rotated(90,{0,0,0})));
        close(rotatedSpline.atParameter(.5).x,-middle.y); close(rotatedSpline.atParameter(.5).y,middle.x);
        const DXFSpline reflectedSpline(readDXFGroups(spline.mirrored(0,{0,0,0})));
        close(reflectedSpline.atParameter(.5).x,middle.x); close(reflectedSpline.atParameter(.5).y,-middle.y);
        const BlockPart affine{{},{2,3,0},{2,1,0},{1,3,0},{0,0,1}};
        const DXFEllipse affineEllipse(readDXFGroups(affineTransformedCurve(ellipse,affine)));
        for (double fraction : {0.,.25,.5,.75,1.}) {
            const auto expected=affine.transformPoint(ellipse.atParameter(ellipse.startParameter()+fraction*(ellipse.endParameter()-ellipse.startParameter())));
            const auto actual=affineEllipse.atParameter(affineEllipse.startParameter()+fraction*(affineEllipse.endParameter()-affineEllipse.startParameter()));
            close(actual.x,expected.x); close(actual.y,expected.y);
        }
        const auto block=Drawing::parse("0\nSECTION\n2\nBLOCKS\n0\nBLOCK\n2\nB\n10\n1\n20\n1\n0\nLINE\n8\n0\n10\n1\n20\n1\n11\n2\n21\n1\n0\nENDBLK\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nINSERT\n8\n0\n2\nB\n10\n10\n20\n20\n41\n2\n42\n3\n50\n90\n0\nENDSEC\n0\nEOF\n");
        const auto exploded=EntityConversion::explode(*block.entities.at(0));
        const auto s=parseEntity(exploded.at(0))->geometry().segments.at(0);
        close(s.start.x,10); close(s.start.y,20); close(s.end.x,10); close(s.end.y,22);
        const auto scaledCircleBlock=Drawing::parse("0\nSECTION\n2\nBLOCKS\n0\nBLOCK\n2\nC\n10\n0\n20\n0\n0\nCIRCLE\n8\n0\n10\n0\n20\n0\n40\n1\n0\nENDBLK\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nINSERT\n8\n0\n2\nC\n10\n10\n20\n20\n41\n2\n42\n3\n0\nENDSEC\n0\nEOF\n");
        const auto elliptic=EntityConversion::explode(*scaledCircleBlock.entities.at(0));
        const DXFEllipse nativeEllipse(readDXFGroups(elliptic.at(0)));
        close(nativeEllipse.center().x,10); close(nativeEllipse.center().y,20);
        const auto ellipticStart=nativeEllipse.atParameter(nativeEllipse.startParameter());
        close(ellipticStart.x,12); close(ellipticStart.y,20);
        const auto ellipticQuarter=nativeEllipse.atParameter(nativeEllipse.startParameter()+std::acos(-1.0)/2);
        close(ellipticQuarter.x,10); close(ellipticQuarter.y,23);
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
