"""Contour cleanup commands, mouse picking, previews, undo/redo and export."""
import FreeCAD
import FreeCADGui
import DXFCAD
from pathlib import Path
import tempfile
from PySide import QtGui
from pivy import coin
from dxfcad_import import import_dxf
from dxfcad_export import export_dxf
from dxfcad_contour_ui import show_contour_tool
from dxfcad_edit import create_line, create_polyline

profile=Path(FreeCAD.getUserAppDataDir())
assert str(profile) in DXFCAD.__file__
FreeCADGui.activateWorkbench("DXFCADWorkbench")
assert all("DXFCAD_"+name in FreeCADGui.listCommands() for name in ("Join","Split","Break","Close","TrimInterior"))
line="0\nLINE\n5\nAB\n10\n0\n20\n0\n11\n10\n21\n0\n"
left="0\nLINE\n5\nA2\n10\n3\n20\n-4\n11\n3\n21\n4\n"
right="0\nLINE\n5\nA3\n10\n7\n20\n-4\n11\n7\n21\n4\n"
circle="0\nCIRCLE\n5\nBC\n10\n20\n20\n0\n40\n4\n"
raw="0\nSECTION\n2\nENTITIES\n"+line+left+right+circle+"0\nENDSEC\n0\nEOF\n"
with tempfile.TemporaryDirectory(dir=profile) as temp:
    folder=Path(temp)
    source=folder/"cleanup.dxf"
    source.write_text(raw)
    doc=import_dxf(source)
    doc.UndoMode=1
    objects={o.DXFHandle:o for o in doc.Objects if "DXFData" in o.PropertiesList}
    original_names={o.Name for o in doc.Objects}
    target_name=objects["AB"].Name
    view=FreeCADGui.activeDocument().activeView()
    view.setCameraType("Orthographic")
    camera=view.getCameraNode()
    camera.orientation.setValue(coin.SbRotation(0,0,0,1))
    camera.position.setValue(10,0,50)
    camera.focalDistance.setValue(50)
    camera.height.setValue(25)
    manager=view.getViewer().getSoEventManager()
    def click(x,y):
        for state in (coin.SoButtonEvent.DOWN,coin.SoButtonEvent.UP):
            event=coin.SoMouseButtonEvent()
            event.setButton(coin.SoMouseButtonEvent.BUTTON1)
            event.setState(state)
            event.setPosition(coin.SbVec2s(*map(int,view.getPointOnScreen(FreeCAD.Vector(x,y,0)))))
            manager.processEvent(event)
    panel=show_contour_tool("trim",[objects["AB"],objects["A2"],objects["A3"]])
    panel.toggle_pick(True)
    click(5,0)
    assert panel.viewport_tool.overlay is not None,panel.error.text()
    assert doc.getObject(target_name).DXFData==line
    assert {o.Name for o in doc.Objects}==original_names
    FreeCADGui.updateGui()
    assert panel.viewport_tool.overlay is not None,"Preview was closed during GUI update"
    assert panel.form.grab().save(str(profile/"contour-trim-panel.png"))
    # The default saveImage backend captures document view providers and omits
    # temporary scene overlays. Render the full viewer for preview verification.
    preferences=FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    previous=preferences.GetString("SavePicture","")
    preferences.SetString("SavePicture","FramebufferObject")
    view.saveImage(str(profile/"contour-trim-preview.png"),900,600,"Current")
    preferences.SetString("SavePicture",previous)
    preview=QtGui.QImage(str(profile/"contour-trim-preview.png"))
    orange=0
    for y in range(preview.height()):
        for x in range(preview.width()):
            color=preview.pixelColor(x,y)
            if color.red()>200 and 60<color.green()<180 and color.blue()<90:
                orange+=1
    assert orange>20,"Retained contour pieces must be visibly orange in the preview"
    panel.reject()
    assert panel.picker.callback is None and panel.viewport_tool.overlay is None
    assert doc.getObject(target_name).DXFData==line
    panel=show_contour_tool("trim",[doc.getObject(target_name),objects["A2"],objects["A3"]])
    panel.first.setText("3")
    panel.apply()
    assert "intersection" in panel.error.text() and doc.getObject(target_name).DXFData==line
    panel.first.setText("5")
    panel.apply()
    assert not FreeCADGui.Control.activeDialog(),panel.error.text()
    assert len(panel.result)==2 and all(abs(o.Shape.Length-3)<1e-7 for o in panel.result)
    assert panel.picker.callback is None and not FreeCADGui.Control.activeDialog()
    doc.undo(); doc.recompute()
    assert {o.Name for o in doc.Objects}==original_names
    assert doc.getObject(target_name).DXFData==line
    doc.redo(); doc.recompute()
    assert abs(doc.getObject(target_name).Shape.Length-3)<1e-7
    doc.undo(); doc.recompute()
    # Split by a real viewport click and preserve the source object's handle.
    panel=show_contour_tool("split",[doc.getObject(target_name)])
    panel.toggle_pick(True)
    click(4,0)
    assert panel.viewport_tool.overlay is not None,panel.error.text()
    assert doc.getObject(target_name).DXFData==line
    panel.apply()
    assert not FreeCADGui.Control.activeDialog(),panel.error.text()
    assert len(panel.result)==2 and panel.result[0].DXFHandle=="AB"
    doc.undo(); doc.recompute()
    # Break a circle into an arc, replacing the native object type.
    panel=show_contour_tool("break",[objects["BC"]])
    panel.first.setText("2"); panel.second.setText("5")
    panel.preview()
    assert panel.viewport_tool.overlay is not None,panel.error.text()
    panel.apply()
    assert not FreeCADGui.Control.activeDialog(),panel.error.text()
    assert panel.result[0].DXFType=="ARC"
    assert panel.result[0].Shape.isValid()
    # Join an unordered chain and close it with one new straight segment.
    a=create_line(doc,DXFCAD.Vector3D(0,8,0),DXFCAD.Vector3D(5,8,0))
    b=create_line(doc,DXFCAD.Vector3D(5,13,0),DXFCAD.Vector3D(5,8,0))
    panel=show_contour_tool("join",[b,a])
    panel.preview()
    assert panel.viewport_tool.overlay is not None,panel.error.text()
    panel.apply()
    assert not FreeCADGui.Control.activeDialog(),panel.error.text()
    joined=panel.result[0]
    assert joined.DXFType=="LWPOLYLINE" and abs(joined.Shape.Length-10)<1e-7
    panel=show_contour_tool("close",[joined])
    panel.preview(); panel.apply()
    assert not FreeCADGui.Control.activeDialog(),panel.error.text()
    assert DXFCAD.read_lwpolyline(joined.DXFData).closed
    # A closed polyline can be opened at one picked seam and retains its length.
    before=joined.Shape.Length
    panel=show_contour_tool("split",[joined])
    panel.first.setText("2")
    panel.preview(); panel.apply()
    assert not FreeCADGui.Control.activeDialog(),panel.error.text()
    assert len(panel.result)==1 and not DXFCAD.read_lwpolyline(panel.result[0].DXFData).closed
    assert abs(panel.result[0].Shape.Length-before)<1e-7
    output=folder/"cleanup-out.dxf"
    export_dxf(output,doc)
    parsed=DXFCAD.DXFDocument.read(str(output))
    assert not parsed.warnings
    assert len({e.handle for e in parsed.entities})==len(parsed.entities)
    assert all(o.Shape.isValid() for o in doc.Objects if "DXFData" in o.PropertiesList)
    FreeCADGui.Selection.clearSelection()
    FreeCAD.closeDocument(doc.Name)
