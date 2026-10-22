"""Drag/release and cross-object snapping through real FreeCAD Coin events."""
import FreeCAD
import FreeCADGui
from pathlib import Path
import tempfile
import DXFCAD
from pivy import coin
from dxfcad_import import import_dxf
from dxfcad_polyline_ui import show_editor
from dxfcad_export import export_dxf

assert FreeCAD.GuiUp
profile = Path(FreeCAD.getUserAppDataDir())
assert str(profile) in DXFCAD.__file__
FreeCADGui.activateWorkbench("DXFCADWorkbench")
records = [
    "0\nLWPOLYLINE\n5\nA0\n8\n0\n90\n3\n70\n128\n230\n2\n10\n-10\n20\n-10\n42\n0.2\n40\n0.2\n41\n0.3\n91\n11\n10\n0\n20\n-10\n91\n12\n10\n0\n20\n-2\n91\n13\n1001\nDragTest\n1000\nkeep metadata\n",
    "0\nLINE\n5\nA1\n10\n3\n20\n1\n11\n6\n21\n1\n",
    "0\nCIRCLE\n5\nA2\n10\n10\n20\n6\n40\n1\n",
    "0\nARC\n5\nA3\n10\n-4\n20\n7\n40\n2\n50\n0\n51\n90\n",
    "0\nLWPOLYLINE\n5\nA4\n90\n2\n10\n-9\n20\n5\n10\n-6\n20\n3\n",
    "0\nTEXT\n5\nA5\n10\n13\n20\n12\n40\n0.4\n1\nSnap text\n",
    "0\nLINE\n5\nA6\n10\n7\n20\n-5\n30\n3\n11\n9\n21\n-5\n31\n3\n",
    "0\nLINE\n5\nA7\n10\n-5\n20\n12\n11\n-4\n21\n12\n",
    "0\nLINE\n5\nA8\n8\nHidden\n10\n5\n20\n12\n11\n6\n21\n12\n",
    "0\nCIRCLE\n5\nA9\n10\n1\n20\n2\n40\n0.4\n",
    "0\nLINE\n5\nAA\n10\n4\n20\n-1\n11\n4\n21\n4\n",
]
drawing = ("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n"
           "0\nSECTION\n2\nTABLES\n0\nLAYER\n2\nHidden\n62\n-7\n0\nENDSEC\n"
           "0\nSECTION\n2\nENTITIES\n" + "".join(records) + "0\nENDSEC\n0\nEOF\n")

