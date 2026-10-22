"""Real Coin event dispatch through the FreeCAD viewport, using synthetic data."""
import FreeCAD
import FreeCADGui
from pathlib import Path
import tempfile
import DXFCAD
from pivy import coin
from dxfcad_edit import create_polyline
from dxfcad_polyline_ui import show_editor
from dxfcad_export import export_dxf

assert FreeCAD.GuiUp
profile = Path(FreeCAD.getUserAppDataDir())
assert str(profile) in DXFCAD.__file__
stage_file = profile / "viewport-stages.txt"
stage_file.write_text("entered test\n", encoding="utf-8")
def checkpoint(*args):
    with stage_file.open("a", encoding="utf-8") as output:
        output.write(" ".join(str(arg) for arg in args)+"\n")

FreeCADGui.activateWorkbench("DXFCADWorkbench")
checkpoint("Viewport stage: initialized")
doc = FreeCAD.newDocument("VertexPicking")
base = DXFCAD.LWPolyline([DXFCAD.LWPolylineVertex(0,0,0.25,0.2,0.4,True),
                         DXFCAD.LWPolylineVertex(10,0), DXFCAD.LWPolylineVertex(10,10)],
                        elevation=4, extrusion=DXFCAD.ExtrusionDirection(1,2,3))
obj = create_polyline(doc, base)
obj.ShowWidths = False
# Native metadata must survive viewport editing.
obj.DXFData += "1001\nViewportTest\n1000\nuntouched\n"
doc.recompute()
original = obj.DXFData
original_shape = obj.Shape.exportBrepToString()
count = len(doc.Objects)
view = FreeCADGui.activeDocument().activeView()
view.setCameraType("Orthographic")
view.setCameraOrientation(FreeCAD.Rotation(FreeCAD.Vector(0,0,1),FreeCAD.Vector(1,2,3)).Q)
view.fitAll()
panel = show_editor(obj)
FreeCADGui.updateGui()
panel.move_in_view.click()
assert panel.viewport_editor is not None, panel.error.text()
editor = panel.viewport_editor
assert editor.view is not None, panel.error.text()
checkpoint("Viewport stage: editor started")
editor._check_view()
assert editor.view is not None, "Active-view check must retain the correct view"
scene = view.getSceneGraph()
overlay = editor.overlay
manager = view.getViewer().getSoEventManager()


def screen(point):
    return view.getPointOnScreen(FreeCAD.Vector(point.x,point.y,point.z))


def click(position, button=coin.SoMouseButtonEvent.BUTTON1):
    for state in (coin.SoButtonEvent.DOWN, coin.SoButtonEvent.UP):
        event = coin.SoMouseButtonEvent()
        event.setPosition(coin.SbVec2s(int(position[0]),int(position[1])))
        event.setButton(button)
        event.setState(state)
        manager.processEvent(event)


def move(position):
    event = coin.SoLocation2Event()
    event.setPosition(coin.SbVec2s(int(position[0]),int(position[1])))
    manager.processEvent(event)


def escape():
    event = coin.SoKeyboardEvent()
    event.setKey(coin.SoKeyboardEvent.ESCAPE)
    event.setState(coin.SoButtonEvent.DOWN)
    manager.processEvent(event)


# Picking and hovering change only a transient analytic-centerline preview.
click(screen(base.world_vertices()[0]))
assert editor.index == 0, panel.error.text()
move(screen(base.world_vertices()[1]))
assert editor.candidate is None and panel.error.text()  # A collapsed arc is invalid.
assert panel._model()[0].vertices[0].x == 0
target_model = base.with_vertex(0, DXFCAD.LWPolylineVertex(3,2,0.25,0.2,0.4,True))
position = screen(target_model.world_vertices()[0])
move(position)
assert editor.candidate is not None, panel.error.text()
panel.form.grab().save(str(profile / "vertex-move-panel.png"))
# FreeCAD's saved-picture path omits loose viewer nodes. Include the transient
# preview under a view-provider root for this capture only, then detach it.
obj.ViewObject.RootNode.addChild(editor.overlay)
try:
    checkpoint("Viewport stage: capture")
    view.saveImage(str(profile / "vertex-move-preview.png"), 1000, 700, "White")
