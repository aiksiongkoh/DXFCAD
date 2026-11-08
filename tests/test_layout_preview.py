"""Synthetic sheet rendering, native-data roundtrip and offscreen Qt presentation."""
import FreeCAD
import Part

import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

module_directory = Path(sys.argv.pop(1)).resolve()
sys.path.insert(0, str(module_directory))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import DXFCAD
from dxfcad_import import default_text_font, import_dxf
from dxfcad_edit import drawing_roots
from dxfcad_export import export_dxf

assert Path(DXFCAD.__file__).parent == module_directory

VIEWPORT = ("0\nVIEWPORT\n5\nA\n67\n1\n410\nSheet 1\n69\n2\n68\n2\n"
            "10\n50\n20\n40\n40\n20\n41\n10\n45\n10\n")
LINE = "0\nLINE\n5\nB\n62\n1\n10\n-100\n20\n0\n11\n100\n21\n0\n"
PAPER = "0\nLINE\n5\nC\n67\n1\n410\nSheet 1\n62\n3\n10\n1\n20\n1\n11\n4\n21\n5\n"


def source(model=LINE, paper=PAPER, viewport=VIEWPORT, tables=""):
    return ("0\nSECTION\n2\nTABLES\n" + tables + "0\nENDSEC\n"
            "0\nSECTION\n2\nOBJECTS\n0\nLAYOUT\n5\nD\n100\nAcDbLayout\n"
            "1\nSheet 1\n71\n1\n10\n0\n20\n0\n11\n120\n21\n80\n"
            "0\nLAYOUT\n5\nE\n100\nAcDbLayout\n1\nEmpty sheet\n71\n2\n0\nENDSEC\n"
            "0\nSECTION\n2\nENTITIES\n" + model + viewport + paper + "0\nENDSEC\n0\nEOF\n")