with tempfile.TemporaryDirectory(dir=profile) as folder:
    path = Path(folder) / "drag-source.dxf"
    path.write_text(drawing, encoding="utf-8")
    doc = import_dxf(path)
    objects = {o.DXFHandle:o for o in doc.Objects if "DXFData" in o.PropertiesList}
    obj = objects["A0"]
    obj.ShowWidths = False
    objects["A7"].ViewObject.Visibility = False
    objects["A9"].Placement = FreeCAD.Placement(FreeCAD.Vector(14,-8,0),FreeCAD.Rotation())
    doc.recompute()
    original = obj.DXFData
    targets_before = {h:(o.DXFData,o.Shape.exportBrepToString()) for h,o in objects.items() if h != "A0"}
    view = FreeCADGui.activeDocument().activeView()
    view.setCameraType("Orthographic")
    view.viewTop()
    view.fitAll()
    panel = show_editor(obj)
    FreeCADGui.updateGui()
    panel.move_in_view.click()
    editor = panel.viewport_editor
    assert editor.view is not None, panel.error.text()
    manager = view.getViewer().getSoEventManager()

    def screen(x,y,z=0):
        return view.getPointOnScreen(FreeCAD.Vector(x,y,z))

    def button(position, state, ctrl=False):
        event = coin.SoMouseButtonEvent()
        event.setButton(coin.SoMouseButtonEvent.BUTTON1)
        event.setState(state)
        event.setPosition(coin.SbVec2s(int(position[0]),int(position[1])))
        event.setCtrlDown(ctrl)
        manager.processEvent(event)

    def move(position):
        event = coin.SoLocation2Event()
        event.setPosition(coin.SbVec2s(int(position[0]),int(position[1])))
        manager.processEvent(event)

    def current():
        p = panel._model()[0].world_vertices()[0]
        return screen(p.x,p.y,p.z)

    def drag(x,y, label=None):
        start = current()
        button(start,coin.SoButtonEvent.DOWN)
        assert editor.index == 0, panel.error.text()
        move(screen(x,y))
        assert editor.candidate is not None, panel.error.text()
        if label:
            assert label in panel.view_status.text(), panel.view_status.text()
        button(screen(x,y),coin.SoButtonEvent.UP)
        assert editor.index is None and editor.press_position is None, (x,y,start,screen(x,y),editor.index,editor.press_position,editor.dragged,panel.error.text())
        FreeCADGui.updateGui()
        assert obj.DXFData == original

    # Free dragging places on release, not on a later click.
    panel.snap_vertices.setChecked(False)
    panel.snap_objects.setChecked(False)
    drag(2,-4)
    native = panel._model()[0]
    assert abs(native.vertices[0].x-2) < 0.15 and abs(native.vertices[0].y+4) < 0.15
    assert native.vertices[0].bulge == 0.2 and native.vertices[0].end_width == 0.3
    assert native.extrusion.z == 2
    # A quick drag can arrive as press/release without an intervening motion event.
    button(current(),coin.SoButtonEvent.DOWN)
    button(screen(4,-3),coin.SoButtonEvent.UP)
    assert editor.index is None
    assert abs(panel._model()[0].vertices[0].x-4) < 0.15
    FreeCADGui.updateGui()

    panel.snap_objects.setChecked(True)
    for x,y,label in [(3,1,"endpoint"),(10,6,"center"),(-2,7,"endpoint"),
                      (-4,7,"center"),(-9,5,"vertex"),(13,12,"insertion point"),(15,-6,"center"),
                      (4.5,1,"midpoint"),(4,1,"intersection")]:
        drag(x,y,label)
        vertex = panel._model()[0].vertices[0]
        assert abs(vertex.x-x) < 1e-10 and abs(vertex.y-y) < 1e-10

    # Hidden objects and hidden layers cannot attract the pointer.
    assert editor._snap_target(screen(-5,12)) is None
    assert editor._snap_target(screen(5,12)) is None
    # Cached native points must follow live record, visibility, label, and
    # placement changes, and must not retain deleted objects as snap targets.
    from dxfcad_objects import DXFObjectProxy, DXFViewProvider
    raw = "0\nLINE\n10\n3\n20\n14\n11\n8\n21\n14\n"
    line = DXFCAD.create_feature(doc, DXFCAD.parse_entity(raw), "")
    if line.TypeId == "Part::FeaturePython":
        DXFObjectProxy(line)
    DXFViewProvider(line.ViewObject,(0.2,0.2,0.2))
    doc.recompute()
    assert editor._snap_target(screen(3,14)) is not None
    line.ViewObject.Visibility = False
    assert editor._snap_target(screen(3,14)) is None
    line.ViewObject.Visibility = True
    line.DXFData = raw.replace("10\n3\n", "10\n4\n")
    doc.recompute()
    assert editor._snap_target(screen(3,14)) is None
    assert editor._snap_target(screen(4,14)) is not None
    line.Label = "Changed label"
    assert "Changed label" in editor._snap_target(screen(4,14))[1]
    line.Placement = FreeCAD.Placement(FreeCAD.Vector(1,0,0),FreeCAD.Rotation())
    assert editor._snap_target(screen(5,14)) is not None
    cached_name = line.Name
    doc.removeObject(cached_name)
    assert editor._snap_target(screen(5,14)) is None
    assert cached_name not in editor._snap_cache
    # Dimension dependencies can change without changing their native record.
    # Snap points must invalidate when the retained picture block changes.
    dimension_raw = ("0\nDIMENSION\n2\n*Snap\n70\n32\n10\n3\n20\n14\n11\n5\n21\n14\n"
                     "13\n3\n23\n12\n14\n8\n24\n12\n")
    context = "0\nBLOCK\n2\n*Snap\n"+raw.replace("0\nLINE\n", "0\nLINE\n62\n0\n")+"0\nENDBLK\n"
    dimension = DXFCAD.create_feature(doc, DXFCAD.parse_entity(dimension_raw, context), "")
    if dimension.TypeId == "Part::FeaturePython":
        DXFObjectProxy(dimension)
    DXFViewProvider(dimension.ViewObject,(0.2,0.2,0.2))
    doc.recompute()
    assert editor._snap_target(screen(3,14)) is not None
    drag(3,14,"endpoint")
    dimension.DXFContext = context.replace("10\n3\n", "10\n4\n")
    doc.recompute()
    assert editor._snap_target(screen(3,14)) is None
    assert editor._snap_target(screen(4,14)) is not None
    assert dimension.DXFData == dimension_raw
    doc.removeObject(dimension.Name)
    # Reject a visible off-plane target; retain the last placed table value.
    before = panel._model()[0].vertices[0]
    button(current(),coin.SoButtonEvent.DOWN)
    move(screen(7,-5,3))
    assert editor.candidate is None and "plane" in panel.error.text()
    button(screen(7,-5,3),coin.SoButtonEvent.UP)
    assert editor.index is None and "plane" in panel.error.text()
    assert panel._model()[0].vertices[0].x == before.x
    assert obj.DXFData == original

    # Escape during a held drag prevents release from placing anything.
    button(current(),coin.SoButtonEvent.DOWN)
    move(screen(3,1))
    key = coin.SoKeyboardEvent()
    key.setKey(coin.SoKeyboardEvent.ESCAPE)
    key.setState(coin.SoButtonEvent.DOWN)
    manager.processEvent(key)
    button(screen(3,1),coin.SoButtonEvent.UP)
    assert editor.index is None and panel._model()[0].vertices[0].x == before.x
    button(current(),coin.SoButtonEvent.DOWN,ctrl=True)
    assert editor.index is None  # Modified gestures remain available to navigation.
    button(current(),coin.SoButtonEvent.UP,ctrl=True)

    # Deactivation while held also detaches every callback and pending drag.
    button(current(),coin.SoButtonEvent.DOWN)
    move(screen(3,1))
    event_scene, event_node = editor.event_scene, editor.event_node
    panel.move_in_view.click()
    assert editor.callback is None and editor.press_position is None
    assert event_scene.findChild(event_node) == -1
    button(screen(3,1),coin.SoButtonEvent.UP)
    panel.reject()
    assert obj.DXFData == original

    # Update plans validate the complete drawing. Keep the placed reference for
    # the read-only snap checks above, then reset it before opening the commit session.
    objects["A9"].Placement = FreeCAD.Placement()
    doc.recompute()
    targets_before["A9"] = (objects["A9"].DXFData, objects["A9"].Shape.exportBrepToString())
    panel = show_editor(obj)
    panel.move_in_view.click()
    editor = panel.viewport_editor
    # Escape while idle detaches the callback from inside its own dispatch.
    manager.processEvent(key)
    assert editor.callback is None and editor.event_node is None
    panel.move_in_view.click()
    editor = panel.viewport_editor
    drag(3,1,"endpoint")
    assert panel.accept(), panel.error.text()
    edited = obj.DXFData
    assert "keep metadata" in edited and "91\n11\n" in edited and "70\n128\n" in edited
    for h,(raw,shape) in targets_before.items():
        assert objects[h].DXFData == raw and objects[h].Shape.exportBrepToString() == shape
    doc.undo()
    doc.recompute()
    assert obj.DXFData == original
    doc.redo()
    doc.recompute()
    assert obj.DXFData == edited
    # Generic target Placements are respected for snapping; reset before export,
    # whose existing contract requires native entity transforms.
    objects["A9"].Placement = FreeCAD.Placement()
    output = Path(folder) / "drag-export.dxf"
    export_dxf(output,doc)
    parsed = DXFCAD.DXFDocument.read(str(output))
    assert len(parsed.entities) == len(objects)
    result = DXFCAD.read_lwpolyline(next(e.raw for e in parsed.entities if e.handle == "A0"))
    assert result.vertices[0].x == 3 and result.vertices[0].y == 1
