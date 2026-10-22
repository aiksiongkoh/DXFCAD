"""Native curved offsets: preview, copy/in-place, atomic failures and persistence."""
import FreeCAD
import FreeCADGui
import DXFCAD
from pathlib import Path
import tempfile
from dxfcad_edit import _create_entity
from dxfcad_drafting_ui import OffsetTaskPanel, show_drafting_tool
from dxfcad_export import export_dxf

FreeCADGui.activateWorkbench("DXFCADWorkbench")
doc=FreeCAD.newDocument("CurvedOffsets")
rich=("0\nLWPOLYLINE\n100\nAcDbEntity\n8\n0\n100\nAcDbPolyline\n90\n3\n70\n128\n38\n3\n43\n0.2\n39\n0.1\n"
      "210\n1\n220\n2\n230\n3\n10\n-5\n20\n0\n42\n1\n40\n0.2\n41\n0.4\n91\n101\n"
      "10\n5\n20\n0\n40\n0.6\n41\n0.8\n91\n102\n10\n10\n20\n0\n91\n103\n1001\nTest\n1000\nkeep curved data\n")
curve=_create_entity(doc,rich,None,"LWPolyline")
ring_raw=DXFCAD.create_lwpolyline(DXFCAD.LWPolyline([DXFCAD.LWPolylineVertex(15,0,1),DXFCAD.LWPolylineVertex(25,0,1)],True,0.3,3))
ring=_create_entity(doc,ring_raw,None,"LWPolyline")
elbow=DXFCAD.create_lwpolyline(DXFCAD.LWPolyline([DXFCAD.LWPolylineVertex(30,0),DXFCAD.LWPolylineVertex(40,0),DXFCAD.LWPolylineVertex(40,10)]))
fillet=_create_entity(doc,DXFCAD.fillet_corner(elbow,1,4),None,"LWPolyline")
objects=[curve,ring,fillet]
doc.recompute()
view=FreeCADGui.activeDocument().activeView()
view.viewTop()
view.fitAll()
before={o.Name:o.DXFData for o in objects}
panel=show_drafting_tool(OffsetTaskPanel,objects)
panel.distance.setText("2.8")
panel.preview()
assert not panel.error.text(),panel.error.text()
assert panel.viewport_tool.overlay is not None
assert before=={o.Name:o.DXFData for o in objects}
panel.reject()
assert panel.viewport_tool.overlay is None and panel.viewport_tool.scene is None
assert before=={o.Name:o.DXFData for o in objects}

panel=show_drafting_tool(OffsetTaskPanel,objects)
panel.distance.setText("2.8")
panel.preview()
assert not panel.error.text(),panel.error.text()
panel.apply()
assert not panel.error.text(),panel.error.text()
copies=[o for o in doc.Objects if "DXFData" in o.PropertiesList and o.Name not in before]
assert len(copies)==3 and before=={o.Name:o.DXFData for o in objects}
copy_names=[o.Name for o in copies]
models=[DXFCAD.read_lwpolyline(o.DXFData) for o in copies]
assert len(models[0].vertices)==4 and models[0].vertices[1].bulge<0
assert models[0].vertices[1].has_widths
assert abs(models[0].vertices[1].start_width-0.4)<1e-7 and abs(models[0].vertices[1].end_width-0.6)<1e-7
assert models[0].extrusion.z==3 and models[0].elevation==3
assert models[1].closed and len(models[1].vertices)==2
assert abs(models[1].segments()[0].arc.radius-2.2)<1e-7
assert abs(models[2].segments()[1].arc.radius-1.2)<1e-7
assert all(not o.Shape.isNull() and o.Shape.isValid() for o in copies)
assert "keep curved data" in copies[0].DXFData and "91\n102\n" in copies[0].DXFData
copy_data={o.Name:o.DXFData for o in copies}
doc.undo()
assert all(doc.getObject(name) is None for name in copy_names)
doc.redo()
assert copy_data=={name:doc.getObject(name).DXFData for name in copy_names}
panel.keep.setChecked(False)
panel.apply()
assert not panel.error.text(),panel.error.text()
after={o.Name:o.DXFData for o in objects}
assert before!=after
count=len(doc.Objects)
panel.distance.setText("3")
panel.apply()
assert "positive" in panel.error.text(),panel.error.text()
assert after=={o.Name:o.DXFData for o in objects} and count==len(doc.Objects)
assert copy_data=={name:doc.getObject(name).DXFData for name in copy_names}
panel.reject()
doc.undo()
assert before=={o.Name:o.DXFData for o in objects}
doc.redo()
assert after=={o.Name:o.DXFData for o in objects}
with tempfile.TemporaryDirectory(dir=FreeCAD.getUserAppDataDir()) as temp:
    folder=Path(temp)
    output=folder/"curved-offset.dxf"
    export_dxf(output,doc)
    parsed=DXFCAD.DXFDocument.read(str(output))
    assert len(parsed.entities)==6 and len({e.handle for e in parsed.entities})==6
    exported=DXFCAD.read_lwpolyline(parsed.entities[0].raw)
    assert len(exported.vertices)==4 and abs(exported.vertices[1].start_width-0.4)<1e-7
    assert exported.extrusion.y==2 and exported.elevation==3
    saved=folder/"curved-offset.FCStd"
    doc.saveAs(str(saved))
    FreeCAD.closeDocument(doc.Name)
    doc=FreeCAD.openDocument(str(saved))
    for name,raw in {**after,**copy_data}.items():
        obj=doc.getObject(name)
        assert obj.DXFData==raw and not obj.Shape.isNull() and obj.Shape.isValid()
    FreeCAD.closeDocument(doc.Name)
print("Curved offsets: tilted/closed profiles, round joins, widths, preview, undo/redo, export and restore passed.")
