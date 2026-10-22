"""Two-click LINE chains, native snapping, cancellation, persistence, and export."""
import FreeCAD
import FreeCADGui
from pathlib import Path
import tempfile
import DXFCAD
from pivy import coin
from dxfcad_line_ui import show_line_tool
from dxfcad_import import import_dxf
from dxfcad_export import export_dxf

assert FreeCAD.GuiUp
profile = Path(FreeCAD.getUserAppDataDir())
assert str(profile) in DXFCAD.__file__
FreeCADGui.activateWorkbench("DXFCADWorkbench")
assert "DXFCAD_DrawLine" in FreeCADGui.listCommands()
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
    panel = show_line_tool()
    FreeCADGui.updateGui()
    assert panel.form.grab().save(str(profile/"draw-lines-panel.png"))
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
    move(10,6)
    assert tool.candidate.x == 10 and tool.candidate.y == 6
    assert "center" in panel.status.text()
    assert not created()  # The preview is not a document object.
    click(10,6)
    assert len(created()) == 1
    click(6,1)
    assert len(created()) == 2
    first = DXFCAD.parse_entity(created()[0].DXFData).snap_points()
    assert (first[0].position.x,first[0].position.y,first[1].position.x,first[1].position.y) == (3,1,10,6)
    click(6,1)
    assert len(created()) == 2 and "distinct" in panel.error.text()
    move(7,-5)
    assert tool.candidate is None and "plane" in panel.error.text()
    click(7,-5)
    assert len(created()) == 2
    click(6,1,coin.SoMouseButtonEvent.BUTTON2)
    assert tool.start_point is None and len(created()) == 2
    click(2,-4,ctrl=True)
    assert tool.start_point is None
    click(2,-4)
    assert tool.start_point is not None
    escape()
    assert tool.start_point is None and len(created()) == 2
    scene, callback = tool.event_scene, tool.event_node
    escape()
    assert tool.callback is None and scene.findChild(callback) == -1
    panel.toggle_pick(True)
    for field,value in zip([*panel.start_fields,*panel.end_fields,panel.elevation],("0","0","3","4","2")):
        field.setText(value)
    panel.create_numeric()
    assert not panel.error.text(), panel.error.text()
    assert len(created()) == 3
    last_name = created()[-1].Name
    assert abs(created()[-1].Shape.Length-5) < 1e-7
    assert abs(created()[-1].Shape.BoundBox.ZMin-2) < 1e-6
    raw = {o.Name:o.DXFData for o in created()}
    panel.reject()
    assert tool.callback is None and tool.overlay is None
    assert original == {name:doc.getObject(name).DXFData for name in original}
    doc.undo()
    doc.recompute()
    assert doc.getObject(last_name) is None
    doc.redo()
    doc.recompute()
    assert doc.getObject(last_name).DXFData == raw[last_name]
    saved = folder/"lines.FCStd"
    doc.saveAs(str(saved))
    FreeCAD.closeDocument(doc.Name)
    source.unlink()
    doc = FreeCAD.openDocument(str(saved))
    doc.recompute()
    assert all(doc.getObject(name).DXFData == record for name,record in raw.items())
    output = folder/"exported.dxf"
    export_dxf(output,doc)
    parsed = DXFCAD.DXFDocument.read(str(output))
    assert len(parsed.entities) == 6
    assert len({e.handle for e in parsed.entities}) == 6
    assert "Retain opaque data" in output.read_text()
    # The opaque SPLINE above is deliberately incomplete. Use a clean drawing
    # of the newly created lines for the independent reader's interoperability check.
    clean = folder/"independent-lines.dxf"
    DXFCAD.DXFDocument.empty(4).write(str(clean),added=list(raw.values()))
    import Import
    independent = FreeCAD.newDocument("IndependentLines")
    Import.readDXF(str(clean),independent.Name,False)
    independent.recompute()
    assert any(abs(o.Shape.Length-5) < 1e-7 and abs(o.Shape.BoundBox.ZMin-2) < 1e-7
               for o in independent.Objects if hasattr(o,"Shape") and not o.Shape.isNull())
    FreeCAD.closeDocument(independent.Name)
    FreeCAD.closeDocument(doc.Name)

# Starting a new drawing and leaving without a segment leaves no empty document.
panel = show_line_tool()
name = panel.document.Name
panel.reject()
assert name not in FreeCAD.listDocuments()
# View/document changes detach callbacks without leaving a pending segment.
doc = FreeCAD.newDocument("LineCleanup")
panel = show_line_tool()
tool = panel.viewport_tool
other = FreeCAD.newDocument("OtherView")
tool.check_view()
assert tool.callback is None
panel.reject()
FreeCAD.closeDocument(other.Name)
FreeCAD.closeDocument(doc.Name)
doc = FreeCAD.newDocument("CloseLineView")
panel = show_line_tool()
tool = panel.viewport_tool
tool.start_point = DXFCAD.Vector3D(0,0,0)
tool.render(DXFCAD.Vector3D(1,0,0))
FreeCAD.closeDocument(doc.Name)
assert tool.callback is None and tool.overlay is None
panel.reject()
doc = FreeCAD.newDocument("ExternalLinePanel")
panel = show_line_tool()
tool = panel.viewport_tool
tool.start_point = DXFCAD.Vector3D(0,0,0)
tool.render(DXFCAD.Vector3D(1,0,0))
scene, callback = tool.event_scene, tool.event_node
FreeCADGui.Control.closeDialog()
FreeCADGui.updateGui()
assert tool.callback is None and tool.overlay is None and scene.findChild(callback) == -1
FreeCAD.closeDocument(doc.Name)
