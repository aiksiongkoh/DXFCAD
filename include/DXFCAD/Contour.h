#pragma once
#include <DXFCAD/DXFDocument.h>

namespace DXFCAD {
// Distances follow the native traversal, in drawing units. Topology edits
// return records for the retained pieces; the first retains identity when its
// type is unchanged, and subsequent pieces have no document identity.
class Contour {
public:
    static double length(const std::string& raw);
    static double distanceAtPoint(const std::string& raw, Vector3D point);
    static double distanceAtRay(const std::string& raw, Vector3D start, Vector3D end);
    static std::vector<std::string> split(const std::string& raw, const std::vector<double>& distances);
    static std::vector<std::string> breakBetween(const std::string& raw, double first, double second);
    static std::string close(const std::string& raw, double tolerance = 1e-7);
    static std::string join(const std::vector<std::string>& entities, double tolerance = 1e-7);
    static std::vector<std::string> trimInterior(const std::string& raw,
        const std::vector<std::string>& boundaries, double pickDistance);
};

// Independent domain plan. Selection indices address live records, including
// new entities whose source slot is -1. Construction validates the whole result.
class ContourEditPlan {
public:
    ContourEditPlan(const DXFDocument& source,
                    const std::vector<std::pair<int,std::string>>& live,
                    const std::vector<std::size_t>& selected,
                    const std::vector<std::string>& results, bool preserveFirst = true);
    bool preservesFirst() const noexcept { return keepFirst_; }
    const std::vector<std::string>& records() const noexcept { return records_; }
    const std::string& drawingBytes() const noexcept { return drawingBytes_; }
private:
    bool keepFirst_ = false;
    std::vector<std::string> records_;
    std::string drawingBytes_;
};
}
