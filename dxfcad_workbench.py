"""Python UI entry point for DXFCAD and downstream workbenches."""

import FreeCADGui
from FreeCADGui import Workbench


class DXFCADWorkbench(Workbench):
    """General DXF workbench; domain behavior belongs in the C++ module."""

    MenuText = "DXFCAD"
    ToolTip = "Create and edit native DXF entities"

    def Initialize(self):
        from dxfcad_commands import CheckWorkbenchCommand, ImportDXFCommand

        FreeCADGui.addCommand("DXFCAD_CheckWorkbench", CheckWorkbenchCommand())
        FreeCADGui.addCommand("DXFCAD_ImportDXF", ImportDXFCommand())
        commands = ["DXFCAD_ImportDXF", "DXFCAD_CheckWorkbench"]
        self.appendToolbar("DXFCAD", commands)
        self.appendMenu("DXFCAD", commands)

    def GetClassName(self):
        return "Gui::PythonWorkbench"
