#pragma once
#include <DXFCAD/DXFDocument.h>
#include <set>
namespace DXFCAD {
// Shared native-record editing utilities retain application groups and XDATA.
DXFRecord readDXFGroups(const std::string& ascii);
DXFRecord nativeEntityFields(const DXFRecord& record);
std::string patchEntityFields(const DXFRecord& record, const std::map<int,double>& values);
std::string patchEntityStringFields(const DXFRecord& record, const std::map<int,std::string>& values, const std::set<int>& removed = {});
struct LineFilletResult {
    std::string first, second, arc;
};
struct LineChamferResult {
    std::string first, second, connector;
};
class Drafting {
public:
    static LineChamferResult chamferLines(const std::string& first, const std::string& second,
                                         double firstDistance, double secondDistance,
                                         bool keepFirstStart = true, bool keepSecondStart = false);
    // Retain the selected endpoint of each LINE; trim/extend the other endpoint
    // to a tangent point and return a new, identity-free native ARC.
    static LineFilletResult filletLines(const std::string& first, const std::string& second,
                                       double radius, bool keepFirstStart = true,
                                       bool keepSecondStart = false);
    static std::string properties(const std::string& raw, const std::string& layer, int color, bool visible,
                                  const std::string& context = "");
    static std::string setLayer(const std::string& drawing, const std::string& name, int color, bool on);
    static TextGeometry textInfo(const std::string& raw);
    static std::string editText(const std::string& raw, const std::string& content, double height, double width = 0);
    static std::string createText(const std::string& content, double height, const Vector3D& origin,
                                  bool multiline = false, double width = 0, const std::string& layer = "0");
    static std::string chamfer(const std::string& raw, std::size_t vertex, double incoming, double outgoing);
    static std::string fillet(const std::string& raw, std::size_t vertex, double radius);
    static std::string offset(const std::string& raw, double distance);
    // Endpoint editing of LINE, ARC and open LWPOLYLINE against finite planar
    // LINE/ARC/CIRCLE/LWPOLYLINE boundaries. Trim retains the opposite endpoint.
    static std::string trim(const std::string& target, const std::string& boundary,
                            bool start, bool extend = false);
    static std::string trimLine(const std::string& target, const std::string& boundary,
                                bool start, bool extend = false);
private:
    static std::string offsetPolyline(const std::string& raw, double distance);
};
}
