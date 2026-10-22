"""Polyline corner plans combine geometry, provenance and validated in-place updates."""
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
from dxfcad_edit import apply_entity_update, create_polyline
from dxfcad_objects import DXFObjectProxy

POLY = ("0\nLWPOLYLINE\n5\nAB\n330\nFF\n8\n0\n90\n3\n70\n128\n38\n3\n230\n2\n39\n0.1\n"
        "10\n0\n20\n0\n40\n0.2\n41\n0.4\n91\n10\n10\n10\n20\n0\n40\n0.6\n41\n0.8\n91\n11\n"
        "10\n10\n20\n10\n91\n12\n1001\nTEST\n1000\nretain corner data\n")
DRAWING = ("0\nSECTION\n2\nENTITIES\n" + POLY +
           "0\nLINE\n5\nAC\n10\n20\n20\n0\n11\n30\n21\n0\n"
           "0\nSPLINE\n999\nopaque\n0\nENDSEC\n0\nEOF\n")


class PolylineCornerPlanTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(dir=module_directory)
        path = Path(self.folder.name) / "corners.dxf"
        path.write_text(DRAWING)
        self.doc = import_dxf(path)
        self.poly, self.line = [o for o in self.doc.Objects if hasattr(o, "DXFData")]
        self.root = self.poly.DXFSourceRoot

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()): FreeCAD.closeDocument(name)
        self.folder.cleanup()

    def plan(self, kind):
        if kind == "fillet": return DXFCAD.prepare_polyline_fillet(self.poly, 1, 2)
        return DXFCAD.prepare_polyline_chamfer(self.poly, 1, 2, 3)

    def test_both_operations_preview_identity_widths_metadata_export_and_undo(self):
        source = self.root.DXFSourceData
        names = [o.Name for o in self.doc.Objects]
        for kind in ("fillet", "chamfer"):
            plan = self.plan(kind)
            self.assertIsInstance(plan, DXFCAD.EntityUpdatePlan)
            self.assertEqual(self.poly.DXFData, POLY)
            raw = plan.preview_records[0][1]
            scalar = DXFCAD.fillet_corner(POLY, 1, 2) if kind == "fillet" else DXFCAD.chamfer_corner(POLY, 1, 2, 3)
            self.assertEqual(raw, scalar)
            expected = plan.drawing_bytes[self.root.Name]
            result = apply_entity_update(plan, kind)
            self.assertEqual(result, [self.poly])
            self.assertEqual([o.Name for o in self.doc.Objects], names)
            self.assertEqual(self.poly.DXFHandle, "AB")
            self.assertEqual(self.poly.DXFSourceIndex, 0)
            self.assertEqual(self.poly.DXFSourceRoot, self.root)
            self.assertEqual(self.root.DXFSourceData, source)
            self.assertEqual(DXFCAD.current_drawing_bytes(self.root), expected)
            self.assertIn(b"opaque", expected)
            self.assertIn("retain corner data", raw)
            self.assertIn("\n330\nFF\n", raw)
            self.assertIn("\n70\n128\n", raw)
            self.assertEqual(raw.count("\n91\n"), 3)
            for vertex_id in (10, 11, 12): self.assertIn(f"\n91\n{vertex_id}\n", raw)
            model = DXFCAD.read_lwpolyline(raw)
            self.assertEqual(len(model.vertices), 4)
            self.assertEqual(model.elevation, 3)
            self.assertEqual(model.extrusion.z, 2)
            self.assertAlmostEqual(model.vertices[1].start_width, 0.36)
            self.assertAlmostEqual(model.vertices[1].end_width, 0.64 if kind == "fillet" else 0.66)
            self.assertFalse(self.poly.Shape.isNull())
            with self.assertRaises(ValueError): plan.apply()
            self.doc.undo(); self.doc.recompute()
            self.assertEqual(self.poly.DXFData, POLY)
            self.doc.redo(); self.doc.recompute()
            self.assertEqual(self.poly.DXFData, raw)
            self.doc.undo(); self.doc.recompute()

    def test_invalid_corner_parameters_types_and_placement_never_mutate(self):
        before = DXFCAD.current_drawing_bytes(self.root)
        for vertex, radius in ((0, 1), (2, 1), (99, 1), (1, 0), (1, -1), (1, float("nan")), (1, 100)):
            with self.assertRaises(ValueError): DXFCAD.prepare_polyline_fillet(self.poly, vertex, radius)
        for incoming, outgoing in ((0, 1), (1, -1), (100, 1), (1, float("inf"))):
            with self.assertRaises(ValueError): DXFCAD.prepare_polyline_chamfer(self.poly, 1, incoming, outgoing)
        with self.assertRaises(ValueError): DXFCAD.prepare_polyline_fillet(self.line, 1, 1)
        with self.assertRaises(ValueError): DXFCAD.prepare_polyline_chamfer(self.line, 1, 1, 1)
        self.assertEqual(DXFCAD.current_drawing_bytes(self.root), before)
        self.poly.Placement = FreeCAD.Placement(FreeCAD.Vector(1, 0, 0), FreeCAD.Rotation())
        with self.assertRaises(ValueError): self.plan("fillet")
        with self.assertRaises(ValueError): self.plan("chamfer")
        self.assertEqual(self.poly.DXFData, POLY)

    def test_stale_target_unselected_records_and_drawing_metadata(self):
        for kind in ("fillet", "chamfer"):
            for obj in (self.poly, self.line):
                plan = self.plan(kind)
                raw = obj.DXFData
                obj.DXFData = DXFCAD.parse_entity(raw).translated(DXFCAD.Vector3D(1, 0, 0))
                with self.assertRaises(ValueError): plan.apply()
                obj.DXFData = raw
            for prop, value in (("DXFSourceRoot", None), ("DXFSourceIndex", -1),
                                ("Placement", FreeCAD.Placement(FreeCAD.Vector(1, 0, 0), FreeCAD.Rotation()))):
                plan = self.plan(kind)
                old = getattr(self.poly, prop)
                setattr(self.poly, prop, value)
                with self.assertRaises(ValueError): plan.apply()
                setattr(self.poly, prop, old)
        plan = self.plan("fillet")
        DXFCAD.ensure_layer(self.root, "Changed", 3, True)
        with self.assertRaises(ValueError): plan.apply()
        self.assertEqual(self.poly.DXFData, POLY)

    def test_display_failure_rolls_back_in_place_changes(self):
        before = DXFCAD.current_drawing_bytes(self.root)
        for kind in ("fillet", "chamfer"):
            plan = self.plan(kind)
            with patch("dxfcad_edit._check_display", side_effect=ValueError("display failure")):
                with self.assertRaisesRegex(ValueError, "display failure"):
                    apply_entity_update(plan, "Fail corner")
            self.assertEqual(self.poly.DXFData, POLY)
            self.assertEqual(DXFCAD.current_drawing_bytes(self.root), before)

    def test_closed_seam_and_detached_entities(self):
        model = DXFCAD.LWPolyline([DXFCAD.LWPolylineVertex(0, 0), DXFCAD.LWPolylineVertex(10, 0),
                                  DXFCAD.LWPolylineVertex(10, 10), DXFCAD.LWPolylineVertex(0, 10)], True)
        closed = create_polyline(self.doc, model, self.root)
        for fillet in (True, False):
            plan = DXFCAD.prepare_polyline_fillet(closed, 0, 1) if fillet else DXFCAD.prepare_polyline_chamfer(closed, 0, 1, 2)
            apply_entity_update(plan, "Closed seam corner")
            self.assertTrue(DXFCAD.read_lwpolyline(closed.DXFData).closed)
            self.assertEqual(len(DXFCAD.read_lwpolyline(closed.DXFData).vertices), 5)
            self.doc.undo(); self.doc.recompute()
        detached = DXFCAD.create_feature(self.doc, DXFCAD.parse_entity(POLY), "")
        if detached.TypeId == "Part::FeaturePython": DXFObjectProxy(detached)
        self.doc.recompute()
        plan = DXFCAD.prepare_polyline_fillet(detached, 1, 2)
        self.assertEqual(plan.drawing_bytes, {})
        apply_entity_update(plan, "Detached corner")
        self.assertEqual(len(DXFCAD.read_lwpolyline(detached.DXFData).vertices), 4)


unittest.main(argv=[sys.argv[0]])
