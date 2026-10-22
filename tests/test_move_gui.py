"""Multi-object native Move, native snapping, cancellation, persistence, and export."""
import FreeCAD
import FreeCADGui
from pathlib import Path
import tempfile
import DXFCAD
from pivy import coin
from dxfcad_move_ui import show_move_tool
from dxfcad_import import import_dxf
from dxfcad_export import export_dxf

assert FreeCAD.GuiUp
profile = Path(FreeCAD.getUserAppDataDir())
assert str(profile) in DXFCAD.__file__
FreeCADGui.activateWorkbench("DXFCADWorkbench")
assert "DXFCAD_Move" in FreeCADGui.listCommands()
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
    panel = show_move_tool(selected)
    FreeCADGui.updateGui()
    assert panel.form.grab().save(str(profile/"move-objects-panel.png"))
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
    def state():
        return {o.Name:o.DXFData for o in doc.Objects if "DXFData" in o.PropertiesList}
    before = state()
    assert tool.callback is not None
    click(3,1)
    assert tool.start_point.x == 3 and tool.start_point.y == 1
    move(6,1)
    assert tool.candidate.x == 6 and tool.overlay is not None
    assert state() == before  # Live preview never mutates native objects.
    assert [float(f.text()) for f in panel.delta_fields] == [3,0,0]
    assert view.getSceneGraph().findChild(tool.overlay) >= 0
    FreeCADGui.updateGui()
    FreeCADGui.getMainWindow().grab().save(str(profile/"move-window-preview.png"))
    view.saveImage(str(profile/"move-preview.png"),900,600,"Current")
    assert (profile/"move-preview.png").is_file()
    move(7,-5)
    assert tool.candidate is None and "plane" in panel.error.text()
    click(3,1)
    assert "nonzero" in panel.error.text() and state() == before
    escape()
    assert tool.start_point is None and tool.overlay is None and state() == before
    click(3,1)
    click(6,1,coin.SoMouseButtonEvent.BUTTON2)
    assert tool.start_point is None and state() == before
    click(3,1,ctrl=True)
    assert tool.start_point is None
    click(3,1)
    move(10,6)
    assert "center" in panel.status.text()
    click(10,6)
    assert tool.start_point is None and tool.overlay is None
    after_pick = state()
    for obj in selected:
        assert obj.Placement.isIdentity()
        a = DXFCAD.parse_entity(before[obj.Name]).snap_points()
        b = DXFCAD.parse_entity(after_pick[obj.Name]).snap_points()
        for old,new in zip(a,b):
            assert abs(new.position.x-old.position.x-7)<1e-8
            assert abs(new.position.y-old.position.y-5)<1e-8
            assert abs(new.position.z-old.position.z)<1e-8
    doc.undo()
    doc.recompute()
    assert state() == before
    doc.redo()
    doc.recompute()
    assert state() == after_pick
    for field,value in zip(panel.delta_fields,("-2","3","4")):
        field.setText(value)
    panel.create_numeric()
    assert not panel.error.text(), panel.error.text()
    after_numeric = state()
    assert after_numeric != after_pick
    for obj in selected:
        old = DXFCAD.parse_entity(after_pick[obj.Name]).snap_points()[0].position
        new = DXFCAD.parse_entity(obj.DXFData).snap_points()[0].position
        assert abs(new.x-old.x+2)<1e-8 and abs(new.y-old.y-3)<1e-8 and abs(new.z-old.z-4)<1e-8
    doc.undo()
    doc.recompute()
    assert state() == after_pick
    panel.create_numeric()
    assert "changed" in panel.error.text() and state() == after_pick
    doc.redo()
    doc.recompute()
    assert state() == after_numeric
    for field in panel.delta_fields:
        field.setText("0")
    panel.create_numeric()
    assert "nonzero" in panel.error.text() and state() == after_numeric
    panel.delta_fields[0].setText("nan")
    panel.create_numeric()
    assert "finite" in panel.error.text() and state() == after_numeric
    panel.reject()
    assert tool.callback is None and tool.overlay is None
    out = folder/"moved.dxf"
    export_dxf(out,doc)
    parsed = DXFCAD.DXFDocument.read(str(out))
    assert len(parsed.entities) == 7
    assert "Retain opaque data" in out.read_text()
    by_handle = {DXFCAD.parse_entity(o.DXFData).handle:o.DXFData for o in selected}
    for entity in parsed.entities:
        if entity.handle in by_handle:
            assert entity.raw == by_handle[entity.handle]
    # FreeCAD's independent DXF reader reads the moved primitives and their elevation.
    clean = folder/"clean-moved.dxf"
    primitives = [DXFCAD.parse_entity(o.DXFData) for o in selected if o.DXFType in ("LINE","CIRCLE")]
    # Supply valid R2000 tables/blocks and fresh handles to the independent reader.
    DXFCAD.DXFDocument.empty(4).write(str(clean),added=[e.raw.replace("\n5\n"+e.handle+"\n","\n",1) for e in primitives])
    import Import
    independent = FreeCAD.newDocument("IndependentMove")
    Import.readDXF(str(clean),independent.Name,False)
    independent.recompute()
    shapes = [o.Shape for o in independent.Objects if hasattr(o,"Shape") and not o.Shape.isNull()]
    assert len(shapes) == 2
    assert all(abs(s.BoundBox.ZMin-4)<1e-6 for s in shapes)
    FreeCAD.closeDocument(independent.Name)
    FreeCAD.setActiveDocument(doc.Name)
    saved = folder/"moved.FCStd"
    doc.saveAs(str(saved))
    FreeCAD.closeDocument(doc.Name)
    source.unlink()
    reopened = FreeCAD.openDocument(str(saved))
    assert {name:reopened.getObject(name).DXFData for name in after_numeric} == after_numeric
    selected = [reopened.getObject(name) for name in selected_names]
    panel = show_move_tool(selected)
    tool = panel.viewport_tool
    tool.start_point = DXFCAD.Vector3D(0,0,0)
    tool.render(DXFCAD.Vector3D(2,0,0))
    other = FreeCAD.newDocument("OtherMoveView")
    tool.check_view()
    assert tool.callback is None and tool.overlay is None
    FreeCAD.closeDocument(other.Name)
    FreeCAD.setActiveDocument(reopened.Name)
    FreeCADGui.updateGui()
    panel.reject()
    panel = show_move_tool(selected)
    tool = panel.viewport_tool
    tool.start_point = DXFCAD.Vector3D(0,0,0)
    tool.render(DXFCAD.Vector3D(2,0,0))
    FreeCAD.closeDocument(reopened.Name)
    assert tool.callback is None and tool.overlay is None
    panel.reject()
    try:
        show_move_tool([])
        raise AssertionError("Empty selection accepted")
    except ValueError:
        pass
print("Six entity types: snapped Move, preview, cancel, undo, numeric displacement, save/reopen and export passed.")
