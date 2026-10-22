"""Run separate-line fillet/chamfer workflows consecutively in one session."""
import FreeCAD
import FreeCADGui
from pathlib import Path
import gc
import runpy

folder=Path(__file__).resolve().parent
for operation in ("fillet","chamfer"):
    assert not FreeCAD.listDocuments() and not FreeCADGui.Control.activeDialog()
    runpy.run_path(str(folder/("test_line_"+operation+"_gui.py")),run_name="dxfcad_line_"+operation)
    FreeCADGui.updateGui()
    gc.collect()
    assert not FreeCAD.listDocuments() and not FreeCADGui.Control.activeDialog()
print("Separate-line Fillet and Chamfer workflows passed with no document/task-panel leaks.")
