"""LINE trim/extend preview, endpoint choice, undo and persistence."""
import FreeCAD
import FreeCADGui
from pathlib import Path
import tempfile
import DXFCAD
from dxfcad_edit import create_line
from dxfcad_drafting_ui import show_drafting_tool, TrimTaskPanel, ExtendTaskPanel
from dxfcad_export import export_dxf
FreeCADGui.activateWorkbench("DXFCADWorkbench")
assert "DXFCAD_Trim" in FreeCADGui.listCommands() and "DXFCAD_Extend" in FreeCADGui.listCommands()
profile=Path(FreeCAD.getUserAppDataDir())
doc=FreeCAD.newDocument("Drafting")
line=create_line(doc,DXFCAD.Vector3D(0,0,2),DXFCAD.Vector3D(10,0,2))
cut=create_line(doc,DXFCAD.Vector3D(5,-5,2),DXFCAD.Vector3D(5,5,2))
far=create_line(doc,DXFCAD.Vector3D(15,-5,2),DXFCAD.Vector3D(15,5,2))
original=line.DXFData
panel=show_drafting_tool(TrimTaskPanel,[line,cut])
FreeCADGui.updateGui()
assert panel.form.grab().save(str(profile/"trim-panel.png"))
panel.preview()
assert not panel.error.text(),panel.error.text()
assert panel.viewport_tool.overlay is not None and line.DXFData==original
assert isinstance(panel.trim_plan,DXFCAD.TrimPlan)
assert panel.trim_plan.boundary==cut
panel.reject()
assert panel.viewport_tool.overlay is None and line.DXFData==original
panel=show_drafting_tool(TrimTaskPanel,[line,far])
panel.apply()
assert "inside" in panel.error.text() and line.DXFData==original
panel.reject()
panel=show_drafting_tool(TrimTaskPanel,[line,cut])
panel.apply()
assert not panel.error.text(),panel.error.text()
assert abs(line.Shape.Length-5)<1e-7
trimmed=line.DXFData
panel.reject()
doc.undo()
assert line.DXFData==original
doc.redo()
assert line.DXFData==trimmed
doc.undo()
panel=show_drafting_tool(ExtendTaskPanel,[line,far])
panel.preview()
assert panel.viewport_tool.overlay is not None
panel.apply()
assert not panel.error.text(),panel.error.text()
assert abs(line.Shape.Length-15)<1e-7
extended=line.DXFData
panel.reject()
doc.undo()
assert line.DXFData==original
doc.redo()
assert line.DXFData==extended
with tempfile.TemporaryDirectory(dir=profile) as temp:
    folder=Path(temp)
    out=folder/"extended.dxf"
    export_dxf(out,doc)
    parsed=DXFCAD.DXFDocument.read(str(out))
    assert len(parsed.entities)==3
    assert parsed.entities[0].snap_points()[1].position.x==15
    saved=folder/"extended.FCStd"
    name,cut_name=line.Name,cut.Name
    doc.saveAs(str(saved))
    FreeCAD.closeDocument(doc.Name)
    doc=FreeCAD.openDocument(str(saved))
    line=doc.getObject(name)
    assert line.DXFData==extended and abs(line.Shape.Length-15)<1e-7
    cut=doc.getObject(cut_name)
    assert cut is not None
    panel=show_drafting_tool(TrimTaskPanel,[line,cut])
    panel.preview()
    assert panel.viewport_tool.overlay is not None
    FreeCAD.closeDocument(doc.Name)
    assert panel.viewport_tool.overlay is None and panel.viewport_tool.scene is None
    panel.reject()
from dxfcad_edit import _create_entity