finally:
    obj.ViewObject.RootNode.removeChild(editor.overlay)
checkpoint("Viewport stage: capture complete")
assert obj.DXFData == original and obj.Shape.exportBrepToString() == original_shape
assert len(doc.Objects) == count
escape()
assert editor.index is None and editor.view is not None
assert panel._model()[0].vertices[0].x == 0

# Place a point in a tilted plane, with screen-pixel precision.
click(screen(base.world_vertices()[0]))
move(position)
click(position)
assert editor.index is None, panel.error.text()
placed = panel._model()[0]
assert abs(placed.vertices[0].x-3) < 0.15 and abs(placed.vertices[0].y-2) < 0.15
assert placed.elevation == 4 and placed.extrusion.y == 2
assert placed.vertices[0].bulge == 0.25 and placed.vertices[0].end_width == 0.4
assert obj.DXFData == original
FreeCADGui.updateGui()

# Snap to the last vertex in screen space; C++ retains exact native coordinates.
click(screen(placed.world_vertices()[0]))
click(screen(placed.world_vertices()[2]))
assert panel._model()[0].vertices[0].x == 10
assert panel._model()[0].vertices[0].y == 10
assert obj.DXFData == original
last_overlay = editor.overlay
assert panel.accept(), panel.error.text()
checkpoint("Viewport stage: accepted")
assert editor.callback is None and editor.overlay is None
assert scene.findChild(last_overlay) == -1
assert "untouched" in obj.DXFData
edited = obj.DXFData
doc.undo()
doc.recompute()
assert obj.DXFData == original
doc.redo()
doc.recompute()
checkpoint("Viewport stage: undo redo complete")
assert obj.DXFData == edited
with tempfile.TemporaryDirectory(dir=profile) as folder:
    output = Path(folder) / "picked.dxf"
    export_dxf(output,doc)
    parsed = DXFCAD.DXFDocument.read(str(output))
    assert DXFCAD.read_lwpolyline(parsed.entities[0].raw).vertices[0].x == 10

# Canceling the panel and switching documents both detach events and overlays.
panel = show_editor(obj)
checkpoint("Viewport stage: perspective")
panel.move_in_view.click()
editor = panel.viewport_editor
assert editor.view is not None, panel.error.text()
view.setCameraType("Perspective")
view.fitAll()
panel.snap_vertices.setChecked(False)
current = panel._model()[0]
click(screen(current.world_vertices()[0]))
target = current.with_vertex(0,DXFCAD.LWPolylineVertex(2,2,0.25,0.2,0.4,True))
move(screen(target.world_vertices()[0]))
assert editor.candidate is not None, panel.error.text()
assert abs(editor.candidate.vertices[0].x-2) < 0.15
assert abs(editor.candidate.vertices[0].y-2) < 0.15
click(screen(target.world_vertices()[0]))
assert abs(panel._model()[0].vertices[0].x-2) < 0.15
panel.reject()
checkpoint("Viewport stage: perspective canceled")
assert editor.callback is None and editor.overlay is None
assert obj.DXFData == edited
panel = show_editor(obj)
checkpoint("Viewport stage: switch editor created")
panel.move_in_view.click()
checkpoint("Viewport stage: switch editor started")
editor = panel.viewport_editor
other = FreeCAD.newDocument("OtherView")
checkpoint("Viewport stage: document switch")
editor._check_view()
assert editor.callback is None and editor.overlay is None
FreeCAD.closeDocument(other.Name)
FreeCAD.setActiveDocument(doc.Name)
panel.reject()  # Task panels belong to their own GUI document.
checkpoint("Viewport stage: returned to document")
panel = show_editor(obj)
panel.move_in_view.click()
editor = panel.viewport_editor
FreeCADGui.Control.closeDialog()  # Closing outside accept/reject must detach too.
FreeCADGui.updateGui()
assert editor.callback is None and editor.overlay is None
panel = show_editor(obj)
panel.move_in_view.click()
editor = panel.viewport_editor
checkpoint("Viewport cleanup: closing document with an active editor")
FreeCAD.closeDocument(doc.Name)
assert editor.callback is None and editor.overlay is None
checkpoint("Viewport cleanup: document closed and editor detached")
