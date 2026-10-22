"""Native TEXT/MTEXT creation/editing, snapped origins, undo and export."""
import FreeCAD
import FreeCADGui
from pathlib import Path
import tempfile
import DXFCAD
from pivy import coin
from dxfcad_edit import create_line
from dxfcad_text_ui import show_text_tool
from dxfcad_export import export_dxf
FreeCADGui.activateWorkbench("DXFCADWorkbench")
assert "DXFCAD_CreateText" in FreeCADGui.listCommands() and "DXFCAD_EditText" in FreeCADGui.listCommands()
profile=Path(FreeCAD.getUserAppDataDir())
doc=FreeCAD.newDocument("TextEditing")
line=create_line(doc,DXFCAD.Vector3D(3,1,2),DXFCAD.Vector3D(6,1,2))
view=FreeCADGui.activeDocument().activeView()
view.setCameraType("Orthographic")
camera=view.getCameraNode()
camera.orientation.setValue(coin.SbRotation(0,0,0,1))
camera.position.setValue(5,5,50)
camera.focalDistance.setValue(50)
camera.height.setValue(40)
panel=show_text_tool()
panel.origin_fields[2].setText("2")
panel.toggle_pick(True)
manager=view.getViewer().getSoEventManager()
point=view.getPointOnScreen(FreeCAD.Vector(3,1,2))
for state in (coin.SoButtonEvent.DOWN,coin.SoButtonEvent.UP):
    event=coin.SoMouseButtonEvent()
    event.setButton(coin.SoMouseButtonEvent.BUTTON1)
    event.setState(state)
    event.setPosition(coin.SbVec2s(*map(int,point)))
    manager.processEvent(event)
assert [float(f.text()) for f in panel.origin_fields]==[3,1,2]
assert panel.viewport_tool.callback is None
panel.content.setPlainText("Label")
panel.height.setText("2")
panel.preview()
assert not panel.error.text(),panel.error.text()
assert panel.preview_tool.overlay is not None
assert len([o for o in doc.Objects if "DXFData" in o.PropertiesList])==1
FreeCADGui.updateGui()
assert panel.form.grab().save(str(profile/"text-panel.png"))
panel.apply()
assert not panel.error.text(),panel.error.text()
text=next(o for o in doc.Objects if getattr(o,"DXFType","")=="TEXT")
assert DXFCAD.text_info(text.DXFData)==("Label",2.0,0.0)
assert (DXFCAD.parse_entity(text.DXFData).snap_points()[0].position.z)==2
panel.multiline.setChecked(True)
panel.content.setPlainText("First line\nSecond {literal} \\P")
panel.width.setText("12")
panel.height.setText("1.5")
panel.apply()
assert not panel.error.text(),panel.error.text()
mtext=next(o for o in doc.Objects if getattr(o,"DXFType","")=="MTEXT")
assert DXFCAD.text_info(mtext.DXFData)[0]=="First line\nSecond {literal} \\P"
assert not mtext.Shape.isNull()
count=len(doc.Objects)
panel.height.setText("0")
panel.apply()
assert "positive" in panel.error.text() and len(doc.Objects)==count
panel.reject()
original=text.DXFData
panel=show_text_tool(text)
panel.content.setPlainText("Updated label")
panel.height.setText("3")
panel.preview()
assert text.DXFData==original and panel.preview_tool.overlay is not None
preview_plan=panel.text_plan
predicted=preview_plan.drawing_bytes[text.DXFSourceRoot.Name]
panel.apply()
assert not panel.error.text(),panel.error.text()
assert DXFCAD.current_drawing_bytes(text.DXFSourceRoot)==predicted
assert panel.text_plan is None
updated=text.DXFData
assert DXFCAD.text_info(updated)==("Updated label",3.0,0.0)
assert text.Placement.isIdentity()
panel.reject()
doc.undo()
assert text.DXFData==original
doc.redo()
assert text.DXFData==updated
with tempfile.TemporaryDirectory(dir=profile) as temp:
    folder=Path(temp)
    out=folder/"text.dxf"
    export_dxf(out,doc)
    parsed=DXFCAD.DXFDocument.read(str(out))
    assert len(parsed.entities)==3
    contents=[DXFCAD.text_info(e.raw)[0] for e in parsed.entities if e.type in ("TEXT","MTEXT")]
    assert "Updated label" in contents and "First line\nSecond {literal} \\P" in contents
    saved=folder/"text.FCStd"
    names=[text.Name,mtext.Name]
    raw=[text.DXFData,mtext.DXFData]
    doc.saveAs(str(saved))
    FreeCAD.closeDocument(doc.Name)
    doc=FreeCAD.openDocument(str(saved))
    assert raw==[doc.getObject(n).DXFData for n in names]
    panel=show_text_tool(doc.getObject(names[1]))
    panel.preview()
    assert panel.preview_tool.overlay is not None
    FreeCAD.closeDocument(doc.Name)
    assert panel.preview_tool.overlay is None
    panel.reject()
panel=show_text_tool()
name=panel.document.Name
panel.reject()
assert name not in FreeCAD.listDocuments()
print("TEXT/MTEXT creation, origin snapping, editing, preview, undo, FCStd and native export passed.")

# Successful edits refresh the C++ session; preview plans retain drawing dependencies.
from dxfcad_edit import create_text
doc=FreeCAD.newDocument("TextSession")
text=create_text(doc,"Initial",2,DXFCAD.Vector3D())
panel=show_text_tool(text)
for content in ("First edit","Second edit"):
    panel.content.setPlainText(content)
    panel.apply()
    assert not panel.error.text(),panel.error.text()
    assert DXFCAD.text_info(text.DXFData)[0]==content
    assert panel.edit_session.original_data==text.DXFData and panel.text_plan is None
panel.content.setPlainText("Stale preview")
panel.preview()
assert not panel.error.text(),panel.error.text()
plan=panel.text_plan
raw=text.DXFData
DXFCAD.ensure_layer(text.DXFSourceRoot,"ChangedDrawing",3,True)
panel.apply()
assert "changed" in panel.error.text().lower(),panel.error.text()
assert panel.text_plan is plan and text.DXFData==raw
panel.reject()
FreeCAD.closeDocument(doc.Name)
