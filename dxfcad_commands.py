"""UI commands for the standalone DXFCAD workbench."""

import FreeCAD


class CheckWorkbenchCommand:
    """Check UI registration without requiring or changing a document."""

    def GetResources(self):
        return {
            "MenuText": "Check DXFCAD Workbench",
            "ToolTip": "Print a workbench loading confirmation in Report view",
        }

    def IsActive(self):
        return True

    def Activated(self):
        FreeCAD.Console.PrintMessage("DXFCAD workbench loaded successfully.\n")


class ImportDXFCommand:
    """Choose a drawing and show its native DXF objects in a new document."""

    def GetResources(self):
        return {"MenuText": "Import Native DXF...",
                "ToolTip": "Display native DXF entities using DXFCAD"}

    def IsActive(self):
        return True

    def Activated(self):
        import FreeCADGui
        from PySide import QtWidgets
        from dxfcad_import import import_dxf

        filename, _ = QtWidgets.QFileDialog.getOpenFileName(
            FreeCADGui.getMainWindow(), "Import Native DXF", "", "DXF drawings (*.dxf)")
        if not filename:
            return
        try:
            import_dxf(filename)
            FreeCADGui.activeDocument().activeView().viewTop()
            FreeCADGui.activeDocument().activeView().fitAll()
        except Exception as error:
            FreeCAD.Console.PrintError(f"DXFCAD import failed: {error}\n")
            QtWidgets.QMessageBox.critical(FreeCADGui.getMainWindow(), "DXFCAD import", str(error))
