"""Offset previews, copies/in-place mode, atomic validation, undo and export."""
import FreeCAD
import FreeCADGui
from pathlib import Path
import tempfile
import DXFCAD
from dxfcad_edit import create_line, create_circle, create_rectangle, _create_entity
from dxfcad_drafting_ui import show_drafting_tool, OffsetTaskPanel
from dxfcad_export import export_dxf
FreeCADGui.activateWorkbench("DXFCADWorkbench")
assert "DXFCAD_Offset" in FreeCADGui.listCommands()
profile=Path(FreeCAD.getUserAppDataDir())
doc=FreeCAD.newDocument("Offsets")
line=create_line(doc,DXFCAD.Vector3D(0,0,2),DXFCAD.Vector3D(10,0,2))
circle=create_circle(doc,DXFCAD.Vector3D(0,10,2),2)
rect=create_rectangle(doc,DXFCAD.Vector3D(20,0,2),DXFCAD.Vector3D(30,6,2))
arc=_create_entity(doc,"0\nARC\n100\nAcDbEntity\n8\n0\n100\nAcDbCircle\n10\n20\n20\n10\n30\n2\n40\n5\n100\nAcDbArc\n50\n0\n51\n90\n",None,"ARC")
objects=[line,circle,rect,arc]
before={o.Name:o.DXFData for o in objects}
panel=show_drafting_tool(OffsetTaskPanel,objects)
FreeCADGui.updateGui()
assert panel.form.grab().save(str(profile/"offset-panel.png"))
panel.preview()
assert not panel.error.text(),panel.error.text()
assert panel.viewport_tool.overlay is not None
assert before=={o.Name:o.DXFData for o in objects}
preview_plan=panel.offset_plan
assert isinstance(preview_plan,DXFCAD.OffsetPlan) and preview_plan.copies
predicted=preview_plan.drawing_bytes[line.DXFSourceRoot.Name]
panel.apply()
assert not panel.error.text(),panel.error.text()
assert DXFCAD.current_drawing_bytes(line.DXFSourceRoot)==predicted
assert panel.offset_plan is None
copies=[o for o in doc.Objects if "DXFData" in o.PropertiesList and o.Name not in before]
assert len(copies)==4
assert before=={o.Name:o.DXFData for o in objects}
assert abs(copies[0].Shape.BoundBox.YMin-1)<1e-7
assert abs(copies[1].Shape.Length-6*3.141592653589793)<1e-7
assert abs(copies[2].Shape.Length-24)<1e-7
names=[o.Name for o in copies]
doc.undo()
assert all(doc.getObject(n) is None for n in names)
doc.redo()
assert all(doc.getObject(n) is not None for n in names)
panel.keep.setChecked(False)
panel.apply()
assert not panel.error.text(),panel.error.text()
after={o.Name:o.DXFData for o in objects}
assert before!=after
panel.distance.setText("-4")
panel.apply()
assert "positive" in panel.error.text() and after=={o.Name:o.DXFData for o in objects}
panel.distance.setText("0")
panel.preview()
assert "nonzero" in panel.error.text() and panel.viewport_tool.overlay is None
panel.reject()
doc.undo()
assert before=={o.Name:o.DXFData for o in objects}
doc.redo()
assert after=={o.Name:o.DXFData for o in objects}
with tempfile.TemporaryDirectory(dir=profile) as temp:
    out=Path(temp)/"offset.dxf"
    export_dxf(out,doc)
    parsed=DXFCAD.DXFDocument.read(str(out))
    assert len(parsed.entities)==8 and len(set(e.handle for e in parsed.entities))==8
    saved=Path(temp)/"offset.FCStd"
    doc.saveAs(str(saved))
    FreeCAD.closeDocument(doc.Name)
    reopened=FreeCAD.openDocument(str(saved))
    assert after=={n:reopened.getObject(n).DXFData for n in after}
    FreeCAD.closeDocument(reopened.Name)
print("Offset LINE/ARC/CIRCLE/straight polyline: preview, copy/in-place, atomic validation, undo and export passed.")

# Apply the previewed plan, including its dependency snapshot, without replanning.
doc=FreeCAD.newDocument("StaleOffsetPreview")
line=create_line(doc,DXFCAD.Vector3D(),DXFCAD.Vector3D(2,0,0))
panel=show_drafting_tool(OffsetTaskPanel,[line])
panel.preview()
assert not panel.error.text(),panel.error.text()
preview_plan=panel.offset_plan
create_line(doc,DXFCAD.Vector3D(0,3,0),DXFCAD.Vector3D(2,3,0),line.DXFSourceRoot)
names={o.Name for o in doc.Objects}
panel.apply()
assert "changed" in panel.error.text().lower(),panel.error.text()
assert panel.offset_plan is preview_plan
assert {o.Name for o in doc.Objects}==names
panel.reject()
FreeCAD.closeDocument(doc.Name)
