"""Two-click CIRCLE creation, native snapping, cancellation, persistence, and export."""
import FreeCAD
import FreeCADGui
from pathlib import Path
import tempfile
import DXFCAD
from pivy import coin
from dxfcad_circle_ui import show_circle_tool
from dxfcad_import import import_dxf
from dxfcad_export import export_dxf

assert FreeCAD.GuiUp
profile = Path(FreeCAD.getUserAppDataDir())
assert str(profile) in DXFCAD.__file__
FreeCADGui.activateWorkbench("DXFCADWorkbench")
assert "DXFCAD_DrawCircle" in FreeCADGui.listCommands()
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
    panel = show_circle_tool()
    FreeCADGui.updateGui()
    assert panel.form.grab().save(str(profile/"draw-circles-panel.png"))
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

    assert tool.callback is not None
    click(3,1)
    assert tool.start_point.x == 3 and tool.start_point.y == 1
    click(3,1)
    assert not created() and "positive" in panel.error.text()
    move(7,-5)
    assert tool.candidate is None and "plane" in panel.error.text()
    click(7,-5)
    assert not created()
    move(6,1)
    assert tool.candidate.x == 6 and tool.candidate.y == 1
    assert not created() and tool.overlay is not None
    assert float(panel.radius.text()) == 3
    click(6,1)
    assert len(created()) == 1 and tool.start_point is None
    assert created()[0].DXFType == "CIRCLE"
    assert abs(created()[0].Shape.Length-6*3.141592653589793) < 1e-7
    click(10,6)
    assert (tool.start_point.x,tool.start_point.y) == (10,6)
    move(6,1)
    escape()
    assert tool.start_point is None and tool.overlay is None and len(created()) == 1
    click(10,6)
    click(6,1,coin.SoMouseButtonEvent.BUTTON2)
    assert tool.start_point is None
    click(2,-4,ctrl=True)
    assert tool.start_point is None
    for field,value in zip([*panel.start_fields,panel.radius,panel.elevation],("0","0","-2","2")):
        field.setText(value)
    panel.create_numeric()
    assert len(created()) == 1 and "positive" in panel.error.text()
    panel.radius.setText("nan")
    panel.create_numeric()
    assert len(created()) == 1 and "finite" in panel.error.text()
    panel.radius.setText("2.5")
    panel.create_numeric()
    assert len(created()) == 2 and not panel.error.text(), panel.error.text()
    obj = created()[-1]
    assert abs(obj.Shape.Length-5*3.141592653589793) < 1e-7
    assert abs(obj.Shape.BoundBox.ZMin-2) < 1e-6
    raw, name = obj.DXFData, obj.Name
    panel.reject()
    assert tool.callback is None and tool.overlay is None
    doc.undo()
    doc.recompute()
    assert doc.getObject(name) is None
    doc.redo()
    doc.recompute()
    assert doc.getObject(name).DXFData == raw
    assert original == {name:doc.getObject(name).DXFData for name in original}
    out = folder/"circles-out.dxf"
    export_dxf(out,doc)
    restored = DXFCAD.DXFDocument.read(str(out))
    assert len(restored.entities) == 5
    handles = [e.handle for e in restored.entities]
    assert len(set(handles)) == len(handles)
    assert "Retain opaque data" in out.read_text()
    # A clean independent-reader fixture excludes the deliberately opaque SPLINE.
    clean = folder/"circle-only.dxf"
    DXFCAD.DXFDocument.empty(4).write(str(clean),added=[raw])
    import Import
    independent = FreeCAD.newDocument("IndependentCircle")
    Import.readDXF(str(clean),independent.Name,False)
    independent.recompute()
    shapes = [o.Shape for o in independent.Objects if hasattr(o,"Shape") and not o.Shape.isNull()]
    assert shapes and any(abs(s.Length-5*3.141592653589793) < 1e-6 for s in shapes)
    FreeCAD.closeDocument(independent.Name)
    FreeCAD.setActiveDocument(doc.Name)
    saved = folder/"circles.FCStd"
    doc.saveAs(str(saved))
    FreeCAD.closeDocument(doc.Name)
    reopened = FreeCAD.openDocument(str(saved))
    assert reopened.getObject(name).DXFData == raw
    assert abs(reopened.getObject(name).Shape.Length-5*3.141592653589793) < 1e-7
    # Closing the document with a pending preview detaches all callbacks.
    panel = show_circle_tool()
    tool = panel.viewport_tool
    tool.start_point = DXFCAD.Vector3D(0,0,2)
    tool.render(DXFCAD.Vector3D(2,0,2))
    FreeCAD.closeDocument(reopened.Name)
    FreeCADGui.updateGui()
    assert tool.callback is None and tool.overlay is None
    panel.reject()
    # Externally destroying the task form is also safe.
    panel = show_circle_tool()
    tool = panel.viewport_tool
    FreeCADGui.Control.closeDialog()
    FreeCADGui.updateGui()
    assert tool.callback is None
    for doc_name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(doc_name)
    panel = show_circle_tool()
    doc_name = panel.document.Name
    panel.reject()
    assert doc_name not in FreeCAD.listDocuments()
print("Circle picking, snapping, preview, cancellation, numeric radius, undo, persistence and export passed.")
