"""Native polyline chamfer editing through its task panel."""
import FreeCAD
import FreeCADGui
from pathlib import Path
import tempfile
import math
import DXFCAD
from dxfcad_edit import create_polyline
from dxfcad_drafting_ui import show_drafting_tool, ChamferTaskPanel
from dxfcad_export import export_dxf
FreeCADGui.activateWorkbench("DXFCADWorkbench")
assert "DXFCAD_Chamfer" in FreeCADGui.listCommands()
profile=Path(FreeCAD.getUserAppDataDir())
doc=FreeCAD.newDocument("Chamfers")
obj=create_polyline(doc,DXFCAD.LWPolyline([DXFCAD.LWPolylineVertex(0,0),DXFCAD.LWPolylineVertex(10,0),DXFCAD.LWPolylineVertex(10,10)],False,0,2))
original=obj.DXFData
panel=show_drafting_tool(ChamferTaskPanel,[obj])
panel.amount.setText("2")
panel.outgoing.setText("3")
panel.preview()
assert not panel.error.text(),panel.error.text()
assert panel.viewport_tool.overlay is not None and obj.DXFData==original
preview_plan=panel.corner_plan
assert isinstance(preview_plan,DXFCAD.EntityUpdatePlan)
predicted=preview_plan.drawing_bytes[obj.DXFSourceRoot.Name]
FreeCADGui.updateGui()
assert panel.form.grab().save(str(profile/"chamfer-panel.png"))
panel.amount.setText("100")
panel.preview()
assert "fit" in panel.error.text() and obj.DXFData==original
panel.amount.setText("2")
panel.outgoing.setText("3")
panel.apply()
assert not panel.error.text(),panel.error.text()
assert len(DXFCAD.read_lwpolyline(obj.DXFData).vertices)==4
assert DXFCAD.current_drawing_bytes(obj.DXFSourceRoot)==predicted
assert panel.corner_plan is None
assert abs(obj.Shape.Length-(15+math.sqrt(13)))<1e-7
assert abs(obj.Shape.BoundBox.ZMin-2)<1e-7
rounded=obj.DXFData
panel.reject()
doc.undo()
assert obj.DXFData==original
doc.redo()
assert obj.DXFData==rounded
with tempfile.TemporaryDirectory(dir=profile) as temp:
    path=Path(temp)
    out=path/"chamfer.dxf"
    export_dxf(out,doc)
    parsed=DXFCAD.DXFDocument.read(str(out))
    assert len(parsed.entities)==1 and DXFCAD.read_lwpolyline(parsed.entities[0].raw).vertices[1].bulge==0
    saved=path/"chamfer.FCStd"
    name=obj.Name
    doc.saveAs(str(saved))
    FreeCAD.closeDocument(doc.Name)
    doc=FreeCAD.openDocument(str(saved))
    assert doc.getObject(name).DXFData==rounded
    assert abs(doc.getObject(name).Shape.Length-(15+math.sqrt(13)))<1e-7
    FreeCAD.closeDocument(doc.Name)
print("Polyline chamfer: preview, distance validation, undo/redo, persistence and export passed.")

doc=FreeCAD.newDocument("StaleChamferPreview")
obj=create_polyline(doc,DXFCAD.LWPolyline([DXFCAD.LWPolylineVertex(0,0),DXFCAD.LWPolylineVertex(10,0),DXFCAD.LWPolylineVertex(10,10)]))
panel=show_drafting_tool(ChamferTaskPanel,[obj])
panel.preview()
assert not panel.error.text(),panel.error.text()
plan=panel.corner_plan
raw=obj.DXFData
DXFCAD.ensure_layer(obj.DXFSourceRoot,"ChangedDrawing",3,True)
panel.apply()
assert "changed" in panel.error.text().lower(),panel.error.text()
assert panel.corner_plan is plan and obj.DXFData==raw
panel.reject()
FreeCAD.closeDocument(doc.Name)
