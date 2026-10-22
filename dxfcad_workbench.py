"""Python UI entry point for DXFCAD and downstream workbenches."""

import FreeCADGui
from FreeCADGui import Workbench


class DXFCADWorkbench(Workbench):
    """General DXF workbench; domain behavior belongs in the C++ module."""

    MenuText = "DXFCAD"
    ToolTip = "Create and edit native DXF entities"

    def Initialize(self):
        from dxfcad_commands import (
            CheckWorkbenchCommand, ImportDXFCommand, CreatePolylineCommand,
            EditPolylineCommand, ExportDXFCommand, SavedViewsCommand,
            DrawLineCommand, DrawCircleCommand, DrawArcCommand, MoveCommand, CopyCommand,
            RotateCommand, ScaleCommand, MirrorCommand, RectangleCommand,
            DraftingCommand, ExtendCommand, OffsetCommand, FilletCommand,
            ChamferCommand, CreateTextCommand, EditTextCommand, PropertiesCommand, FilletLinesCommand, ChamferLinesCommand, ContourCommand,
        )

        FreeCADGui.addCommand("DXFCAD_CheckWorkbench", CheckWorkbenchCommand())
        FreeCADGui.addCommand("DXFCAD_ImportDXF", ImportDXFCommand())
        FreeCADGui.addCommand("DXFCAD_DrawLine", DrawLineCommand())
        FreeCADGui.addCommand("DXFCAD_DrawCircle", DrawCircleCommand())
        FreeCADGui.addCommand("DXFCAD_DrawArc", DrawArcCommand())
        FreeCADGui.addCommand("DXFCAD_Rectangle", RectangleCommand())
        FreeCADGui.addCommand("DXFCAD_Trim", DraftingCommand())
        FreeCADGui.addCommand("DXFCAD_Extend", ExtendCommand())
        for name,operation in (("Join","join"),("Split","split"),("Break","break"),("Close","close"),("TrimInterior","trim")):
            FreeCADGui.addCommand("DXFCAD_"+name,ContourCommand(operation))
        FreeCADGui.addCommand("DXFCAD_Offset", OffsetCommand())
        FreeCADGui.addCommand("DXFCAD_Fillet", FilletCommand())
        FreeCADGui.addCommand("DXFCAD_FilletLines", FilletLinesCommand())
        FreeCADGui.addCommand("DXFCAD_ChamferLines", ChamferLinesCommand())
        FreeCADGui.addCommand("DXFCAD_Chamfer", ChamferCommand())
        FreeCADGui.addCommand("DXFCAD_CreateText", CreateTextCommand())
        FreeCADGui.addCommand("DXFCAD_EditText", EditTextCommand())
        FreeCADGui.addCommand("DXFCAD_Properties", PropertiesCommand())
        FreeCADGui.addCommand("DXFCAD_Move", MoveCommand())
        FreeCADGui.addCommand("DXFCAD_Copy", CopyCommand())
        FreeCADGui.addCommand("DXFCAD_Rotate", RotateCommand())
        FreeCADGui.addCommand("DXFCAD_Scale", ScaleCommand())
        FreeCADGui.addCommand("DXFCAD_Mirror", MirrorCommand())
        FreeCADGui.addCommand("DXFCAD_CreateLWPolyline", CreatePolylineCommand())
        FreeCADGui.addCommand("DXFCAD_EditLWPolyline", EditPolylineCommand())
        FreeCADGui.addCommand("DXFCAD_ExportDXF", ExportDXFCommand())
        FreeCADGui.addCommand("DXFCAD_SavedViews", SavedViewsCommand())
        draw = ["DXFCAD_DrawLine", "DXFCAD_DrawCircle", "DXFCAD_DrawArc", "DXFCAD_Rectangle",
                "DXFCAD_CreateLWPolyline", "DXFCAD_CreateText"]
        modify = ["DXFCAD_Move", "DXFCAD_Copy", "DXFCAD_Rotate", "DXFCAD_Scale",
                  "DXFCAD_Mirror", "DXFCAD_Offset", "DXFCAD_FilletLines", "DXFCAD_ChamferLines", "DXFCAD_Properties"]
        detail = ["DXFCAD_Trim", "DXFCAD_Extend", "DXFCAD_Fillet", "DXFCAD_Chamfer",
                  "DXFCAD_EditLWPolyline", "DXFCAD_EditText"]
        drawing = ["DXFCAD_ImportDXF", "DXFCAD_ExportDXF", "DXFCAD_SavedViews"]
        contours=["DXFCAD_Join","DXFCAD_Split","DXFCAD_Break","DXFCAD_Close","DXFCAD_TrimInterior"]
        self.appendToolbar("DXFCAD Draw", draw)
        self.appendToolbar("DXFCAD Modify", modify)
        self.appendToolbar("DXFCAD Drawing", drawing)
        self.appendToolbar("DXFCAD Contour", contours)
        self.appendMenu("DXFCAD", drawing+draw+modify+detail+contours+["DXFCAD_CheckWorkbench"])

    def GetClassName(self):
        return "Gui::PythonWorkbench"
