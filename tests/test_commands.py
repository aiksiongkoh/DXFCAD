"""Check command registration and availability without starting FreeCAD's GUI."""

from pathlib import Path
import runpy
import sys
from types import ModuleType, SimpleNamespace
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]


class CommandTests(unittest.TestCase):
    def setUp(self):
        self.selection = []
        self.freecad = SimpleNamespace(ActiveDocument=None)
        self.registered = {}
        workbench_base = type("Workbench", (), {
            "appendToolbar": lambda *args: None,
            "appendMenu": lambda *args: None,
        })
        gui = SimpleNamespace(
            Selection=SimpleNamespace(getSelection=lambda: self.selection),
            Workbench=workbench_base,
            addCommand=self.registered.__setitem__,
        )
        self.commands = ModuleType("dxfcad_commands")
        modules = patch.dict(sys.modules, {
            "FreeCAD": self.freecad, "FreeCADGui": gui,
            "dxfcad_commands": self.commands,
        })
        modules.start()
        self.addCleanup(modules.stop)
        self.commands.__dict__.update(runpy.run_path(str(ROOT / "dxfcad_commands.py")))
        workbench = runpy.run_path(str(ROOT / "dxfcad_workbench.py"))
        workbench["DXFCADWorkbench"]().Initialize()

    def test_registration_and_resources(self):
        names = """CheckWorkbench ImportDXF DrawLine DrawCircle DrawArc Rectangle
            Trim Extend Join Split Break Close TrimInterior Offset Fillet FilletLines
            ChamferLines Chamfer CreateText EditText Properties Move Copy Rotate
            Scale Mirror CreateLWPolyline EditLWPolyline ExportDXF SavedViews PaperLayouts""".split()
        self.assertEqual(set(self.registered), {"DXFCAD_" + name for name in names})
        for name, command in self.registered.items():
            with self.subTest(command=name):
                self.assertIsInstance(command, self.commands.DXFCADCommand)
                resources = command.GetResources()
                self.assertTrue(resources["MenuText"])
                self.assertTrue(resources["ToolTip"])
                self.assertTrue(callable(command.Activated))
        self.assertEqual(self.registered["DXFCAD_ImportDXF"].GetResources()["MenuText"],
                         "Import DXF/DWG...")

    def test_availability_without_document(self):
        enabled = {"CheckWorkbench", "ImportDXF", "DrawLine", "DrawCircle",
                   "DrawArc", "Rectangle", "CreateText", "CreateLWPolyline"}
        for name, command in self.registered.items():
            with self.subTest(command=name):
                self.assertEqual(command.IsActive(), name.removeprefix("DXFCAD_") in enabled)

    def test_document_commands_require_document(self):
        self.freecad.ActiveDocument = object()
        for name in ("SavedViews", "ExportDXF", "PaperLayouts"):
            self.assertTrue(self.registered["DXFCAD_" + name].IsActive())
        self.assertFalse(self.registered["DXFCAD_Move"].IsActive())

    def test_selected_entity_commands(self):
        self.freecad.ActiveDocument = object()
        general = """Move Copy Rotate Scale Mirror Trim Extend Offset Fillet
            FilletLines Chamfer ChamferLines Properties Join Split Break Close
            TrimInterior""".split()
        line = SimpleNamespace(PropertiesList=["DXFData"], DXFType="LINE")
        polyline = SimpleNamespace(PropertiesList=["DXFData"], DXFType="LWPOLYLINE")
        text = SimpleNamespace(PropertiesList=["DXFData"], DXFType="TEXT")
        mtext = SimpleNamespace(PropertiesList=["DXFData"], DXFType="MTEXT")
        ordinary = SimpleNamespace(PropertiesList=[], DXFType="")
        for selection, active in (([], False), ([line], True), ([line, polyline], True),
                                  ([ordinary], False), ([line, ordinary], False)):
            self.selection = selection
            for name in general:
                with self.subTest(command=name, selection=selection):
                    self.assertEqual(self.registered["DXFCAD_" + name].IsActive(), active)
        for selection, edit_polyline, edit_text in (
                ([], False, False), ([line], False, False), ([polyline], True, False),
                ([text], False, True), ([mtext], False, True),
                ([polyline, text], False, False), ([text, mtext], False, False)):
            self.selection = selection
            with self.subTest(selection=selection):
                self.assertEqual(self.registered["DXFCAD_EditLWPolyline"].IsActive(), edit_polyline)
                self.assertEqual(self.registered["DXFCAD_EditText"].IsActive(), edit_text)


if __name__ == "__main__":
    unittest.main()