class LayoutPreviewTests(unittest.TestCase):
    def tearDown(self):
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)

    def preview(self, raw, name="Sheet 1", tolerance=0.02):
        return DXFCAD.preview_layout(DXFCAD.Drawing.parse(raw), name, default_text_font(), tolerance)

    def test_analytic_line_clipping_and_native_paper_coordinates(self):
        preview = self.preview(source())
        self.assertFalse(preview.warnings)
        self.assertEqual(len(preview.viewports), 1)
        self.assertEqual(len(preview.paths), 2)
        model, paper = preview.paths
        for point, expected in zip(model.contours[0], [(40, 40), (60, 40)]):
            self.assertAlmostEqual(point.x, expected[0], places=9)
            self.assertAlmostEqual(point.y, expected[1], places=9)
        self.assertEqual([(p.x, p.y) for p in paper.contours[0]], [(1, 1), (4, 5)])
        self.assertEqual(preview.limits.max_x, 120)
        self.assertFalse(self.preview(source(), "Empty sheet").paths)
        with self.assertRaises(ValueError):
            self.preview(source(), "Missing")
        for tolerance in (0, -1, float("nan")):
            with self.assertRaises(ValueError):
                self.preview(source(), tolerance=tolerance)

    def test_rotated_scaled_view_and_multiple_viewports(self):
        second = VIEWPORT.replace("5\nA\n", "5\nF\n").replace("10\n50\n", "10\n100\n")
        second = second.replace("45\n10\n", "45\n20\n") + "51\n90\n"
        preview = self.preview(source(viewport=VIEWPORT + second, paper=""))
        self.assertEqual(len(preview.viewports), 2)
        path = preview.paths[1].contours[0]
        for p in path:
            self.assertAlmostEqual(p.x, 100, places=9)
        self.assertAlmostEqual(path[0].y, 35)
        self.assertAlmostEqual(path[1].y, 45)

    def test_frozen_and_globally_hidden_layers_and_off_viewports(self):
        tables = "0\nLAYER\n5\nAA\n2\nFrozen\n62\n7\n0\nLAYER\n2\nHidden\n62\n-3\n"
        frozen = LINE.replace("5\nB\n", "5\nF1\n8\nFrozen\n")
        hidden = LINE.replace("5\nB\n", "5\nF2\n8\nHidden\n")
        preview = self.preview(source(model=LINE + frozen + hidden, viewport=VIEWPORT + "331\nAA\n", tables=tables))
        self.assertEqual(len(preview.paths), 2)
        off = self.preview(source(viewport=VIEWPORT.replace("68\n2\n", "68\n0\n")))
        self.assertFalse(off.viewports)
        self.assertEqual(len(off.paths), 1)  # Paper geometry remains visible.
        self.assertTrue(any("Viewport is off" in warning for warning in off.warnings))
        perspective = self.preview(source(viewport=VIEWPORT + "90\n1\n"))
        self.assertFalse(perspective.viewports)
        self.assertTrue(any("Perspective" in warning for warning in perspective.warnings))
        border_off = self.preview(source(viewport=VIEWPORT + "8\nHidden\n", tables=tables))
        self.assertEqual(len(border_off.viewports), 1)
        self.assertEqual(len(border_off.paths), 2)

    def test_width_faces_and_curves_use_bounded_paper_coordinates(self):
        wide = ("0\nLWPOLYLINE\n90\n2\n43\n2\n10\n-100\n20\n2\n10\n100\n20\n2\n")
        circle = "0\nCIRCLE\n10\n0\n20\n0\n40\n3\n"
        preview = self.preview(source(model=wide + circle, paper=""))
        self.assertFalse(preview.warnings)
        self.assertTrue(any(path.filled for path in preview.paths))
        for path in preview.paths:
            for contour in path.contours:
                for p in contour:
                    self.assertTrue(40 - 1e-9 <= p.x <= 60 + 1e-9)
                    self.assertTrue(35 - 1e-9 <= p.y <= 45 + 1e-9)
        face = next(path for path in preview.paths if path.filled)
        points = face.contours[0]
        area = abs(sum(a.x*b.y - b.x*a.y for a, b in zip(points, points[1:] + points[:1]))) / 2
        self.assertAlmostEqual(area, 40, places=7)
        coarse = self.preview(source(model=circle, paper=""), tolerance=0.1)
        fine = self.preview(source(model=circle, paper=""), tolerance=0.001)
        self.assertGreater(sum(len(p.contours) for p in fine.paths), sum(len(p.contours) for p in coarse.paths))

    def test_supported_sheet_text_and_spline(self):
        text = "0\nTEXT\n67\n1\n410\nSheet 1\n10\n10\n20\n10\n40\n2\n1\nSheet annotation\n"
        spline = ("0\nSPLINE\n67\n1\n410\nSheet 1\n70\n8\n71\n2\n72\n6\n73\n3\n"
                  "40\n0\n40\n0\n40\n0\n40\n1\n40\n1\n40\n1\n"
                  "10\n0\n20\n0\n30\n0\n10\n5\n20\n5\n30\n0\n10\n10\n20\n0\n30\n0\n")
        preview = self.preview(source(model="", paper=text + spline, viewport=""))
        self.assertFalse(preview.warnings)
        self.assertGreater(len(preview.paths), 2)

    def test_preview_restore_and_export_keep_source_records(self):
        raw = source()
        with tempfile.TemporaryDirectory(dir=module_directory) as folder:
            path = Path(folder) / "layout.dxf"
            path.write_text(raw, encoding="utf-8")
            doc = import_dxf(path)
            root = drawing_roots(doc)[0]
            self.assertEqual(len([o for o in doc.Objects if hasattr(o, "DXFData")]), 1)
            before = DXFCAD.current_drawing_bytes(root)
            DXFCAD.preview_layout(DXFCAD.current_drawing(root), "Sheet 1", default_text_font())
            self.assertEqual(DXFCAD.current_drawing_bytes(root), before)
            saved = Path(folder) / "layout.FCStd"
            doc.saveAs(str(saved))
            FreeCAD.closeDocument(doc.Name)
            path.unlink()
            doc = FreeCAD.openDocument(str(saved))
            root = drawing_roots(doc)[0]
            self.assertEqual(len(DXFCAD.current_drawing(root).layouts), 2)
            self.assertTrue(DXFCAD.preview_layout(DXFCAD.current_drawing(root), "Sheet 1", default_text_font()).paths)
            output = Path(folder) / "exported.dxf"
            export_dxf(output, doc, root)
            self.assertEqual(output.read_bytes(), before)

    def test_offscreen_dialog_renders_and_switches_layouts(self):
        from PySide import QtWidgets, QtGui

        app = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        # The standalone offscreen process has no FreeCAD main-window font.
        font_id = QtGui.QFontDatabase.addApplicationFont(default_text_font())
        families = QtGui.QFontDatabase.applicationFontFamilies(font_id)
        if families:
            app.setFont(QtGui.QFont(families[0], 10))
        with patch.dict(sys.modules, {"FreeCADGui": SimpleNamespace(getMainWindow=lambda: None)}):
            from dxfcad_paper_ui import PaperLayoutDialog
        doc = FreeCAD.newDocument("LayoutDialog")
        circle = "0\nCIRCLE\n62\n5\n10\n0\n20\n0\n40\n3\n"
        wide = "0\nLWPOLYLINE\n62\n2\n90\n2\n43\n1\n10\n-2\n20\n-1\n10\n2\n20\n-1\n"
        border = ("0\nLWPOLYLINE\n67\n1\n410\nSheet 1\n90\n4\n70\n1\n"
                  "10\n2\n20\n2\n10\n118\n20\n2\n10\n118\n20\n78\n10\n2\n20\n78\n")
        title = ("0\nTEXT\n67\n1\n410\nSheet 1\n10\n5\n20\n70\n40\n4\n1\nDXFCAD / Paper layout\n"
                 "0\nTEXT\n67\n1\n410\nSheet 1\n10\n5\n20\n6\n40\n2\n1\nSheet 1 - two clipped model views\n")
        second = VIEWPORT.replace("5\nA\n", "5\nF\n").replace("10\n50\n", "10\n90\n")
        second = second.replace("45\n10\n", "45\n20\n") + "51\n90\n"
        raw = source(model=LINE + circle + wide, paper=border + title, viewport=VIEWPORT + second)
        population = DXFCAD.populate_drawing(doc, DXFCAD.Drawing.parse(raw))
        before = DXFCAD.current_drawing_bytes(population.root)
        dialog = PaperLayoutDialog(population.root)
        dialog.show()
        app.processEvents()
        app.processEvents()
        self.assertTrue(dialog.preview.paths, dialog.message.text())
        self.assertIn("2 viewport", dialog.message.text())
        self.assertGreater(len(dialog.scene.items()), 2)
        dialog.grab().save(str(module_directory / "paper-layout-preview.png"))
        dialog.sheets.setCurrentIndex(1)
        self.assertIn("0 viewport", dialog.message.text())
        self.assertFalse(dialog.preview.paths)
        dialog.reject()
        self.assertEqual(DXFCAD.current_drawing_bytes(population.root), before)


if __name__ == "__main__":
    unittest.main()
