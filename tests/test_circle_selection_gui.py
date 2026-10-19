"""Run with a FreeCAD GUI runtime and DXFCAD installed in its test profile."""
import FreeCAD
import FreeCADGui

assert FreeCAD.GuiUp, "Run through run_gui_test.py with the FreeCAD executable"

from pathlib import Path
from pivy import coin
import json
import DXFCAD
from dxfcad_objects import DXFObjectProxy, DXFViewProvider

profile = Path(FreeCAD.getUserAppDataDir())
assert str(profile) in DXFCAD.__file__
doc = FreeCAD.newDocument("CircleSelection")
raw = "0\nCIRCLE\n10\n1\n20\n2\n30\n3\n40\n2\n210\n0\n220\n1\n230\n0\n"
obj = DXFCAD.create_feature(doc, DXFCAD.parse_entity(raw), "")
if obj.TypeId == "Part::FeaturePython":
    DXFObjectProxy(obj)
DXFViewProvider(obj.ViewObject, (0.2, 0.2, 0.2))
doc.recompute()
proxy = obj.ViewObject.Proxy
assert proxy._center_switch.whichChild.getValue() == coin.SO_SWITCH_NONE
before = obj.Shape.exportBrepToString()
FreeCADGui.Selection.addSelection(obj)
FreeCADGui.updateGui()
assert proxy._center_switch.whichChild.getValue() == coin.SO_SWITCH_ALL
center = proxy._center_coordinates.point.getValues()[0]
assert (FreeCAD.Vector(*center) - FreeCAD.Vector(-1, 3, 2)).Length < 1e-7
assert obj.DXFData == raw and obj.Shape.exportBrepToString() == before
other = DXFCAD.create_feature(doc, DXFCAD.parse_entity(raw), "")
if other.TypeId == "Part::FeaturePython":
    DXFObjectProxy(other)
DXFViewProvider(other.ViewObject, (0.2, 0.2, 0.2))
doc.recompute()
FreeCADGui.Selection.addSelection(other)
assert proxy._center_switch.whichChild.getValue() == coin.SO_SWITCH_ALL
assert other.ViewObject.Proxy._center_switch.whichChild.getValue() == coin.SO_SWITCH_ALL
FreeCADGui.Selection.removeSelection(other)
assert other.ViewObject.Proxy._center_switch.whichChild.getValue() == coin.SO_SWITCH_NONE
doc.removeObject(other.Name)
obj.Placement = FreeCAD.Placement(FreeCAD.Vector(5, 0, 0), FreeCAD.Rotation())
doc.recompute()
assert proxy._center_switch.whichChild.getValue() == coin.SO_SWITCH_ALL
center = proxy._center_coordinates.point.getValues()[0]
assert (FreeCAD.Vector(*center) - FreeCAD.Vector(-1, 3, 2)).Length < 1e-7
obj.ViewObject.Visibility = False
assert proxy._center_switch.whichChild.getValue() == coin.SO_SWITCH_NONE
obj.ViewObject.Visibility = True
assert proxy._center_switch.whichChild.getValue() == coin.SO_SWITCH_ALL
FreeCADGui.Selection.removeSelection(obj)
assert proxy._center_switch.whichChild.getValue() == coin.SO_SWITCH_NONE
FreeCADGui.Selection.addSelection(doc.Name, obj.Name, "Edge1")
assert proxy._center_switch.whichChild.getValue() == coin.SO_SWITCH_ALL
FreeCADGui.Selection.clearSelection()
assert proxy._center_switch.whichChild.getValue() == coin.SO_SWITCH_NONE

# Save and reopen: transient scene nodes and the observer must be recreated.
saved = profile / "circle-selection.FCStd"
doc.saveAs(str(saved))
FreeCAD.closeDocument(doc.Name)
doc = FreeCAD.openDocument(str(saved))
obj = doc.getObject("DXFCIRCLE")
proxy = obj.ViewObject.Proxy
assert proxy._center_switch.whichChild.getValue() == coin.SO_SWITCH_NONE
FreeCADGui.Selection.addSelection(obj)
FreeCADGui.updateGui()
assert proxy._center_switch.whichChild.getValue() == coin.SO_SWITCH_ALL
FreeCADGui.activeDocument().activeView().viewFront()
FreeCADGui.activeDocument().activeView().fitAll()
FreeCADGui.activeDocument().activeView().saveImage(str(profile / "selected-circle.png"), 800, 600, "Current")
FreeCADGui.Selection.clearSelection()
assert proxy._center_switch.whichChild.getValue() == coin.SO_SWITCH_NONE

# Recompute a selected circle to a new center, then an invalid shape and back.
FreeCADGui.Selection.addSelection(obj)
obj.DXFData = raw.replace("10\n1\n", "10\n4\n")
doc.recompute()
center = proxy._center_coordinates.point.getValues()[0]
assert (FreeCAD.Vector(*center) - FreeCAD.Vector(-4, 3, 2)).Length < 1e-7
obj.DXFData = raw.replace("40\n2\n", "40\n0\n")
doc.recompute()
assert proxy._center_switch.whichChild.getValue() == coin.SO_SWITCH_NONE
obj.DXFData = raw
doc.recompute()
assert proxy._center_switch.whichChild.getValue() == coin.SO_SWITCH_ALL
FreeCADGui.Selection.clearSelection()
name = obj.Name
doc.removeObject(name)
doc.recompute()
FreeCAD.closeDocument(doc.Name)
result = {"native": DXFCAD.NATIVE_DOCUMENT_OBJECTS, "selection_center": "passed"}
(profile / "circle-selection-validation.json").write_text(json.dumps(result))
print(result, flush=True)
