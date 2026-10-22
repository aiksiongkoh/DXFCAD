"""C++ text edit sessions retain opening state and produce validated update plans."""
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
from dxfcad_edit import apply_entity_update

DRAWING = ("0\nSECTION\n2\nENTITIES\n0\nTEXT\n5\nAB\n330\nFF\n1\nLabel\n40\n2\n10\n0\n20\n0\n"
           "1001\nTEST\n1000\nretain me\n0\nMTEXT\n5\nAC\n1\nParagraph\n40\n2\n41\n10\n10\n0\n20\n5\n"
           "0\nLINE\n5\nAD\n10\n0\n20\n10\n11\n10\n21\n10\n"
           "0\nSPLINE\n999\nopaque\n0\nENDSEC\n0\nEOF\n")


class TextEditSessionTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(dir=module_directory)
        path = Path(self.folder.name) / "text.dxf"
        path.write_text(DRAWING)
        self.doc = import_dxf(path)
        self.text, self.mtext, self.line = [o for o in self.doc.Objects if hasattr(o, "DXFData")]
        self.root = self.text.DXFSourceRoot

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()): FreeCAD.closeDocument(name)
        self.folder.cleanup()

    def test_text_and_mtext_preview_identity_export_undo_and_repeat(self):
        source = self.root.DXFSourceData
        for obj, content, width in ((self.text, "Updated label", 0),
                                     (self.mtext, "First\nSecond {literal} \\P", 8)):
            raw = obj.DXFData
            slot = obj.DXFSourceIndex
            session = DXFCAD.begin_text_edit(obj)
            self.assertIsInstance(session, DXFCAD.TextEditSession)
            self.assertEqual(session.object, obj)
            self.assertEqual(session.original_data, raw)
            self.assertEqual(session.text_info, DXFCAD.text_info(raw))
            plan = session.prepare(content, 3, width)
            self.assertEqual(obj.DXFData, raw)
            preview = plan.preview_records[0][1]
            expected = plan.drawing_bytes[self.root.Name]
            apply_entity_update(plan, "Edit text")
            self.assertEqual(obj.DXFData, preview)
            self.assertEqual(DXFCAD.text_info(preview), (content, 3, width))
            self.assertEqual(obj.DXFSourceIndex, slot)
            self.assertEqual(self.root.DXFSourceData, source)
            self.assertEqual(DXFCAD.current_drawing_bytes(self.root), expected)
            self.assertIn(b"retain me", expected)
            self.assertIn(b"opaque", expected)
            self.assertFalse(obj.Shape.isNull())
            with self.assertRaises(ValueError): session.prepare("Stale", 2)
            with self.assertRaises(ValueError): plan.apply()
            self.doc.undo(); self.doc.recompute()
            self.assertEqual(obj.DXFData, raw)
            self.doc.redo(); self.doc.recompute()
            self.assertEqual(obj.DXFData, preview)
            apply_entity_update(DXFCAD.prepare_text_edit(obj, "Again", 2, width), "Edit again")
            self.assertEqual(DXFCAD.text_info(obj.DXFData)[0], "Again")

    def test_selected_record_font_context_ownership_index_placement_changes(self):
        for prop, value in (("DXFData", DXFCAD.edit_text(self.text.DXFData, "Changed", 3)),
                            ("TextFont", "changed.ttf"), ("DXFSourceRoot", None), ("DXFSourceIndex", -1),
                            ("Placement", FreeCAD.Placement(FreeCAD.Vector(1, 0, 0), FreeCAD.Rotation()))):
            session = DXFCAD.begin_text_edit(self.text)
            old = getattr(self.text, prop)
            setattr(self.text, prop, value)
            with self.assertRaises(ValueError): session.prepare("Proposed", 3)
            setattr(self.text, prop, old)
        session = DXFCAD.begin_text_edit(self.text)
        self.text.addProperty("App::PropertyString", "DXFContext", "DXF")
        self.text.DXFContext = "changed context"
        with self.assertRaises(ValueError): session.prepare("Proposed", 3)

    def test_unselected_and_source_changes_invalidate_session_and_cached_plan(self):
        session = DXFCAD.begin_text_edit(self.text)
        plan = session.prepare("Proposed", 3)
        raw = self.line.DXFData
        self.line.DXFData = DXFCAD.offset_entity(raw, 1)
        with self.assertRaises(ValueError): session.prepare("Proposed", 3)
        with self.assertRaises(ValueError): plan.apply()
        self.line.DXFData = raw
        session = DXFCAD.begin_text_edit(self.text)
        plan = session.prepare("Proposed", 3)
        DXFCAD.ensure_layer(self.root, "New", 3, True)
        with self.assertRaises(ValueError): session.prepare("Proposed", 3)
        with self.assertRaises(ValueError): plan.apply()

    def test_invalid_inputs_types_and_placement_do_not_mutate(self):
        raw = self.text.DXFData
        session = DXFCAD.begin_text_edit(self.text)
        for content, height in (("", 2), ("Multiline\nTEXT", 2), ("Label", 0), ("Label", float("nan"))):
            with self.assertRaises(ValueError): session.prepare(content, height)
            self.assertEqual(self.text.DXFData, raw)
        session = DXFCAD.begin_text_edit(self.mtext)
        for width in (-1, float("inf")):
            with self.assertRaises(ValueError): session.prepare("Label", 2, width)
        with self.assertRaises(ValueError): DXFCAD.begin_text_edit(self.line)
        self.text.Placement = FreeCAD.Placement(FreeCAD.Vector(1, 0, 0), FreeCAD.Rotation())
        with self.assertRaises(ValueError): DXFCAD.begin_text_edit(self.text)

    def test_deleted_object_and_display_failure_rollback(self):
        session = DXFCAD.begin_text_edit(self.text)
        original = self.text.DXFData
        plan = session.prepare("Proposed", 3)
        with patch("dxfcad_edit._check_display", side_effect=ValueError("display failure")):
            with self.assertRaisesRegex(ValueError, "display failure"):
                apply_entity_update(plan, "Fail text")
        self.assertEqual(self.text.DXFData, original)
        plan = session.prepare("After rollback", 3)
        self.doc.removeObject(self.text.Name)
        with self.assertRaises((ValueError, RuntimeError)): session.prepare("Deleted", 3)
        with self.assertRaises((ValueError, RuntimeError)): plan.apply()

    def test_open_is_read_only_and_prepare_checks_export_eligibility(self):
        self.line.Placement = FreeCAD.Placement(FreeCAD.Vector(1, 0, 0), FreeCAD.Rotation())
        session = DXFCAD.begin_text_edit(self.text)
        self.assertEqual(session.text_info[0], "Label")
        with self.assertRaises(ValueError): session.prepare("Proposed", 3)


unittest.main(argv=[sys.argv[0]])