assert not FreeCADGui.Control.activeDialog()
print("Contour commands, mouse picking, preview/cancel, topology changes, undo/redo and export passed.")

# Interior-trim preview retains dependencies in a separate boundary drawing.
doc=FreeCAD.newDocument("ContourDependencies")
target=create_line(doc,DXFCAD.Vector3D(),DXFCAD.Vector3D(10,0,0))
boundary_root=DXFCAD.create_drawing(doc,DXFCAD.DXFDocument.empty(4))
a=create_line(doc,DXFCAD.Vector3D(3,-5,0),DXFCAD.Vector3D(3,5,0),boundary_root)
b=create_line(doc,DXFCAD.Vector3D(7,-5,0),DXFCAD.Vector3D(7,5,0),boundary_root)
panel=show_contour_tool("trim",[a,target,b])
panel.target.setCurrentIndex(1)
panel.first.setText("5")
panel.preview()
assert not panel.error.text(),panel.error.text()
plan=panel.edit_plan
assert isinstance(plan,DXFCAD.ContourOperationPlan) and plan.template_object==target
raw=target.DXFData
DXFCAD.ensure_layer(boundary_root,"ChangedBoundaryDrawing",3,True)
names={o.Name for o in doc.Objects}
panel.apply()
assert "changed" in panel.error.text().lower(),panel.error.text()
assert panel.edit_plan is plan and target.DXFData==raw
assert {o.Name for o in doc.Objects}==names
panel.reject()
FreeCAD.closeDocument(doc.Name)
