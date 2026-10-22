"""Exercise the actual LWPolyline task panel in an isolated FreeCAD GUI."""
import FreeCAD
import FreeCADGui
from pathlib import Path
import tempfile
import DXFCAD
from PySide import QtWidgets
from dxfcad_polyline_ui import show_editor
from dxfcad_edit import drawing_roots
from dxfcad_export import export_dxf

assert FreeCAD.GuiUp
profile = Path(FreeCAD.getUserAppDataDir())
assert str(profile) in DXFCAD.__file__

FreeCADGui.activateWorkbench("DXFCADWorkbench")
doc = FreeCAD.newDocument("PolylineEditor")
panel = show_editor()
panel.width.setText("2")
panel.insert_vertex()
panel.table.item(2,0).setText("10")
panel.table.item(2,1).setText("10")
assert panel.accept(), panel.error.text()
obj = panel.obj
assert obj.Shape.isValid() and abs(obj.Shape.Area-40) < 1e-7
assert len(drawing_roots(doc)) == 1
original = obj.DXFData
doc.undo()
assert len(doc.Objects) == 0
doc.redo()
doc.recompute()
obj = next(o for o in doc.Objects if "DXFData" in o.PropertiesList)

# Cancel leaves the native record and document unchanged.
panel = show_editor(obj)
panel.table.item(1,0).setText("50")
panel.reject()
assert obj.DXFData == original

# Invalid native values remain in the panel for correction.
panel = show_editor(obj)
panel.table.item(0,3).setText("-1")
assert not panel.accept()
assert panel.error.text() and obj.DXFData == original
panel.table.item(0,3).setText("0")
panel.parameters[0].setText("3")
panel.parameters[1].setText("4")
panel.transform()
assert not panel.error.text(), panel.error.text()
assert float(panel.table.item(0,0).text()) == 3
assert float(panel.table.item(0,1).text()) == 4
assert panel.accept(), panel.error.text()
assert obj.DXFData != original
edited = obj.DXFData
doc.undo()
doc.recompute()
assert obj.DXFData == original
doc.redo()
doc.recompute()
assert obj.DXFData == edited

# Double-click enters the same editor; save a visual QA artifact.
assert obj.ViewObject.Proxy.doubleClicked(obj.ViewObject)
FreeCADGui.activeDocument().activeView().viewTop()
FreeCADGui.activeDocument().activeView().fitAll()
FreeCADGui.updateGui()
FreeCADGui.getMainWindow().grab().save(str(profile / "polyline-editor.png"))
# Capture the widget separately: some OpenGL drivers cannot grab a whole window.
widget = FreeCADGui.getMainWindow().findChild(QtWidgets.QWidget, "DXFCADPolylineEditor")
assert widget is not None
widget.grab().save(str(profile / "polyline-panel.png"))
FreeCADGui.Control.closeDialog()

with tempfile.TemporaryDirectory(dir=profile) as temp:
    saved = Path(temp) / "editor.FCStd"
    output = Path(temp) / "editor.dxf"
    doc.saveAs(str(saved))
    FreeCAD.closeDocument(doc.Name)
    doc = FreeCAD.openDocument(str(saved))
    doc.recompute()
    obj = next(o for o in doc.Objects if "DXFData" in o.PropertiesList)
    assert obj.DXFData == edited
    export_dxf(output, doc)
    parsed = DXFCAD.DXFDocument.read(str(output))
    assert len(parsed.entities) == 1
    assert DXFCAD.read_lwpolyline(parsed.entities[0].raw).vertices[0].x == 3
    # Restored view provider still opens the editor.
    assert obj.ViewObject.Proxy.doubleClicked(obj.ViewObject)
    FreeCADGui.Control.closeDialog()

# Drawing changes while the table is open are rejected by the C++ session.
from dxfcad_edit import create_line
panel = show_editor(obj)
panel.table.item(1, 0).setText("80")
create_line(doc, DXFCAD.Vector3D(), DXFCAD.Vector3D(2, 0, 0), obj.DXFSourceRoot)
assert not panel.accept()
assert "changed" in panel.error.text().lower(), panel.error.text()
assert obj.DXFData == edited
panel.reject()
