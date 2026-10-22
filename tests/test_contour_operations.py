"""Operation-specific contour plans: topology, dependencies, export and rollback."""
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
from dxfcad_edit import apply_contour_plan, create_line
from dxfcad_objects import DXFObjectProxy

DRAWING = ("0\nSECTION\n2\nENTITIES\n0\nLINE\n5\nAB\n10\n0\n20\n0\n11\n10\n21\n0\n1001\nTEST\n1000\nretain me\n"
           "0\nLINE\n5\nAC\n10\n3\n20\n-5\n11\n3\n21\n5\n"
           "0\nLINE\n5\nAD\n10\n7\n20\n-5\n11\n7\n21\n5\n"
           "0\nCIRCLE\n5\nAE\n10\n20\n20\n0\n40\n5\n1001\nTEST\n1000\nretain circle\n"
           "0\nSPLINE\n999\nopaque\n0\nENDSEC\n0\nEOF\n")


class ContourOperationTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(dir=module_directory)
        path = Path(self.folder.name) / "contours.dxf"
        path.write_text(DRAWING)
        self.doc = import_dxf(path)
        self.line, self.first, self.second, self.circle = [o for o in self.doc.Objects if hasattr(o, "DXFData")]
        self.root = self.line.DXFSourceRoot

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()): FreeCAD.closeDocument(name)
        self.folder.cleanup()

    def test_split_break_trim_preview_identity_export_and_undo_redo(self):
        original = self.line.DXFData
        source = self.root.DXFSourceData
        boundaries = [self.first.DXFData, self.second.DXFData]
        for operation, lengths in (("split", [3, 4, 3]), ("break", [3, 3]), ("trim", [3, 3])):
            if operation == "split": plan = DXFCAD.prepare_contour_split(self.line, [3, 7])
            elif operation == "break": plan = DXFCAD.prepare_contour_break(self.line, 3, 7)
            else: plan = DXFCAD.prepare_interior_trim(self.line, [self.first, self.second], 5)
            self.assertIsInstance(plan, DXFCAD.ContourOperationPlan)
            self.assertEqual(self.line.DXFData, original)
            self.assertTrue(plan.preserves_first)
            self.assertEqual(plan.template_object, self.line)
            preview = [raw for obj, raw in plan.preview_records]
            expected = plan.drawing_bytes
            result = apply_contour_plan(plan, operation)
            self.assertEqual([o.DXFData for o in result], preview)
            self.assertEqual([round(o.Shape.Length, 7) for o in result], lengths)
            self.assertEqual(result[0].DXFHandle, "AB")
            self.assertEqual(result[0].DXFSourceIndex, 0)
            self.assertEqual(list(plan.created_objects), result[1:])
            self.assertTrue(all(o.DXFSourceIndex == -1 for o in result[1:]))
            self.assertEqual([self.first.DXFData, self.second.DXFData], boundaries)
            self.assertEqual(self.root.DXFSourceData, source)
            self.assertEqual(DXFCAD.current_drawing_bytes(self.root), expected)
            self.assertIn(b"opaque", expected)
            self.assertTrue(all("retain me" in o.DXFData for o in result))
            with self.assertRaisesRegex(ValueError, "already been applied"): plan.apply()
            self.doc.undo(); self.doc.recompute()
            self.assertEqual(self.line.DXFData, original)
            self.doc.redo(); self.doc.recompute()
            self.assertEqual(DXFCAD.current_drawing_bytes(self.root), expected)
            self.doc.undo(); self.doc.recompute()

    def test_join_close_and_closed_seam_rules_live_in_cpp(self):
        a = create_line(self.doc, DXFCAD.Vector3D(20, 5, 0), DXFCAD.Vector3D(25, 5, 0), self.root)
        b = create_line(self.doc, DXFCAD.Vector3D(25, 10, 0), DXFCAD.Vector3D(25, 5, 0), self.root)
        names = [a.Name, b.Name]
        plan = DXFCAD.prepare_contour_join([b, a])
        self.assertFalse(plan.preserves_first)
        joined = apply_contour_plan(plan, "Join")[0]
        self.assertTrue(all(self.doc.getObject(name) is None for name in names))
        self.assertEqual(joined.DXFType, "LWPOLYLINE")
        self.assertAlmostEqual(joined.Shape.Length, 10)
        self.assertEqual(joined.DXFSourceIndex, -1)
        close = DXFCAD.prepare_contour_close(joined)
        self.assertTrue(close.preserves_first)
        self.assertEqual(apply_contour_plan(close, "Close"), [joined])
        self.assertTrue(DXFCAD.read_lwpolyline(joined.DXFData).closed)
        length = joined.Shape.Length
        seam = DXFCAD.prepare_contour_split(joined, [2])
        result = apply_contour_plan(seam, "Open seam")
        self.assertEqual(len(result), 1)
        self.assertFalse(DXFCAD.read_lwpolyline(result[0].DXFData).closed)
        self.assertAlmostEqual(result[0].Shape.Length, length)

    def test_type_changing_circle_break_preserves_metadata(self):
        name = self.circle.Name
        plan = DXFCAD.prepare_contour_break(self.circle, 1, 4)
        self.assertFalse(plan.preserves_first)
        expected = plan.drawing_bytes
        result = apply_contour_plan(plan, "Break circle")
        self.assertIsNone(self.doc.getObject(name))
        self.assertEqual(result[0].DXFType, "ARC")
        self.assertEqual(result[0].DXFSourceIndex, -1)
        self.assertIn("retain circle", result[0].DXFData)
        self.assertEqual(DXFCAD.current_drawing_bytes(self.root), expected)

    def test_boundary_drawings_and_detached_boundaries_are_dependencies(self):
        root = DXFCAD.create_drawing(self.doc, DXFCAD.DXFDocument.empty(4))
        a = create_line(self.doc, DXFCAD.Vector3D(3, -5, 0), DXFCAD.Vector3D(3, 5, 0), root)
        b = create_line(self.doc, DXFCAD.Vector3D(7, -5, 0), DXFCAD.Vector3D(7, 5, 0), root)
        plan = DXFCAD.prepare_interior_trim(self.line, [a, b], 5)
        original = self.line.DXFData
        DXFCAD.ensure_layer(root, "Changed", 3, True)
        with self.assertRaises(ValueError): plan.apply()
        self.assertEqual(self.line.DXFData, original)
        detached = DXFCAD.create_feature(self.doc, DXFCAD.parse_entity(a.DXFData), "")
        if detached.TypeId == "Part::FeaturePython": DXFObjectProxy(detached)
        self.doc.recompute()
        plan = DXFCAD.prepare_interior_trim(self.line, [detached, b], 5)
        raw = detached.DXFData
        detached.DXFData = DXFCAD.offset_entity(raw, 1)
        with self.assertRaises(ValueError): plan.apply()
        detached.DXFData = raw
        plan = DXFCAD.prepare_contour_operation([b, self.line, detached], "trim", target_index=1, distances=[5])
        self.assertEqual(plan.template_object, self.line)
        result = apply_contour_plan(plan, "Interior trim")
        self.assertEqual([round(o.Shape.Length, 7) for o in result], [3, 3])
        self.assertEqual(detached.DXFData, raw)

    def test_stale_inputs_source_slots_and_boundary_placement(self):
        for obj in (self.line, self.first):
            for prop, value in (("DXFData", DXFCAD.offset_entity(obj.DXFData, 1)),
                                ("DXFSourceRoot", None), ("DXFSourceIndex", -1),
                                ("Placement", FreeCAD.Placement(FreeCAD.Vector(1, 0, 0), FreeCAD.Rotation()))):
                plan = DXFCAD.prepare_interior_trim(self.line, [self.first, self.second], 5)
                old = getattr(obj, prop)
                setattr(obj, prop, value)
                with self.assertRaises(ValueError): plan.apply()
                setattr(obj, prop, old)
        plan = DXFCAD.prepare_interior_trim(self.line, [self.first, self.second], 5)
        self.doc.removeObject(self.first.Name)
        with self.assertRaises((ValueError, RuntimeError)): plan.apply()

    def test_invalid_operations_parameters_and_selection_do_not_mutate(self):
        names = [o.Name for o in self.doc.Objects]
        before = DXFCAD.current_drawing_bytes(self.root)
        cases = [([], "split", {}), ([self.line], "unknown", {}),
                 ([self.line], "split", dict(target_index=1, distances=[3])),
                 ([self.line, self.line], "join", {}), ([self.line], "join", {}),
                 ([self.line, self.first], "split", dict(distances=[3])),
                 ([self.line], "break", dict(distances=[3])),
                 ([self.line, self.first], "trim", dict(distances=[])),
                 ([self.line], "split", dict(distances=[-1])),
                 ([self.line], "close", dict(tolerance=-1))]
        for objects, operation, kwargs in cases:
            with self.assertRaises(ValueError): DXFCAD.prepare_contour_operation(objects, operation, **kwargs)
            self.assertEqual([o.Name for o in self.doc.Objects], names)
            self.assertEqual(DXFCAD.current_drawing_bytes(self.root), before)

    def test_creation_and_display_failures_roll_back_topology(self):
        names = [o.Name for o in self.doc.Objects]
        before = DXFCAD.current_drawing_bytes(self.root)
        factory = DXFCAD.create_feature
        def fail_after_create(*args):
            factory(*args)
            raise RuntimeError("factory failure")
        plan = DXFCAD.prepare_contour_split(self.line, [3, 7])
        with patch.object(DXFCAD, "create_feature", side_effect=fail_after_create):
            with self.assertRaisesRegex(RuntimeError, "factory failure"): apply_contour_plan(plan, "Fail split")
        plan = DXFCAD.prepare_contour_break(self.circle, 1, 4)
        with patch("dxfcad_edit._check_display", side_effect=ValueError("display failure")):
            with self.assertRaisesRegex(ValueError, "display failure"): apply_contour_plan(plan, "Fail break")
        self.assertEqual([o.Name for o in self.doc.Objects], names)
        self.assertEqual(DXFCAD.current_drawing_bytes(self.root), before)


unittest.main(argv=[sys.argv[0]])
