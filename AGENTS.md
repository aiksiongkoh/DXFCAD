# DXFCAD Development Instructions

DXFCAD is a standalone FreeCAD workbench for native DXF objects.

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
- Develop and test one feature at a time.
- Implement LWPolyline first, with a C++ domain object and Python editing UI.
- Keep specialized XynaCAD manufacturing rules in the XynaCAD repository.
