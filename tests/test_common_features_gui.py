"""Exercise the common drafting tools consecutively in one FreeCAD session."""
import FreeCAD
import FreeCADGui
from pathlib import Path
import runpy
import gc

folder=Path(__file__).resolve().parent
scripts=("line_creation","circle_creation","rectangle","move","copy","rotate","scale","mirror",
         "drafting","offset","fillet","chamfer","text_editing","properties")
for script in scripts:
    assert not FreeCADGui.Control.activeDialog(),"Task panel leaked before "+script
    assert not FreeCAD.listDocuments(),"Document leaked before "+script
    FreeCAD.Console.PrintMessage("Common feature checks: "+script+"\n")
    runpy.run_path(str(folder/("test_"+script+"_gui.py")),run_name="dxfcad_common_"+script)
    FreeCADGui.updateGui()
    gc.collect()
assert not FreeCAD.listDocuments() and not FreeCADGui.Control.activeDialog()
print("All common drafting tools completed in one session without document/task-panel leaks.")
