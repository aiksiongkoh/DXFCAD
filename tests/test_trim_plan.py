"""Trim/extend plans track target and boundary dependencies before mutation."""
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
from dxfcad_edit import apply_entity_update, create_line, _create_entity
from dxfcad_objects import DXFObjectProxy

DRAWING = ("0\nSECTION\n2\nENTITIES\n0\nLINE\n5\nAB\n330\nFF\n10\n0\n20\n0\n11\n10\n21\n0\n"
           "1001\nTEST\n1000\nretain me\n0\nLINE\n5\nBC\n10\n5\n20\n-5\n11\n5\n21\n5\n"
           "0\nLINE\n5\nBD\n10\n15\n20\n-5\n11\n15\n21\n5\n"
           "0\nLINE\n5\nBE\n10\n-5\n20\n-5\n11\n-5\n21\n5\n"
           "0\nSPLINE\n999\nopaque\n0\nENDSEC\n0\nEOF\n")


class TrimPlanTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(dir=module_directory)
        path = Path(self.folder.name) / "trim.dxf"
        path.write_text(DRAWING)
        self.doc = import_dxf(path)
        self.target, self.cut, self.far, self.left = [o for o in self.doc.Objects if hasattr(o, "DXFData")]
        self.root = self.target.DXFSourceRoot

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()): FreeCAD.closeDocument(name)
        self.folder.cleanup()

    def test_both_endpoints_trim_extend_preview_export_identity_and_undo(self):
        original = self.target.DXFData
        source = self.root.DXFSourceData
        for boundary, start, extend, length in ((self.cut, False, False, 5), (self.cut, True, False, 5),
                                               (self.far, False, True, 15), (self.left, True, True, 15)):
            before = boundary.DXFData
            plan = DXFCAD.prepare_trim(self.target, boundary, start=start, extend=extend)
            self.assertIsInstance(plan, DXFCAD.TrimPlan)
            self.assertEqual(plan.target, self.target)
            self.assertEqual(plan.boundary, boundary)
            self.assertEqual(list(plan.objects), [self.target])
            self.assertEqual(self.target.DXFData, original)
            raw = plan.preview_records[0][1]
            expected = plan.drawing_bytes[self.root.Name]
            result = apply_entity_update(plan, "Trim/extend")
            self.assertEqual(result, [self.target])
            self.assertEqual(self.target.DXFData, raw)
            self.assertAlmostEqual(self.target.Shape.Length, length)
            self.assertEqual(boundary.DXFData, before)
            self.assertEqual(self.target.DXFHandle, "AB")
            self.assertEqual(self.target.DXFSourceIndex, 0)
            self.assertIn("retain me", raw)
            self.assertIn("\n330\nFF\n", raw)
            self.assertIn(b"opaque", expected)
            self.assertEqual(self.root.DXFSourceData, source)
            self.assertEqual(DXFCAD.current_drawing_bytes(self.root), expected)
            with self.assertRaisesRegex(ValueError, "already been applied"): plan.apply()
            self.doc.undo(); self.doc.recompute()
            self.assertEqual(self.target.DXFData, original)
            self.doc.redo(); self.doc.recompute()
            self.assertEqual(self.target.DXFData, raw)
            self.doc.undo(); self.doc.recompute()

    def test_stale_target_boundary_ownership_index_context_and_placement(self):
        for obj in (self.target, self.cut):
            changes = [("DXFData", DXFCAD.offset_entity(obj.DXFData, 1)), ("DXFSourceRoot", None),
                       ("DXFSourceIndex", -1),
                       ("Placement", FreeCAD.Placement(FreeCAD.Vector(1, 0, 0), FreeCAD.Rotation()))]
            for prop, value in changes:
                plan = DXFCAD.prepare_trim(self.target, self.cut)
                before = self.target.DXFData
                old = getattr(obj, prop)
                setattr(obj, prop, value)
                with self.assertRaises(ValueError): plan.apply()
                if obj != self.target or prop != "DXFData": self.assertEqual(self.target.DXFData, before)
                setattr(obj, prop, old)
            plan = DXFCAD.prepare_trim(self.target, self.cut)
            obj.addProperty("App::PropertyString", "DXFContext", "DXF")
            obj.DXFContext = "changed context"
            with self.assertRaises(ValueError): plan.apply()
            obj.removeProperty("DXFContext")

    def test_other_drawing_and_detached_boundaries_are_explicit_dependencies(self):
        root = DXFCAD.create_drawing(self.doc, DXFCAD.DXFDocument.empty(4))
        boundary = create_line(self.doc, DXFCAD.Vector3D(5, -5, 0), DXFCAD.Vector3D(5, 5, 0), root)
        for detached in (False, True):
            if detached:
                boundary = DXFCAD.create_feature(self.doc, DXFCAD.parse_entity(boundary.DXFData), "")
                if boundary.TypeId == "Part::FeaturePython": DXFObjectProxy(boundary)
                self.doc.recompute()
            plan = DXFCAD.prepare_trim(self.target, boundary)
            original = self.target.DXFData
            raw = boundary.DXFData
            boundary.DXFData = DXFCAD.offset_entity(raw, 1)
            with self.assertRaisesRegex(ValueError, "changed"): plan.apply()
            self.assertEqual(self.target.DXFData, original)
            boundary.DXFData = raw
            apply_entity_update(DXFCAD.prepare_trim(self.target, boundary), "Trim")
            self.assertAlmostEqual(self.target.Shape.Length, 5)
            self.doc.undo(); self.doc.recompute()

    def test_invalid_dependencies_and_geometry_leave_document_unchanged(self):
        before = DXFCAD.current_drawing_bytes(self.root)
        for boundary in (self.target, self.far):
            with self.assertRaises(ValueError): DXFCAD.prepare_trim(self.target, boundary)
        with self.assertRaises(ValueError): DXFCAD.prepare_trim(self.target, self.cut, extend=True)
        raised = create_line(self.doc, DXFCAD.Vector3D(5, -5, 1), DXFCAD.Vector3D(5, 5, 1))
        with self.assertRaises(ValueError): DXFCAD.prepare_trim(self.target, raised)
        foreign = FreeCAD.newDocument("Foreign")
        boundary = create_line(foreign, DXFCAD.Vector3D(5, -5, 0), DXFCAD.Vector3D(5, 5, 0))
        with self.assertRaises(ValueError): DXFCAD.prepare_trim(self.target, boundary)
        self.doc.removeObject(raised.Name)
        self.assertEqual(DXFCAD.current_drawing_bytes(self.root), before)

    def test_deleted_boundary_and_changed_boundary_drawing(self):
        original = self.target.DXFData
        plan = DXFCAD.prepare_trim(self.target, self.cut)
        DXFCAD.ensure_layer(self.root, "New", 3, True)
        with self.assertRaises(ValueError): plan.apply()
        plan = DXFCAD.prepare_trim(self.target, self.cut)
        self.doc.removeObject(self.cut.Name)
        with self.assertRaises((ValueError, RuntimeError)): plan.apply()
        self.assertEqual(self.target.DXFData, original)

    def test_curved_and_polyline_results_and_display_failure_rollback(self):
        arc = _create_entity(self.doc, "0\nARC\n10\n0\n20\n0\n40\n5\n50\n0\n51\n180\n", self.root, "ARC")
        boundary = create_line(self.doc, DXFCAD.Vector3D(0, -10, 0), DXFCAD.Vector3D(0, 10, 0), self.root)
        poly = _create_entity(self.doc, "0\nLWPOLYLINE\n90\n3\n70\n128\n10\n0\n20\n0\n91\n1\n10\n5\n20\n0\n91\n2\n10\n10\n20\n0\n91\n3\n", self.root, "LWPOLYLINE")
        cut = create_line(self.doc, DXFCAD.Vector3D(7, -5, 0), DXFCAD.Vector3D(7, 5, 0), self.root)
        for target, edge in ((arc, boundary), (poly, cut)):
            raw = target.DXFData
            expected = DXFCAD.trim_entity(raw, edge.DXFData, False)
            plan = DXFCAD.prepare_trim(target, edge)
            self.assertEqual(plan.preview_records[0][1], expected)
            with patch("dxfcad_edit._check_display", side_effect=ValueError("display failure")):
                with self.assertRaisesRegex(ValueError, "display failure"):
                    apply_entity_update(plan, "Fail trim")
            self.assertEqual(target.DXFData, raw)
            apply_entity_update(DXFCAD.prepare_trim(target, edge), "Trim curve")
            self.assertEqual(target.DXFData, expected)


unittest.main(argv=[sys.argv[0]])
