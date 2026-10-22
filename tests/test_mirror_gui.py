"""Native selection rotation, native snapping, cancellation, persistence, and export."""
import FreeCAD
import FreeCADGui
from pathlib import Path
import tempfile
import DXFCAD
from pivy import coin
from dxfcad_transform_ui import show_transform_tool, MirrorTaskPanel
from dxfcad_import import import_dxf
from dxfcad_export import export_dxf

assert FreeCAD.GuiUp
profile = Path(FreeCAD.getUserAppDataDir())
assert str(profile) in DXFCAD.__file__
FreeCADGui.activateWorkbench("DXFCADWorkbench")
assert "DXFCAD_Mirror" in FreeCADGui.listCommands()
drawing = ("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n"
           "0\nSECTION\n2\nENTITIES\n"
           "0\nLINE\n5\nA1\n100\nAcDbEntity\n8\n0\n100\nAcDbLine\n10\n3\n20\n1\n11\n6\n21\n1\n"
           "0\nCIRCLE\n5\nA2\n100\nAcDbEntity\n8\n0\n100\nAcDbCircle\n10\n10\n20\n6\n40\n1\n"
           "0\nLINE\n5\nA3\n10\n7\n20\n-5\n30\n3\n11\n9\n21\n-5\n31\n3\n"
           "0\nARC\n5\nA4\n10\n-7\n20\n3\n40\n2\n50\n0\n51\n90\n"
           "0\nLWPOLYLINE\n5\nA5\n90\n2\n70\n0\n10\n-7\n20\n-4\n42\n0.2\n10\n-3\n20\n-4\n"
           "0\nTEXT\n5\nA6\n10\n-6\n20\n-7\n40\n1\n1\nMove text\n"
           "0\nMTEXT\n5\nA7\n10\n-6\n20\n-10\n40\n1\n41\n8\n1\nMove multiline text\n"
           "0\nSPLINE\n5\nF0\n999\nRetain opaque data\n0\nENDSEC\n0\nEOF\n")

with tempfile.TemporaryDirectory(dir=profile) as temp:
    folder = Path(temp)
    source = folder/"lines.dxf"
    source.write_text(drawing)
    doc = import_dxf(source)
    original = {o.Name:o.DXFData for o in doc.Objects if "DXFData" in o.PropertiesList}
    view = FreeCADGui.activeDocument().activeView()
    view.setCameraType("Orthographic")
    camera = view.getCameraNode()
    camera.orientation.setValue(coin.SbRotation(0,0,0,1))
    camera.position.setValue(5,5,50)
    camera.focalDistance.setValue(50)
    camera.height.setValue(40)
    selected = [o for o in doc.Objects if "DXFData" in o.PropertiesList and DXFCAD.parse_entity(o.DXFData).handle != "A3"]
    assert len(selected) == 6
    selected_names = [o.Name for o in selected]
    panel = show_transform_tool(selected,panel_type=MirrorTaskPanel)
    FreeCADGui.updateGui()
    assert panel.form.grab().save(str(profile/"mirror-objects-panel.png"))
    tool = panel.viewport_tool
    manager = view.getViewer().getSoEventManager()
    def screen(x,y,z=0):
        return view.getPointOnScreen(FreeCAD.Vector(x,y,z))
    def button(x,y,state,which=coin.SoMouseButtonEvent.BUTTON1,ctrl=False):
        event = coin.SoMouseButtonEvent()
        event.setButton(which)
        event.setState(state)
        event.setCtrlDown(ctrl)
        event.setPosition(coin.SbVec2s(*map(int,screen(x,y))))
        manager.processEvent(event)
    def click(x,y,which=coin.SoMouseButtonEvent.BUTTON1,ctrl=False):
        button(x,y,coin.SoButtonEvent.DOWN,which,ctrl)
        button(x,y,coin.SoButtonEvent.UP,which,ctrl)
    def move(x,y):
        event = coin.SoLocation2Event()
        event.setPosition(coin.SbVec2s(*map(int,screen(x,y))))
        manager.processEvent(event)
    def escape():
        event = coin.SoKeyboardEvent()
        event.setKey(coin.SoKeyboardEvent.ESCAPE)
        event.setState(coin.SoButtonEvent.DOWN)
        manager.processEvent(event)
    before = {o.Name:o.DXFData for o in selected}
    click(3,1)
    move(10,6)
    assert tool.overlay is not None
    angle = DXFCAD.rotation_angle(DXFCAD.Vector3D(3,1,0),DXFCAD.Vector3D(10,6,0))
    assert abs(float(panel.parameter.text())-angle)<1e-8
    assert before == {o.Name:o.DXFData for o in selected}
    escape()
    assert tool.overlay is None
    click(3,1)
    click(10,6)
    after_pick = {o.Name:o.DXFData for o in selected}
    assert after_pick != before
    for obj in selected:
        assert obj.DXFData == DXFCAD.parse_entity(before[obj.Name]).mirrored(angle,DXFCAD.Vector3D(3,1,0))
        assert not obj.DisplayStatus.startswith("Error:"),obj.DisplayStatus
    doc.undo()
    assert before == {o.Name:o.DXFData for o in selected}
    doc.redo()
    assert after_pick == {o.Name:o.DXFData for o in selected}
    for field in panel.pivot_fields:
        field.setText("0")
    panel.parameter.setText("90")
    panel.create_numeric()
    assert not panel.error.text(),panel.error.text()
    after_numeric = {o.Name:o.DXFData for o in selected}
    for obj in selected:
        old = DXFCAD.parse_entity(after_pick[obj.Name]).snap_points()[0].position
        new = DXFCAD.parse_entity(obj.DXFData).snap_points()[0].position
        assert abs(new.x+old.x)<1e-8 and abs(new.y-old.y)<1e-8
        assert obj.Placement.isIdentity()
    panel.parameter.setText("nan")
    panel.create_numeric()
    assert "finite" in panel.error.text()
    assert after_numeric == {o.Name:o.DXFData for o in selected}
    panel.reject()
    out = folder/"rotated.dxf"
    export_dxf(out,doc)
    assert "Retain opaque data" in out.read_text()
    exported = {e.handle:e.raw for e in DXFCAD.DXFDocument.read(str(out)).entities}
    for obj in selected:
        assert exported[DXFCAD.parse_entity(obj.DXFData).handle] == obj.DXFData
    saved = folder/"rotated.FCStd"
    doc.saveAs(str(saved))
    FreeCAD.closeDocument(doc.Name)
    source.unlink()
    doc = FreeCAD.openDocument(str(saved))
    for name,raw in after_numeric.items():
        assert doc.getObject(name).DXFData == raw
    FreeCAD.closeDocument(doc.Name)
print("Rotate six entity types: preview, snapped angle, numeric rotation, undo/redo, persistence and export passed.")
