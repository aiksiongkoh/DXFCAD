"""Native layer/property editing, display updates, undo, save and export."""
import FreeCAD
import FreeCADGui
from pathlib import Path
import tempfile
import base64
import DXFCAD
from dxfcad_edit import create_line, create_circle, drawing_root, copy_entities
from dxfcad_drafting_ui import show_drafting_tool, PropertiesTaskPanel
from dxfcad_export import export_dxf
FreeCADGui.activateWorkbench("DXFCADWorkbench")
assert "DXFCAD_Properties" in FreeCADGui.listCommands()
profile=Path(FreeCAD.getUserAppDataDir())
doc=FreeCAD.newDocument("Properties")
line=create_line(doc,DXFCAD.Vector3D(0,0,0),DXFCAD.Vector3D(10,0,0))
circle=create_circle(doc,DXFCAD.Vector3D(5,5,0),2)
root=drawing_root(line)
old_source=root.DXFSourceData
old_raw=[line.DXFData,circle.DXFData]
old_color=tuple(line.ViewObject.LineColor)
old_group=next(g for g in root.Group if line in g.Group)
def source():
    return DXFCAD.DXFDocument.parse(base64.b64decode(root.DXFSourceData,validate=True))
def rgb(obj,color):
    return all(abs(a-b)<1e-6 for a,b in zip(obj.ViewObject.LineColor,color))
def undo():
    FreeCADGui.runCommand("Std_Undo")
    doc.recompute()
    FreeCADGui.updateGui()
def redo():
    FreeCADGui.runCommand("Std_Redo")
    doc.recompute()
    FreeCADGui.updateGui()
panel=show_drafting_tool(PropertiesTaskPanel,[line,circle])
panel.layer.setCurrentText("Walls")
panel.layer_color.setValue(1)
panel.preview()
assert not panel.error.text(),panel.error.text()
assert root.DXFSourceData==old_source and [line.DXFData,circle.DXFData]==old_raw
FreeCADGui.updateGui()
assert panel.form.grab().save(str(profile/"properties-panel.png"))
panel.apply()
assert not panel.error.text(),panel.error.text()
assert source().layers["Walls"].color==1
assert line.DXFLayer=="Walls" and circle.DXFLayer=="Walls"
assert rgb(line,(1,0,0)) and rgb(circle,(1,0,0))
walls=next(g for g in root.Group if getattr(g,"DXFLayerName",None)=="Walls")
assert line in walls.Group and circle in walls.Group and not old_group.Group
new_raw=[line.DXFData,circle.DXFData]
new_source=root.DXFSourceData
panel.reject()
undo()
assert root.DXFSourceData==old_source and [line.DXFData,circle.DXFData]==old_raw
assert line in old_group.Group and circle in old_group.Group
assert rgb(line,old_color)
redo()
assert root.DXFSourceData==new_source and [line.DXFData,circle.DXFData]==new_raw
assert rgb(line,(1,0,0))
panel=show_drafting_tool(PropertiesTaskPanel,[line])
panel.edit_layer.setChecked(True)
panel.layer_color.setValue(3)
panel.layer_on.setChecked(False)
panel.apply()
assert not panel.error.text(),panel.error.text()
assert not source().layers["Walls"].visible
assert not line.ViewObject.isVisible() and not circle.ViewObject.isVisible()
assert rgb(circle,(0,1,0))
panel.reject()
undo()
assert source().layers["Walls"].visible and source().layers["Walls"].color==1
assert line.ViewObject.isVisible() and circle.ViewObject.isVisible() and rgb(circle,(1,0,0))
redo()
assert not source().layers["Walls"].visible
panel=show_drafting_tool(PropertiesTaskPanel,[line])
panel.edit_layer.setChecked(True)
panel.layer_on.setChecked(True)
panel.layer_color.setValue(3)
panel.set_color.setChecked(True)
panel.color.setValue(2)
panel.visible.setChecked(False)
panel.apply()
assert not panel.error.text(),panel.error.text()
assert DXFCAD.property_info(line.DXFData)==("Walls",2,False)
assert not line.ViewObject.isVisible() and circle.ViewObject.isVisible()
assert rgb(line,(1,1,0)) and rgb(circle,(0,1,0))
panel.reject()
expected=[line.DXFData,circle.DXFData]
undo()
assert not source().layers["Walls"].visible
redo()
assert expected==[line.DXFData,circle.DXFData]
assert not line.ViewObject.isVisible() and circle.ViewObject.isVisible()
panel=show_drafting_tool(PropertiesTaskPanel,[circle])
current_source=root.DXFSourceData
panel.layer.setCurrentText("Bad|Name")
panel.apply()
assert "name" in panel.error.text().lower() and root.DXFSourceData==current_source
assert expected==[line.DXFData,circle.DXFData]
panel.reject()
with tempfile.TemporaryDirectory(dir=profile) as temp:
    folder=Path(temp)
    out=folder/"properties.dxf"
    export_dxf(out,doc)
    parsed=DXFCAD.DXFDocument.read(str(out))
    assert len(parsed.entities)==2 and parsed.layers["Walls"].color==3 and parsed.layers["Walls"].visible
    assert all(e.layer=="Walls" for e in parsed.entities)
    assert not parsed.entities[0].visible and parsed.entities[1].visible
    import Import
    independent=FreeCAD.newDocument("IndependentProperties")
    Import.readDXF(str(out),independent.Name,False)
    independent.recompute()
    assert any(hasattr(o,"Shape") and not o.Shape.isNull() for o in independent.Objects)
    FreeCAD.closeDocument(independent.Name)
    FreeCAD.setActiveDocument(doc.Name)
    clones=copy_entities([circle],DXFCAD.Vector3D(2,0,0))
    assert clones[0].DXFLayer=="Walls" and rgb(clones[0],(0,1,0))
    saved=folder/"properties.FCStd"
    names=[line.Name,circle.Name]
    doc.saveAs(str(saved))
    FreeCAD.closeDocument(doc.Name)
    doc=FreeCAD.openDocument(str(saved))
    line,circle=[doc.getObject(n) for n in names]
    root=drawing_root(line)
    assert expected==[line.DXFData,circle.DXFData]
    assert rgb(circle,(0,1,0)) and not line.ViewObject.isVisible() and circle.ViewObject.isVisible()
    assert source().layers["Walls"].color==3
    FreeCAD.closeDocument(doc.Name)
print("Layer creation/assignment, colors, visibility, source snapshots, undo, copy, save and export passed.")
