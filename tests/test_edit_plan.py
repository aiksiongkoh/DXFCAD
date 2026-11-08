"""Prepared document edits share EditPlan in C++ and Python without changing behavior."""

import FreeCAD
import Part
from pathlib import Path
import sys
import unittest

module_directory = Path(sys.argv.pop(1)).resolve()
sys.path.insert(0, str(module_directory))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import DXFCAD
from dxfcad_edit import create_line, apply_batch_plan

assert Path(DXFCAD.__file__).parent == module_directory


class EditPlanTests(unittest.TestCase):
    def tearDown(self):
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)

    def test_abstract_base_and_preserved_copy_specialization(self):
        with self.assertRaises(TypeError):
            DXFCAD.EditPlan()
        self.assertEqual(DXFCAD.EntityCopyPlan.__bases__, (DXFCAD.BatchObjectTransformPlan,))
        for name in ("BatchObjectTransformPlan", "EntityUpdatePlan", "TrimPlan", "OffsetPlan",
                     "CornerObjectEditPlan", "ContourObjectEditPlan", "ContourOperationPlan"):
            with self.subTest(plan=name):
                self.assertEqual(getattr(DXFCAD, name).__bases__, (DXFCAD.EditPlan,))

    def test_real_plans_share_identity_without_mutating_during_preparation(self):
        doc = FreeCAD.newDocument("EditPlanHierarchy")
        root = DXFCAD.create_drawing(doc, DXFCAD.Drawing.empty(4))
        first = create_line(doc, DXFCAD.Vector3D(0, 0, 0), DXFCAD.Vector3D(10, 0, 0), root)
        second = create_line(doc, DXFCAD.Vector3D(10, 0, 0), DXFCAD.Vector3D(10, 10, 0), root)
        boundary = create_line(doc, DXFCAD.Vector3D(5, -5, 0), DXFCAD.Vector3D(5, 5, 0), root)
        original = DXFCAD.current_drawing_bytes(root)
        names = [obj.Name for obj in doc.Objects]
        offset = DXFCAD.Vector3D(0, 2, 0)
        moved = DXFCAD.parse_entity(first.DXFData).translated(offset)
        copied = DXFCAD.parse_entity(first.DXFData).copied(offset)
        plans = [
            DXFCAD.prepare_batch_transform([first], "move", offset=offset),
            DXFCAD.prepare_entity_copy([first], [copied]),
            DXFCAD.prepare_entity_update([first], [moved]),
            DXFCAD.prepare_trim(first, boundary, start=False),
            DXFCAD.prepare_offset([first], 1, keep_originals=True),
            DXFCAD.prepare_line_fillet([first, second], 2),
            DXFCAD.prepare_contour_edit([first], DXFCAD.Contour.split(first.DXFData, [4])),
            DXFCAD.prepare_contour_split(first, [4]),
        ]
        for plan in plans:
            with self.subTest(plan=type(plan).__name__):
                self.assertIsInstance(plan, DXFCAD.EditPlan)
                self.assertEqual(plan.document, doc)
                self.assertTrue(callable(plan.apply))
        self.assertIsInstance(plans[1], DXFCAD.BatchObjectTransformPlan)
        self.assertEqual(DXFCAD.current_drawing_bytes(root), original)
        self.assertEqual([obj.Name for obj in doc.Objects], names)
        # Applying a concrete plan through a reference recognized as EditPlan
        # retains its existing result, stale-state and transaction behavior.
        self.assertEqual(apply_batch_plan(plans[0], "Move through EditPlan"), [first])
        self.assertAlmostEqual(first.Shape.Vertexes[0].Point.y, 2)
        with self.assertRaises(ValueError):
            plans[0].apply()
        with self.assertRaisesRegex(ValueError, "changed"):
            plans[2].apply()
        doc.undo()
        doc.recompute()
        self.assertEqual(DXFCAD.current_drawing_bytes(root), original)


if __name__ == "__main__":
    unittest.main()
