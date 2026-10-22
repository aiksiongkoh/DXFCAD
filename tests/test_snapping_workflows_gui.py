"""Exercise shared snapping and observer cleanup across consecutive tools."""
import FreeCAD
import FreeCADGui
from pathlib import Path
import gc
import runpy

folder=Path(__file__).resolve().parent
for script in ("object_snapping","polyline_drag_snap","line_creation","move"):
    assert not FreeCADGui.Control.activeDialog() and not FreeCAD.listDocuments()
    FreeCAD.Console.PrintMessage("Shared snapping checks: "+script+"\n")
    runpy.run_path(str(folder/("test_"+script+"_gui.py")),run_name="dxfcad_snapping_"+script)
    assert not FreeCADGui.Control.activeDialog()
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    FreeCADGui.updateGui()
    gc.collect()
print("Shared snapping workflows, consecutive tool use and observer cleanup passed.")
