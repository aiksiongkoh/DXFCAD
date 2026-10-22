"""Related-copy plans preserve metadata and validate complete additions atomically."""
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
from dxfcad_edit import apply_batch_plan, create_related, create_line, create_polyline, create_text

DRAWING = ("0\nSECTION\n2\nENTITIES\n0\nLINE\n5\nAB\n330\nFF\n10\n0\n20\n0\n11\n10\n21\n0\n"
           "1001\nTEST\n1000\nretain me\n0\nCIRCLE\n5\nBC\n10\n20\n20\n0\n40\n3\n"
           "0\nSPLINE\n999\nopaque\n0\nENDSEC\n0\nEOF\n")


class EntityCopyTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(dir=module_directory)
        path = Path(self.folder.name) / "copy.dxf"
        path.write_text(DRAWING)
        self.doc = import_dxf(path)
        self.objects = [o for o in self.doc.Objects if hasattr(o, "DXFData")]
        self.root = self.objects[0].DXFSourceRoot

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()): FreeCAD.closeDocument(name)
        self.folder.cleanup()

    def records(self):
        return [DXFCAD.offset_entity(o.DXFData, 1) for o in self.objects]

    def test_preview_apply_identity_export_and_undo_redo(self):
        original = [o.DXFData for o in self.objects]
        source = self.root.DXFSourceData
        plan = DXFCAD.prepare_entity_copy(self.objects, self.records())
        self.assertIsInstance(plan, DXFCAD.EntityCopyPlan)
        self.assertTrue(plan.copies)
        self.assertEqual([o.DXFData for o in self.objects], original)
        previews = [raw for obj, raw in plan.preview_records]
        for raw in previews:
            self.assertFalse(DXFCAD.parse_entity(raw).handle)
            self.assertNotIn("\n330\n", raw)
        expected = plan.drawing_bytes[self.root.Name]
        copies = apply_batch_plan(plan, "Offset copies")
        self.assertEqual([o.DXFData for o in copies], previews)
        self.assertEqual([o.DXFData for o in self.objects], original)
        self.assertEqual(self.root.DXFSourceData, source)
        self.assertTrue(all(o.DXFSourceRoot == self.root and o.DXFSourceIndex == -1 for o in copies))
        self.assertTrue(all(o in self.root.Group[0].Group for o in copies))
        self.assertIn("retain me", copies[0].DXFData)
        self.assertIn(b"opaque", expected)
        self.assertEqual(DXFCAD.current_drawing_bytes(self.root), expected)
        with self.assertRaises(ValueError): plan.apply()
        names = [o.Name for o in copies]
        self.doc.undo(); self.doc.recompute()
        self.assertTrue(all(self.doc.getObject(name) is None for name in names))
        self.doc.redo(); self.doc.recompute()
        self.assertEqual(DXFCAD.current_drawing_bytes(self.root), expected)

    def test_invalid_complete_selection_rejected_without_mutation(self):
        names = [o.Name for o in self.doc.Objects]
        records = self.records()
        cases = [([], []), (self.objects, records[:1]),
                 ([self.objects[0]] * 2, [records[0]] * 2),
                 (self.objects, [records[0], "0\nCIRCLE\n40\n-1\n"]),
                 ([self.objects[0]], [records[0] + records[0]]),
                 ([self.objects[0]], [records[1]]),
                 ([self.objects[0]], [DXFCAD.entity_properties(records[0], "Other", -1, True)])]
        for objects, raw in cases:
            with self.assertRaises(ValueError): DXFCAD.prepare_entity_copy(objects, raw)
            self.assertEqual([o.Name for o in self.doc.Objects], names)

    def test_stale_selected_unselected_source_ownership_and_placement(self):
        for obj in self.objects:
            plan = DXFCAD.prepare_entity_copy([self.objects[0]], [self.records()[0]])
            raw = obj.DXFData
            obj.DXFData = DXFCAD.offset_entity(raw, 1)
            with self.assertRaises(ValueError): plan.apply()
            obj.DXFData = raw
        for prop, value in (("DXFSourceRoot", None), ("DXFSourceIndex", -1),
                            ("Placement", FreeCAD.Placement(FreeCAD.Vector(1, 0, 0), FreeCAD.Rotation()))):
            plan = DXFCAD.prepare_entity_copy(self.objects, self.records())
            old = getattr(self.objects[0], prop)
            setattr(self.objects[0], prop, value)
            with self.assertRaises(ValueError): plan.apply()
            setattr(self.objects[0], prop, old)
        plan = DXFCAD.prepare_entity_copy(self.objects, self.records())
        DXFCAD.ensure_layer(self.root, "New", 3, True)
        with self.assertRaises(ValueError): plan.apply()

    def test_multiple_drawings_and_foreign_document_rejection(self):
        root = DXFCAD.create_drawing(self.doc, DXFCAD.DXFDocument.empty(4))
        line = create_line(self.doc, DXFCAD.Vector3D(), DXFCAD.Vector3D(2, 0, 0), root)
        objects = [self.objects[0], line]
        plan = DXFCAD.prepare_entity_copy(objects, [DXFCAD.offset_entity(o.DXFData, 1) for o in objects])
        expected = dict(plan.drawing_bytes)
        copies = apply_batch_plan(plan, "Copy two drawings")
        self.assertEqual([o.DXFSourceRoot for o in copies], [self.root, root])
        for drawing in (self.root, root):
            self.assertEqual(DXFCAD.current_drawing_bytes(drawing), expected[drawing.Name])
        foreign = FreeCAD.newDocument("Foreign")
        other = create_line(foreign, DXFCAD.Vector3D(), DXFCAD.Vector3D(2, 0, 0))
        with self.assertRaises(ValueError):
            DXFCAD.prepare_entity_copy([line, other], [line.DXFData, other.DXFData])

    def test_factory_and_display_failures_roll_back_all_copies(self):
        names = [o.Name for o in self.doc.Objects]
        before = DXFCAD.current_drawing_bytes(self.root)
        factory = DXFCAD.create_feature
        calls = []
        def fail_second(*args):
            calls.append(args)
            obj = factory(*args)
            if len(calls) == 2: raise RuntimeError("factory failure")
            return obj
        with patch.object(DXFCAD, "create_feature", side_effect=fail_second):
            with self.assertRaisesRegex(RuntimeError, "factory failure"):
                create_related(zip(self.objects, self.records()), "Fail copy")
        self.assertEqual([o.Name for o in self.doc.Objects], names)
        self.assertEqual(DXFCAD.current_drawing_bytes(self.root), before)
        with patch("dxfcad_edit._check_display", side_effect=ValueError("display failure")):
            with self.assertRaisesRegex(ValueError, "display failure"):
                create_related(list(zip(self.objects, self.records())), "Fail display")
        self.assertEqual([o.Name for o in self.doc.Objects], names)
        self.assertEqual(DXFCAD.current_drawing_bytes(self.root), before)

    def test_polyline_display_preferences_and_native_provenance(self):
        poly = create_polyline(self.doc, DXFCAD.LWPolyline([DXFCAD.LWPolylineVertex(), DXFCAD.LWPolylineVertex(4, 0)]), self.root)
        poly.ShowWidths = True
        raw = DXFCAD.offset_entity(poly.DXFData, 1)
        clone = create_related([(poly, raw)], "Offset polyline")[0]
        self.assertTrue(clone.ShowWidths)
        self.assertEqual(clone.DXFData, DXFCAD.parse_entity(raw).copied(DXFCAD.Vector3D()))
        self.assertFalse(clone.Shape.isNull())

    def test_detached_templates_rejected_before_creation(self):
        template = self.objects[0]
        template.DXFSourceRoot = None
        names = [o.Name for o in self.doc.Objects]
        with self.assertRaises(ValueError): DXFCAD.prepare_entity_copy([template], [template.DXFData])
        self.assertEqual([o.Name for o in self.doc.Objects], names)

    def test_text_copy_keeps_font_and_rejects_stale_font(self):
        text = create_text(self.doc, "Label", 2, DXFCAD.Vector3D(), root=self.root)
        raw = DXFCAD.edit_text(text.DXFData, "Related label", 3)
        plan = DXFCAD.prepare_entity_copy([text], [raw])
        font = text.TextFont
        text.TextFont = "changed.ttf"
        with self.assertRaises(ValueError): plan.apply()
        text.TextFont = font
        clone = create_related([(text, raw)], "Copy text")[0]
        self.assertEqual(clone.TextFont, font)
        self.assertEqual(DXFCAD.text_info(clone.DXFData)[0], "Related label")
        self.assertFalse(clone.Shape.isNull())


unittest.main(argv=[sys.argv[0]])
