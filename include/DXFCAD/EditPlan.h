#pragma once

namespace DXFCAD {

// A prepared edit to persistent FreeCAD document objects.
//
// Concrete plans validate and retain the information needed for their edits.
// Their application runs inside a transaction owned by the caller. A plan may
// contain a domain ChangeSet or delegate to other plans; it is not a ChangeSet
// or a UI command.
//
// Application results, preview access, stale-state checks and reuse rules belong
// to each concrete interface. This abstract base supplies the common identity
// and safe polymorphic destruction without prescribing a uniform apply().
class EditPlan {
public:
    virtual ~EditPlan() = 0;

protected:
    EditPlan() = default;
    EditPlan(const EditPlan&) = default;
    EditPlan(EditPlan&&) noexcept = default;
    EditPlan& operator=(const EditPlan&) = default;
    EditPlan& operator=(EditPlan&&) noexcept = default;
};

inline EditPlan::~EditPlan() = default;

} // namespace DXFCAD
