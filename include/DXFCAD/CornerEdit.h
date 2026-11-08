#pragma once
#include <DXFCAD/ChangeSet.h>
#include <DXFCAD/Drawing.h>
#include <array>

namespace DXFCAD {
// A change set containing two LINE updates and one identity-free LINE/ARC connector.
// Construction validates the full proposed drawing without changing its source.
class CornerChangeSet : public ChangeSet {
public:
    CornerChangeSet(const Drawing& source,
                   const std::vector<std::pair<int,std::string>>& live,
                   const std::array<std::size_t,2>& selected,
                   const std::vector<std::string>& updates,
                   const std::string& connector,const std::string& connectorType);
    const std::vector<std::string>& updates() const noexcept { return updates_; }
    const std::string& connector() const noexcept { return connector_; }
    const std::string& drawingBytes() const noexcept { return drawingBytes_; }
private:
    std::vector<std::string> updates_;
    std::string connector_,drawingBytes_;
};
}
