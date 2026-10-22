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


class DrawLineCommand:
    def GetResources(self):
        return {"MenuText": "Draw Lines...", "ToolTip": "Draw connected native DXF lines with snapping and undo"}

    def IsActive(self):
        return True

    def Activated(self):
        import FreeCADGui
        from dxfcad_edit import drawing_root
        from dxfcad_line_ui import show_line_tool
        try:
            roots = {drawing_root(obj) for obj in FreeCADGui.Selection.getSelection()}
            roots.discard(None)
            if len(roots) > 1:
                raise ValueError("Select one DXFCAD drawing before drawing lines.")
            show_line_tool(next(iter(roots)) if roots else None)
        except Exception as error:
            FreeCAD.Console.PrintError(f"DXFCAD: {error}\n")


class DrawCircleCommand:
    def GetResources(self):
        return {"MenuText": "Draw Circles...", "ToolTip": "Draw native DXF circles with snapping and undo"}

    def IsActive(self):
        return True

    def Activated(self):
        import FreeCADGui
        from dxfcad_edit import drawing_root
        from dxfcad_circle_ui import show_circle_tool
        try:
            roots = {drawing_root(obj) for obj in FreeCADGui.Selection.getSelection()}
            roots.discard(None)
            if len(roots) > 1:
                raise ValueError("Select one DXFCAD drawing before drawing circles.")
            show_circle_tool(next(iter(roots)) if roots else None)
        except Exception as error:
            FreeCAD.Console.PrintError(f"DXFCAD: {error}\n")


class DrawArcCommand(DrawCircleCommand):
    def GetResources(self):
        return {"MenuText":"Draw Arcs...","ToolTip":"Draw native DXF arcs through three snapped points or from center and angles"}

    def Activated(self):
        import FreeCADGui
        from dxfcad_edit import drawing_root
        from dxfcad_arc_ui import show_arc_tool
        try:
            roots={drawing_root(obj) for obj in FreeCADGui.Selection.getSelection()}
            roots.discard(None)
            if len(roots)>1:
                raise ValueError("Select one DXFCAD drawing before drawing arcs.")
            show_arc_tool(next(iter(roots)) if roots else None)
        except Exception as error:
            FreeCAD.Console.PrintError(f"DXFCAD: {error}\n")


class MoveCommand:
    def GetResources(self):
        return {"MenuText": "Move Objects...", "ToolTip": "Move selected native DXF objects with snapping and undo"}

    def IsActive(self):
        import FreeCADGui
        selected = FreeCADGui.Selection.getSelection()
        return bool(selected) and all("DXFData" in obj.PropertiesList for obj in selected)

    def Activated(self):
        from dxfcad_move_ui import show_move_tool
        try:
            show_move_tool()
        except Exception as error:
            FreeCAD.Console.PrintError(f"DXFCAD: {error}\n")


class CopyCommand(MoveCommand):
    def GetResources(self):
        return {"MenuText": "Copy Objects...", "ToolTip": "Copy selected native DXF objects with snapping and undo"}

    def Activated(self):
        from dxfcad_move_ui import show_copy_tool
        try:
            show_copy_tool()
        except Exception as error:
            FreeCAD.Console.PrintError(f"DXFCAD: {error}\n")


class RotateCommand(MoveCommand):
    def GetResources(self):
        return {"MenuText": "Rotate Objects...", "ToolTip": "Rotate native DXF objects around a center"}

    def Activated(self):
        from dxfcad_transform_ui import show_transform_tool
        try:
            show_transform_tool()
        except Exception as error:
            FreeCAD.Console.PrintError(f"DXFCAD: {error}\n")


class ScaleCommand(MoveCommand):
    def GetResources(self):
        return {"MenuText": "Scale Objects...", "ToolTip": "Uniformly scale native DXF objects about a center"}

    def Activated(self):
        from dxfcad_transform_ui import show_transform_tool, ScaleTaskPanel
        try:
            show_transform_tool(panel_type=ScaleTaskPanel)
        except Exception as error:
            FreeCAD.Console.PrintError(f"DXFCAD: {error}\n")


