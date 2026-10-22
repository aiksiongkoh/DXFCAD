"""Unified C++ offset planning: complete previews, both modes and stale plans."""
import FreeCAD
import Part
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

module_directory = Path(sys.argv.pop(1)).resolve()
sys.path.insert(0, str(module_directory))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import DXFCAD
assert Path(DXFCAD.__file__).parent == module_directory
from dxfcad_import import import_dxf
from dxfcad_edit import apply_batch_plan, create_line, create_text

DRAWING = ("0\nSECTION\n2\nENTITIES\n0\nLINE\n5\nAB\n330\nFF\n10\n0\n20\n0\n11\n10\n21\n0\n"
           "1001\nTEST\n1000\nretain me\n0\nCIRCLE\n5\nBC\n10\n0\n20\n10\n40\n3\n"
           "0\nLWPOLYLINE\n5\nBD\n90\n2\n70\n129\n10\n15\n20\n0\n42\n1\n91\n11\n"
           "10\n25\n20\n0\n42\n1\n91\n12\n"
           "0\nSPLINE\n999\nopaque\n0\nENDSEC\n0\nEOF\n")


class OffsetPlanTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(dir=module_directory)
        path = Path(self.folder.name) / "offset.dxf"
        path.write_text(DRAWING)
        self.doc = import_dxf(path)
        self.objects = [o for o in self.doc.Objects if hasattr(o, "DXFData")]
        self.root = self.objects[0].DXFSourceRoot

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()): FreeCAD.closeDocument(name)
        self.folder.cleanup()

    def test_copy_and_replace_preview_export_identity_undo_redo(self):
        before = [o.DXFData for o in self.objects]
        source = self.root.DXFSourceData
        for copies in (True, False):
            plan = DXFCAD.prepare_offset(self.objects, 1, keep_originals=copies)
            self.assertIsInstance(plan, DXFCAD.OffsetPlan)
            self.assertEqual(plan.copies, copies)
            self.assertEqual(list(plan.objects), self.objects)
            preview = [raw for obj, raw in plan.preview_records]
            self.assertEqual([o.DXFData for o in self.objects], before)
            expected = plan.drawing_bytes[self.root.Name]
            result = apply_batch_plan(plan, "Offset")
            self.assertEqual([o.DXFData for o in result], preview)
            self.assertTrue(all(o.Shape.isValid() and not o.Shape.isNull() for o in result))
            self.assertEqual(DXFCAD.current_drawing_bytes(self.root), expected)
            self.assertEqual(self.root.DXFSourceData, source)
            self.assertIn(b"opaque", expected)
            self.assertIn("retain me", result[0].DXFData)
            if copies:
                self.assertEqual([o.DXFData for o in self.objects], before)
                self.assertTrue(all(o.DXFSourceIndex == -1 and o.DXFSourceRoot == self.root for o in result))
                self.assertFalse(DXFCAD.parse_entity(result[0].DXFData).handle)
                self.assertNotIn("\n330\n", result[0].DXFData)
            else:
                self.assertEqual(result, self.objects)
                self.assertEqual(result[0].DXFHandle, "AB")
                self.assertEqual(result[0].DXFSourceIndex, 0)
            with self.assertRaises(ValueError): plan.apply()
            self.doc.undo(); self.doc.recompute()
            self.assertEqual([o.DXFData for o in self.objects], before)
            self.doc.redo(); self.doc.recompute()
            self.assertEqual(DXFCAD.current_drawing_bytes(self.root), expected)
            self.doc.undo(); self.doc.recompute()

    def test_invalid_distance_or_one_invalid_member_never_mutates(self):
        names = [o.Name for o in self.doc.Objects]
        before = DXFCAD.current_drawing_bytes(self.root)
        for copies in (True, False):
            for distance in (0, float("nan"), float("inf"), -4, 6):
                with self.assertRaises(ValueError): DXFCAD.prepare_offset(self.objects, distance, copies)
                self.assertEqual([o.Name for o in self.doc.Objects], names)
                self.assertEqual(DXFCAD.current_drawing_bytes(self.root), before)
        with self.assertRaises(ValueError): DXFCAD.prepare_offset([], 1)

    def test_stale_selected_unselected_source_and_placement(self):
        for copies in (True, False):
            for obj in self.objects[:2]:
                plan = DXFCAD.prepare_offset([self.objects[0]], 1, copies)
                raw = obj.DXFData
                obj.DXFData = DXFCAD.offset_entity(raw, 1)
                with self.assertRaises(ValueError): plan.apply()
                obj.DXFData = raw
            plan = DXFCAD.prepare_offset(self.objects, 1, copies)
            self.objects[0].Placement = FreeCAD.Placement(FreeCAD.Vector(1, 0, 0), FreeCAD.Rotation())
            with self.assertRaises(ValueError): plan.apply()
            self.objects[0].Placement = FreeCAD.Placement()
        plan = DXFCAD.prepare_offset(self.objects, 1)
        DXFCAD.ensure_layer(self.root, "NewLayer", 2, True)
        with self.assertRaises(ValueError): plan.apply()

    def test_multiple_drawings_duplicates_and_foreign_document(self):
        root = DXFCAD.create_drawing(self.doc, DXFCAD.DXFDocument.empty(4))
        other = create_line(self.doc, DXFCAD.Vector3D(), DXFCAD.Vector3D(2, 0, 0), root)
        for copies in (True, False):
            plan = DXFCAD.prepare_offset([self.objects[0], other, other], 1, copies)
            self.assertEqual(len(plan.objects), 2)
            expected = dict(plan.drawing_bytes)
            apply_batch_plan(plan, "Offset two drawings")
            for drawing in (self.root, root):
                self.assertEqual(DXFCAD.current_drawing_bytes(drawing), expected[drawing.Name])
            self.doc.undo(); self.doc.recompute()
        foreign = FreeCAD.newDocument("Foreign")
        line = create_line(foreign, DXFCAD.Vector3D(), DXFCAD.Vector3D(2, 0, 0))
        with self.assertRaises(ValueError): DXFCAD.prepare_offset([self.objects[0], line], 1)

    def test_creation_and_display_failures_roll_back_both_modes(self):
        before = DXFCAD.current_drawing_bytes(self.root)
        names = [o.Name for o in self.doc.Objects]
        factory = DXFCAD.create_feature
        count = []
        def fail_second(*args):
            count.append(args)
            obj = factory(*args)
            if len(count) == 2: raise RuntimeError("factory failure")
            return obj
        plan = DXFCAD.prepare_offset(self.objects, 1)
        with patch.object(DXFCAD, "create_feature", side_effect=fail_second):
            with self.assertRaisesRegex(RuntimeError, "factory failure"):
                apply_batch_plan(plan, "Fail offset")
        for copies in (True, False):
            plan = DXFCAD.prepare_offset(self.objects, 1, copies)
            with patch("dxfcad_edit._check_display", side_effect=ValueError("display failure")):
                with self.assertRaisesRegex(ValueError, "display failure"):
                    apply_batch_plan(plan, "Fail display")
            self.assertEqual([o.Name for o in self.doc.Objects], names)
            self.assertEqual(DXFCAD.current_drawing_bytes(self.root), before)

    def test_unsupported_entity_and_detached_copy_rejected(self):
        text = create_text(self.doc, "Label", 2, DXFCAD.Vector3D(), root=self.root)
        for copies in (True, False):
            with self.assertRaises(ValueError): DXFCAD.prepare_offset([self.objects[0], text], 1, copies)
        self.objects[0].DXFSourceRoot = None
        with self.assertRaises(ValueError): DXFCAD.prepare_offset([self.objects[0]], 1)


unittest.main(argv=[sys.argv[0]])
