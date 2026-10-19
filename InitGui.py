"""Register the standalone DXFCAD workbench."""

import FreeCADGui

from dxfcad_workbench import DXFCADWorkbench

FreeCADGui.addWorkbench(DXFCADWorkbench())