class MirrorCommand(MoveCommand):
    def GetResources(self):
        return {"MenuText": "Mirror Objects...", "ToolTip": "Reflect native DXF objects across a line"}

    def Activated(self):
        from dxfcad_transform_ui import show_transform_tool, MirrorTaskPanel
        try:
            show_transform_tool(panel_type=MirrorTaskPanel)
        except Exception as error:
            FreeCAD.Console.PrintError(f"DXFCAD: {error}\n")


class RectangleCommand(DrawLineCommand):
    def GetResources(self):
        return {"MenuText": "Draw Rectangles...", "ToolTip": "Create a closed native rectangle from two corners"}

    def Activated(self):
        import FreeCADGui
        from dxfcad_edit import drawing_root
        from dxfcad_rectangle_ui import show_rectangle_tool
        try:
            roots = {drawing_root(obj) for obj in FreeCADGui.Selection.getSelection()}
            roots.discard(None)
            if len(roots)>1:
                raise ValueError("Select one destination drawing.")
            show_rectangle_tool(next(iter(roots)) if roots else None)
        except Exception as error:
            FreeCAD.Console.PrintError(f"DXFCAD: {error}\n")


class DraftingCommand(MoveCommand):
    panel_name = "TrimTaskPanel"
    menu_text = "Trim Objects..."
    tooltip = "Trim a line, arc or open polyline endpoint to a finite boundary"

    def GetResources(self):
        return {"MenuText": self.menu_text, "ToolTip": self.tooltip}

    def Activated(self):
        import dxfcad_drafting_ui
        try:
            dxfcad_drafting_ui.show_drafting_tool(getattr(dxfcad_drafting_ui,self.panel_name))
        except Exception as error:
            FreeCAD.Console.PrintError(f"DXFCAD: {error}\n")


class ExtendCommand(DraftingCommand):
    panel_name = "ExtendTaskPanel"
    menu_text = "Extend Objects..."
    tooltip = "Extend a line, arc or open polyline endpoint to a finite boundary"


class ContourCommand(MoveCommand):
    def __init__(self,operation):
        self.operation=operation

    def GetResources(self):
        labels={"join":"Join Contours...","split":"Split Contour...","break":"Break Contour...",
                "close":"Close Polyline...","trim":"Trim Interior Section..."}
        return {"MenuText":labels[self.operation],"ToolTip":labels[self.operation].rstrip(".")}

    def Activated(self):
        from dxfcad_contour_ui import show_contour_tool
        try:
            show_contour_tool(self.operation)
        except Exception as error:
            FreeCAD.Console.PrintError(f"DXFCAD: {error}\n")


class OffsetCommand(DraftingCommand):
    panel_name = "OffsetTaskPanel"
    menu_text = "Offset Objects..."
    tooltip = "Offset lines, arcs, circles and straight or curved polylines"


class FilletCommand(DraftingCommand):
    panel_name = "FilletTaskPanel"
    menu_text = "Fillet Polyline Corner..."
    tooltip = "Round one polyline corner with a native bulged arc"


class FilletLinesCommand(DraftingCommand):
    panel_name = "LineFilletTaskPanel"
    menu_text = "Fillet Lines..."
    tooltip = "Trim or extend two native lines and join them with a tangent arc"


class ChamferCommand(DraftingCommand):
    panel_name = "ChamferTaskPanel"
    menu_text = "Chamfer Polyline Corner..."
    tooltip = "Bevel one polyline corner with two distances"


class ChamferLinesCommand(DraftingCommand):
    panel_name = "LineChamferTaskPanel"
    menu_text = "Chamfer Lines..."
    tooltip = "Trim or extend two native lines and join them with a bevel"


class CreateTextCommand(DrawLineCommand):
    def GetResources(self):
        return {"MenuText": "Create Text...", "ToolTip": "Create native TEXT or MTEXT"}

    def Activated(self):
        import FreeCADGui
        from dxfcad_edit import drawing_root
        from dxfcad_text_ui import show_text_tool
        try:
            roots = {drawing_root(obj) for obj in FreeCADGui.Selection.getSelection()}
            roots.discard(None)
            if len(roots)>1:
                raise ValueError("Select one destination drawing.")
            show_text_tool(root=next(iter(roots)) if roots else None)
        except Exception as error:
            FreeCAD.Console.PrintError(f"DXFCAD: {error}\n")


