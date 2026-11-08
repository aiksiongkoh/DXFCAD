#pragma once

#include <DXFCAD/Drawing.h>

namespace DXFCAD::FreeCAD {

struct LayoutPath {
    std::vector<std::vector<Vector3D>> contours;
    std::vector<double> color;
    bool filled = false;
};

// A read-only, disposable sheet display. Never creates or modifies document objects.
class LayoutPreview {
public:
    LayoutPreview(const Drawing& drawing,const std::string& layout,const std::string& font,double tolerance);
    std::vector<LayoutPath> paths;
    std::vector<PaperRectangle> viewports;
    std::optional<PaperRectangle> limits;
    std::vector<std::string> warnings;
};

} // namespace DXFCAD::FreeCAD
