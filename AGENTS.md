# DXFCAD Development Instructions

DXFCAD is a standalone FreeCAD workbench for native DXF objects.

## Development guidance

Read [ARCHITECTURE.md](ARCHITECTURE.md) before designing or substantially changing
classes, interfaces, inheritance, ownership, or dependencies.

Read [VERIFICATION.md](VERIFICATION.md) before implementing or modifying behavior,
creating tests, or evaluating correctness.

## Project rules

- Use FreeCAD's existing APIs whenever possible.
- Keep this repository independent of FreeCAD and XynaCAD.
- DXFCAD must work without XynaCAD installed.
- Keep the dependency direction XynaCAD -> DXFCAD -> FreeCAD.
- Do not modify FreeCAD source without permission.
- Do not commit proprietary code to public repositories.
- Use C++ for domain objects and algorithms, including geometry, transformations,
  domain validation, and DXF import/export semantics.
- Use Python for UI objects and algorithms, including workbenches, commands,
  dialogs, task panels, and interactive editing workflows.
- Expose C++ domain APIs to Python; keep domain behavior out of UI code.
- Preserve native DXF entity data as authoritative and derive display geometry
  from it.
- Favor Object-Oriented Programming.
- Model DXF Reference keywords and named concepts under DXFItem, preserving
  meaningful subhierarchies. DXFItem is a project-defined conceptual grouping;
  native record, geometry and coordinate APIs belong to the relevant subtypes.
- Use CartesianFrame and Vector3D for mathematical frames and vectors, including
  extrusion values. Keep these generic types outside DXFItem; specify the native
  extrusion default (0, 0, 1) at the entity boundary.
- Use the ChangeSet suffix for domain objects that hold calculated, validated
  proposed changes without applying them. Keep their names distinct from the
  FreeCAD adapter objects that apply changes, including in Python bindings.
- Prefix a type name with DXF only when its name exists in the DXF reference
  and the type uses that name with the reference's meaning. Use unprefixed
  names for workbench abstractions and derived geometry. DXFCAD is the project
  name, not a claim of a DXF format construct. Use the correct names throughout;
  do not retain mistaken names as compatibility aliases or runtime type names.
- Develop and test one feature at a time.
- Implement LWPolyline first, with a C++ domain object and Python editing UI.
- Keep specialized XynaCAD manufacturing rules in the XynaCAD repository.