class EditTextCommand(MoveCommand):
    def GetResources(self):
        return {"MenuText": "Edit Text...", "ToolTip": "Edit native text content, height, and multiline width"}

    def IsActive(self):
        import FreeCADGui
        objects = FreeCADGui.Selection.getSelection()
        return len(objects)==1 and getattr(objects[0],"DXFType","") in ("TEXT","MTEXT")

    def Activated(self):
        import FreeCADGui
        from dxfcad_text_ui import show_text_tool
        try:
            show_text_tool(FreeCADGui.Selection.getSelection()[0])
        except Exception as error:
            FreeCAD.Console.PrintError(f"DXFCAD: {error}\n")


class PropertiesCommand(DraftingCommand):
    panel_name = "PropertiesTaskPanel"
    menu_text = "DXF Properties..."
    tooltip = "Assign layers, ACI colors, visibility and layer settings"


class CreatePolylineCommand:
    def GetResources(self):
        return {"MenuText": "Create LWPolyline...", "ToolTip": "Create a native DXF polyline"}

    def IsActive(self):
        return True

    def Activated(self):
        import FreeCADGui
        from dxfcad_edit import drawing_root
        from dxfcad_polyline_ui import show_editor

        try:
            selected = FreeCADGui.Selection.getSelection()
            root = drawing_root(selected[0]) if len(selected) == 1 else None
            show_editor(root=root)
        except Exception as error:
            FreeCAD.Console.PrintError(f"DXFCAD: {error}\n")


class EditPolylineCommand:
    def GetResources(self):
        return {"MenuText": "Edit LWPolyline...", "ToolTip": "Edit vertices, bulges, widths, and transform native coordinates"}

    def IsActive(self):
        import FreeCADGui
        selected = FreeCADGui.Selection.getSelection()
        return len(selected) == 1 and getattr(selected[0], "DXFType", "") == "LWPOLYLINE"

    def Activated(self):
        import FreeCADGui
        from dxfcad_polyline_ui import show_editor

        try:
            show_editor(FreeCADGui.Selection.getSelection()[0])
        except Exception as error:
            FreeCAD.Console.PrintError(f"DXFCAD: {error}\n")


class SavedViewsCommand:
    def GetResources(self):
        return {"MenuText": "Saved DXF Views...", "ToolTip": "Inspect saved layout viewports and open supported model views"}

    def IsActive(self):
        return FreeCAD.ActiveDocument is not None

    def Activated(self):
        import FreeCADGui
        from PySide import QtWidgets
        from dxfcad_layout_ui import show_saved_views
        try:
            show_saved_views()
        except Exception as error:
            QtWidgets.QMessageBox.information(FreeCADGui.getMainWindow(), "Saved DXF Views", str(error))


class ExportDXFCommand:
    def GetResources(self):
        return {"MenuText": "Export Native DXF...", "ToolTip": "Export a whole drawing, preserving unedited source records"}

    def IsActive(self):
        return FreeCAD.ActiveDocument is not None

    def Activated(self):
        import FreeCADGui
        from PySide import QtWidgets
        from dxfcad_edit import drawing_root, drawing_roots
        from dxfcad_export import export_dxf

        try:
            document = FreeCAD.ActiveDocument
            selected_roots = {drawing_root(obj) for obj in FreeCADGui.Selection.getSelection()}
            selected_roots.discard(None)
            if len(selected_roots) == 1:
                root = selected_roots.pop()
            elif not selected_roots and len(drawing_roots(document)) == 1:
                root = drawing_roots(document)[0]
            else:
                raise ValueError("Select one DXFCAD drawing to export.")
            filename, _ = QtWidgets.QFileDialog.getSaveFileName(
                FreeCADGui.getMainWindow(), "Export Native DXF", "", "DXF drawings (*.dxf)")
            if filename:
                if not filename.lower().endswith(".dxf"):
                    filename += ".dxf"
                export_dxf(filename, document, root)
                FreeCAD.Console.PrintMessage(f"DXFCAD exported {filename}\n")
        except Exception as error:
            FreeCAD.Console.PrintError(f"DXFCAD export failed: {error}\n")
            QtWidgets.QMessageBox.critical(FreeCADGui.getMainWindow(), "DXFCAD export", str(error))
