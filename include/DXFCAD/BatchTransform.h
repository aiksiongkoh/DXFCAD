#pragma once
#include <DXFCAD/DXFDocument.h>

namespace DXFCAD {
struct BatchTransformInput {
    std::string raw, context;
};

// Independent, immutable record plan. FreeCAD ownership and transactions are
// handled by the persistence adapter; entity algorithms remain in DXFEntity.
class BatchTransformPlan {
public:
    BatchTransformPlan(const std::vector<BatchTransformInput>& inputs,
                       const std::string& operation, Vector3D offset = {},
                       double value = 0, Vector3D pivot = {}, bool allowIdentity = false);
    bool copies() const noexcept { return copies_; }
    const std::vector<std::string>& records() const noexcept { return records_; }
private:
    bool copies_ = false;
    std::vector<std::string> records_;
};
}
