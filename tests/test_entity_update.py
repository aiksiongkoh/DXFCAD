"""Validated native record updates: preview, stale state, export and rollback."""
import FreeCAD
import Part
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

module_directory = Path(sys.argv.pop(1)).resolve()
sys.path.insert(0, str(module_directory))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import DXFCAD
assert Path(DXFCAD.__file__).parent == module_directory
from dxfcad_edit import create_text, create_line, apply_entity_update, apply_records
from dxfcad_import import initialize_population, default_text_font


class EntityUpdateTests(unittest.TestCase):
    def setUp(self):
        self.doc = FreeCAD.newDocument("EntityUpdates")
        self.text = create_text(self.doc, "Before", 2, DXFCAD.Vector3D())
        self.root = self.text.DXFSourceRoot
        self.before = self.text.DXFData
        self.after = DXFCAD.edit_text(self.before, "After", 3, 0)

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)

    def plan(self):
        return DXFCAD.prepare_entity_update([self.text], [self.after])

    def test_preview_apply_export_undo_redo_and_one_shot(self):
        snapshot = self.root.DXFSourceData
        group = self.root.Group[0]
        plan = self.plan()
        self.assertEqual(self.text.DXFData, self.before)
        self.assertEqual(plan.preview_records, [(self.text, self.after)])
        expected = plan.drawing_bytes[self.root.Name]
        self.assertEqual(apply_entity_update(plan, "Edit text"), [self.text])
        self.assertEqual(DXFCAD.current_drawing_bytes(self.root), expected)
        self.assertEqual(self.root.DXFSourceData, snapshot)
        self.assertIn(self.text, group.Group)
        self.assertEqual(self.text.DXFSourceIndex, -1)
        self.assertFalse(self.text.Shape.isNull())
        with self.assertRaises(ValueError): plan.apply()
        self.doc.undo(); self.doc.recompute()
        self.assertEqual(self.text.DXFData, self.before)
        self.doc.redo(); self.doc.recompute()
        self.assertEqual(self.text.DXFData, self.after)

    def test_invalid_proposals_never_mutate(self):
        bad = ["0\nCIRCLE\n40\n-1\n", self.after + self.after,
               self.after.replace("0\nTEXT\n", "0\nTEXT\n5\nAB\n", 1),
               DXFCAD.entity_properties(self.after, "Other", 1, False),
               DXFCAD.create_line(DXFCAD.Vector3D(), DXFCAD.Vector3D(1, 0, 0))]
        for raw in bad:
            with self.assertRaises(ValueError): DXFCAD.prepare_entity_update([self.text], [raw])
            self.assertEqual(self.text.DXFData, self.before)
        for objects, records in (([], []), ([self.text], []),
                                 ([self.text, self.text], [self.after, self.after])):
            with self.assertRaises(ValueError): DXFCAD.prepare_entity_update(objects, records)

    def test_stale_selected_font_index_ownership_and_placement(self):
        for prop, value in (("DXFData", self.after), ("TextFont", "changed.ttf"),
                            ("DXFSourceIndex", 10), ("DXFSourceRoot", None),
                            ("Placement", FreeCAD.Placement(FreeCAD.Vector(1, 0, 0), FreeCAD.Rotation()))):
            plan = self.plan()
            original = getattr(self.text, prop)
            setattr(self.text, prop, value)
            with self.assertRaises(ValueError): plan.apply()
            setattr(self.text, prop, original)
            self.doc.recompute()

    def test_stale_source_and_unselected_record(self):
        line = create_line(self.doc, DXFCAD.Vector3D(), DXFCAD.Vector3D(2, 0, 0), self.root)
        plan = self.plan()
        line.DXFData = DXFCAD.offset_entity(line.DXFData, 1)
        with self.assertRaises(ValueError): plan.apply()
        self.assertEqual(self.text.DXFData, self.before)
        plan = self.plan()
        DXFCAD.ensure_layer(self.root, "NewLayer", 3, True)
        with self.assertRaises(ValueError): plan.apply()
        self.assertEqual(self.text.DXFData, self.before)

    def test_multiple_drawings_one_document_and_foreign_document_rejection(self):
        root = DXFCAD.create_drawing(self.doc, DXFCAD.DXFDocument.empty(4))
        second = create_text(self.doc, "Second", 1, DXFCAD.Vector3D(), root=root)
        records = [self.after, DXFCAD.edit_text(second.DXFData, "Changed", 2)]
        plan = DXFCAD.prepare_entity_update([self.text, second], records)
        expected = dict(plan.drawing_bytes)
        apply_entity_update(plan, "Edit two drawings")
        for drawing in (self.root, root):
            self.assertEqual(DXFCAD.current_drawing_bytes(drawing), expected[drawing.Name])
        foreign = FreeCAD.newDocument("Foreign")
        other = create_text(foreign, "Foreign", 1, DXFCAD.Vector3D())
        with self.assertRaises(ValueError):
            DXFCAD.prepare_entity_update([self.text, other], [self.after, other.DXFData])

    def test_drawing_and_deleted_object_changes_reject_before_mutation(self):
        plan = self.plan()
        create_line(self.doc, DXFCAD.Vector3D(), DXFCAD.Vector3D(2, 0, 0), self.root)
        with self.assertRaises(ValueError): plan.apply()
        self.assertEqual(self.text.DXFData, self.before)
        plan = self.plan()
        self.doc.removeObject(self.text.Name)
        with self.assertRaises((ValueError, RuntimeError)): plan.apply()

    def test_complete_batch_validation_and_display_failure_rollback(self):
        second = create_text(self.doc, "Second", 1, DXFCAD.Vector3D(), root=self.root)
        old = second.DXFData
        with self.assertRaises(ValueError):
            DXFCAD.prepare_entity_update([self.text, second], [self.after, "0\nCIRCLE\n40\n-1\n"])
        self.assertEqual(self.text.DXFData, self.before)
        with patch("dxfcad_edit._check_display", side_effect=ValueError("display failure")):
            with self.assertRaisesRegex(ValueError, "display failure"):
                apply_records([(self.text, self.after), (second, DXFCAD.edit_text(old, "Changed", 2))], "Edit")
        self.assertEqual(self.text.DXFData, self.before)
        self.assertEqual(second.DXFData, old)

    def test_imported_identity_unknown_data_and_opaque_records_preserved(self):
        raw = "0\nSECTION\n2\nENTITIES\n0\nTEXT\n5\nAB\n330\nFF\n1\nLabel\n40\n2\n10\n0\n20\n0\n1001\nTEST\n1000\nretain\n0\nSPLINE\n999\nopaque\n0\nENDSEC\n0\nEOF\n"
        population = DXFCAD.populate_drawing(self.doc, DXFCAD.DXFDocument.parse(raw), font_file=default_text_font())
        initialize_population(population)
        obj = population.entities[0]
        updated = DXFCAD.edit_text(obj.DXFData, "Updated", 3)
        plan = DXFCAD.prepare_entity_update([obj], [updated])
        expected = plan.drawing_bytes[obj.DXFSourceRoot.Name]
        apply_entity_update(plan, "Edit imported text")
        self.assertEqual(obj.DXFSourceIndex, 0)
        self.assertIn("1000\nretain", obj.DXFData)
        self.assertIn(b"opaque", expected)
        self.assertEqual(DXFCAD.current_drawing_bytes(obj.DXFSourceRoot), expected)


unittest.main(argv=[sys.argv[0]])