doc=FreeCAD.newDocument("CurvedDrafting")
arc_raw="0\nARC\n100\nAcDbEntity\n8\n0\n100\nAcDbCircle\n10\n0\n20\n0\n40\n5\n100\nAcDbArc\n50\n0\n51\n180\n"
curve=_create_entity(doc,arc_raw,None,"Arc")
cut=create_line(doc,DXFCAD.Vector3D(0,-10,0),DXFCAD.Vector3D(0,10,0))
poly_raw="0\nLWPOLYLINE\n100\nAcDbEntity\n8\n0\n100\nAcDbPolyline\n90\n3\n70\n0\n10\n-5\n20\n0\n42\n1\n40\n2\n41\n6\n91\n1\n10\n5\n20\n0\n91\n2\n10\n10\n20\n0\n91\n3\n1001\nTest\n1000\nkeep\n"
poly=_create_entity(doc,poly_raw,None,"LWPolyline")
far=create_line(doc,DXFCAD.Vector3D(-10,3,0),DXFCAD.Vector3D(10,3,0))
doc.recompute()
originals={obj.Name:obj.DXFData for obj in (curve,poly,cut,far)}
for obj in (curve,poly):
    panel=show_drafting_tool(TrimTaskPanel,[obj,cut])
    panel.endpoint.setCurrentIndex(0)
    panel.preview()
    assert not panel.error.text(),panel.error.text()
    assert panel.viewport_tool.overlay is not None
    assert obj.DXFData==originals[obj.Name]
    panel.apply()
    assert not panel.error.text(),panel.error.text()
    changed=obj.DXFData
    assert changed!=originals[obj.Name]
    assert not obj.Shape.isNull()
    panel.reject()
    doc.undo()
    assert obj.DXFData==originals[obj.Name]
    doc.redo()
    assert obj.DXFData==changed
assert abs(DXFCAD.parse_entity(curve.DXFData).snap_points()[0].position.y-5)<1e-7
model=DXFCAD.read_lwpolyline(poly.DXFData)
assert len(model.vertices)==3 and abs(model.vertices[0].start_width-4)<1e-7
assert "1000\nkeep\n" in poly.DXFData and "91\n1\n" in poly.DXFData
panel=show_drafting_tool(ExtendTaskPanel,[curve,far])
panel.endpoint.setCurrentIndex(0)
panel.preview()
assert not panel.error.text(),panel.error.text()
panel.apply()
assert not panel.error.text(),panel.error.text()
panel.reject()
assert abs(DXFCAD.parse_entity(curve.DXFData).snap_points()[0].position.x-4)<1e-7
assert cut.DXFData==originals[cut.Name] and far.DXFData==originals[far.Name]
with tempfile.TemporaryDirectory(dir=profile) as temp:
    folder=Path(temp)
    expected={o.Name:o.DXFData for o in (curve,poly)}
    export_dxf(folder/"curves.dxf",doc)
    parsed=DXFCAD.DXFDocument.read(str(folder/"curves.dxf"))
    assert len(parsed.entities)==4
    trimmed_poly=next(e for e in parsed.entities if e.type=="LWPOLYLINE")
    assert abs(DXFCAD.read_lwpolyline(trimmed_poly.raw).vertices[0].start_width-4)<1e-7
    doc.saveAs(str(folder/"curves.FCStd"))
    FreeCAD.closeDocument(doc.Name)
    doc=FreeCAD.openDocument(str(folder/"curves.FCStd"))
    for name,raw in expected.items():
        assert doc.getObject(name).DXFData==raw and not doc.getObject(name).Shape.isNull()
    FreeCAD.closeDocument(doc.Name)
print("LINE/ARC/curved-polyline trim/extend preview, undo/redo, native attributes, export and reopen passed.")

# Boundary drawing dependencies remain part of the cached preview plan.
doc=FreeCAD.newDocument("StaleTrimPreview")
line=create_line(doc,DXFCAD.Vector3D(),DXFCAD.Vector3D(10,0,0))
boundary_root=DXFCAD.create_drawing(doc,DXFCAD.DXFDocument.empty(4))
cut=create_line(doc,DXFCAD.Vector3D(5,-5,0),DXFCAD.Vector3D(5,5,0),boundary_root)
panel=show_drafting_tool(TrimTaskPanel,[line,cut])
panel.preview()
assert not panel.error.text(),panel.error.text()
preview_plan=panel.trim_plan
raw=line.DXFData
DXFCAD.ensure_layer(boundary_root,"ChangedBoundaryDrawing",2,True)
panel.apply()
assert "changed" in panel.error.text().lower(),panel.error.text()
assert panel.trim_plan is preview_plan and line.DXFData==raw
panel.reject()
FreeCAD.closeDocument(doc.Name)
