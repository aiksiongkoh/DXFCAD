#pragma once
#include <DXFCAD/ChangeSet.h>
#include <DXFCAD/Drawing.h>

namespace DXFCAD {
struct BatchTransformInput {
    std::string raw, context;
};

// Calculated record changes with read-only access. FreeCAD ownership and transactions are
// handled by the persistence adapter; entity algorithms remain in DXFEntity.
class BatchTransformChangeSet : public ChangeSet {
public:
    BatchTransformChangeSet(const std::vector<BatchTransformInput>& inputs,
                       const std::string& operation, Vector3D offset = {},
                       double value = 0, Vector3D pivot = {}, bool allowIdentity = false);
    bool copies() const noexcept { return copies_; }
    const std::vector<std::string>& records() const noexcept { return records_; }
private:
    bool copies_ = false;
    std::vector<std::string> records_;
};
}
