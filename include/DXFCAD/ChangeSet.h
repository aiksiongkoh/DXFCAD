#pragma once

namespace DXFCAD {

// A calculated, validated set of proposed domain changes.
//
// Successful construction of a concrete change set establishes its operation's
// invariants. It owns its results, exposes read-only inspection and does not
// mutate the source drawing or apply edits to FreeCAD objects. Specific record
// roles and drawing results belong to the concrete type's interface.
//
// This abstract base expresses that common domain identity. It deliberately
// promises neither records(), drawingBytes() nor apply(). Application plans
// contain change sets and remain responsible for applying the proposed edits.
class ChangeSet {
public:
    virtual ~ChangeSet() = 0;

protected:
    ChangeSet() = default;
    ChangeSet(const ChangeSet&) = default;
    ChangeSet(ChangeSet&&) noexcept = default;
    ChangeSet& operator=(const ChangeSet&) = default;
    ChangeSet& operator=(ChangeSet&&) noexcept = default;
};

inline ChangeSet::~ChangeSet() = default;

} // namespace DXFCAD
