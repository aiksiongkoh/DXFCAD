#pragma once

namespace DXFCAD {

// A modeled concept whose name and meaning come from the DXF Reference.
//
// This project-defined grouping includes native entities and table metadata.
// Generic mathematical frames and vectors are separate types. DXFItem is not a DXF
// keyword or record type. Record/handle access, geometry and transformations
// remain responsibilities of the concrete concepts where they have meaning.
// FreeCAD persistence adapters and workbench abstractions are separate types.
class DXFItem {
public:
    virtual ~DXFItem() = 0;

protected:
    DXFItem() = default;
    DXFItem(const DXFItem&) = default;
    DXFItem(DXFItem&&) noexcept = default;
    DXFItem& operator=(const DXFItem&) = default;
    DXFItem& operator=(DXFItem&&) noexcept = default;
};

inline DXFItem::~DXFItem() = default;

} // namespace DXFCAD
