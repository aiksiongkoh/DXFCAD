"""Two-click CIRCLE creation, native snapping, cancellation, persistence, and export."""
import FreeCAD
import FreeCADGui
from pathlib import Path
import tempfile
import DXFCAD
from pivy import coin
from dxfcad_rectangle_ui import show_rectangle_tool
from dxfcad_import import import_dxf
from dxfcad_export import export_dxf

assert FreeCAD.GuiUp
profile = Path(FreeCAD.getUserAppDataDir())
assert str(profile) in DXFCAD.__file__
FreeCADGui.activateWorkbench("DXFCADWorkbench")
assert "DXFCAD_Rectangle" in FreeCADGui.listCommands()
drawing = ("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n"
           "0\nSECTION\n2\nENTITIES\n"
           "0\nLINE\n5\nA1\n10\n3\n20\n1\n11\n6\n21\n1\n"
           "0\nCIRCLE\n5\nA2\n10\n10\n20\n6\n40\n1\n"
           "0\nLINE\n5\nA3\n10\n7\n20\n-5\n30\n3\n11\n9\n21\n-5\n31\n3\n"
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
    panel = show_rectangle_tool()
    FreeCADGui.updateGui()
    assert panel.form.grab().save(str(profile/"draw-rectangles-panel.png"))
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
    def created():
        return [o for o in doc.Objects if getattr(o,"DXFSourceIndex",None) == -1]

    before = {o.Name:o.DXFData for o in doc.Objects if "DXFData" in o.PropertiesList}
    click(3,1)
    move(6,1)
    assert tool.candidate is None and "nonzero" in panel.error.text()
    move(7,-5)
    assert tool.candidate is None and "plane" in panel.error.text()
    move(10,6)
    assert tool.overlay is not None and not created()
    escape()
    assert tool.overlay is None and not created()
    click(3,1)
    click(10,6)
    assert len(created()) == 1
    obj = created()[0]
    assert DXFCAD.read_lwpolyline(obj.DXFData).closed
    assert len(DXFCAD.read_lwpolyline(obj.DXFData).vertices) == 4
    assert abs(obj.Shape.Length-24)<1e-7
    for field,value in zip([*panel.start_fields,*panel.end_fields,panel.elevation],("0","0","3","4","2")):
        field.setText(value)
    panel.create_numeric()
    assert not panel.error.text(),panel.error.text()
    assert len(created()) == 2
    last = created()[-1]
    assert abs(last.Shape.Length-14)<1e-7 and abs(last.Shape.BoundBox.ZMin-2)<1e-7
    name,raw = last.Name,last.DXFData
    panel.reject()
    doc.undo()
    assert doc.getObject(name) is None
    doc.redo()
    assert doc.getObject(name).DXFData == raw
    assert before == {n:doc.getObject(n).DXFData for n in before}
    out = folder/"rectangles.dxf"
    export_dxf(out,doc)
    assert len(DXFCAD.DXFDocument.read(str(out)).entities) == 5
    assert "Retain opaque data" in out.read_text()
    saved = folder/"rectangles.FCStd"
    doc.saveAs(str(saved))
    FreeCAD.closeDocument(doc.Name)
    source.unlink()
    reopened = FreeCAD.openDocument(str(saved))
    assert reopened.getObject(name).DXFData == raw
    assert abs(reopened.getObject(name).Shape.Length-14)<1e-7
    FreeCAD.closeDocument(reopened.Name)
print("Rectangle snapping, preview, validation, undo/redo, persistence and export passed.")
