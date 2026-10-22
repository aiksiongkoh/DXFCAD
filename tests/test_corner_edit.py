"""Corner plans: preview, identity, stale state, atomic failure, undo and export."""
import FreeCAD
import Part
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

module_directory=Path(sys.argv.pop(1)).resolve()
sys.path.insert(0,str(module_directory))
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import DXFCAD
assert Path(DXFCAD.__file__).parent==module_directory
from dxfcad_import import import_dxf
from dxfcad_edit import apply_corner_plan,create_line

A="0\nLINE\n5\nAB\n10\n0\n20\n0\n11\n10\n21\n0\n1001\nTEST\n1000\nkeep me\n"
B="0\nLINE\n5\nBC\n10\n10\n20\n0\n11\n10\n21\n10\n"
DRAWING="0\nSECTION\n2\nENTITIES\n"+A+B+"0\nSPLINE\n999\nopaque\n0\nENDSEC\n0\nEOF\n"


class CornerEditTests(unittest.TestCase):
    def setUp(self):
        self.folder=tempfile.TemporaryDirectory(dir=module_directory)
        path=Path(self.folder.name)/"corner.dxf"; path.write_text(DRAWING)
        self.doc=import_dxf(path)
        self.doc.UndoMode=1
        self.lines=[o for o in self.doc.Objects if hasattr(o,"DXFData")]
        self.root=self.lines[0].DXFSourceRoot

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()): FreeCAD.closeDocument(name)
        self.folder.cleanup()

    def test_preview_apply_identity_undo_redo_and_export(self):
        for operation,expected_type in ((lambda:DXFCAD.prepare_line_fillet(self.lines,2),"ARC"),
                                        (lambda:DXFCAD.prepare_line_chamfer(self.lines,2,3),"LINE")):
            source=self.root.DXFSourceData
            names=[o.Name for o in self.lines]
            before=[o.DXFData for o in self.lines]
            plan=operation()
            expected=plan.drawing_bytes
            self.assertEqual(len(plan.preview_records),3)
            self.assertEqual([o.DXFData for o in self.lines],before)
            self.assertEqual(self.root.DXFSourceData,source)
            connector=apply_corner_plan(plan,"Corner edit")
            connector_name=connector.Name
            self.assertEqual(connector.DXFType,expected_type)
            self.assertEqual(connector.DXFSourceIndex,-1)
            self.assertEqual(connector.DXFSourceRoot,self.root)
            self.assertFalse(DXFCAD.parse_entity(connector.DXFData).handle)
            self.assertEqual([o.Name for o in self.lines],names)
            self.assertEqual([o.DXFSourceIndex for o in self.lines],[0,1])
            self.assertEqual([o.DXFHandle for o in self.lines],["AB","BC"])
            self.assertTrue(connector.Shape.isValid())
            self.assertEqual(DXFCAD.current_drawing_bytes(self.root),expected)
            with self.assertRaisesRegex(ValueError,"already been applied"): plan.apply()
            self.doc.undo(); self.doc.recompute()
            self.assertEqual([o.DXFData for o in self.lines],before)
            self.assertIsNone(self.doc.getObject(connector_name))
            self.doc.redo(); self.doc.recompute()
            self.assertEqual(DXFCAD.current_drawing_bytes(self.root),expected)
            self.doc.undo(); self.doc.recompute()

    def test_stale_selected_unselected_and_source_snapshots_are_rejected(self):
        extra=create_line(self.doc,DXFCAD.Vector3D(20,0,0),DXFCAD.Vector3D(21,0,0),self.root)
        for changed in (self.lines[0],extra):
            plan=DXFCAD.prepare_line_fillet(self.lines,2)
            raw=changed.DXFData
            changed.DXFData=DXFCAD.parse_entity(raw).translated(DXFCAD.Vector3D(0,1,0))
            with self.assertRaisesRegex(ValueError,"changed"): plan.apply()
            changed.DXFData=raw
        plan=DXFCAD.prepare_line_fillet(self.lines,2)
        source=DXFCAD.drawing_document(self.root)
        DXFCAD.set_drawing_source(self.root,DXFCAD.DXFDocument.parse(DXFCAD.set_layer(source.serialize(),"Extra",3,True)))
        with self.assertRaisesRegex(ValueError,"changed"): plan.apply()
        self.assertEqual([o.DXFData for o in self.lines],[A,B])

    def test_wrong_selection_types_and_results_do_not_mutate(self):
        other_root=DXFCAD.create_drawing(self.doc,DXFCAD.DXFDocument.empty(4))
        other=create_line(self.doc,DXFCAD.Vector3D(10,0,0),DXFCAD.Vector3D(10,10,0),other_root)
        names={o.Name for o in self.doc.Objects}
        with self.assertRaisesRegex(ValueError,"same DXFCAD drawing"):
            DXFCAD.prepare_line_fillet([self.lines[0],other],2)
        with self.assertRaises(ValueError): DXFCAD.prepare_line_fillet([self.lines[0]]*2,2)
        with self.assertRaises(ValueError): DXFCAD.prepare_line_fillet([],2)
        with self.assertRaises(ValueError): DXFCAD.prepare_line_chamfer(self.lines,-1,3)
        result=DXFCAD.fillet_lines(A,B,2)
        with self.assertRaisesRegex(ValueError,"connector type"):
            DXFCAD.prepare_corner_edit(self.lines,[result.first,result.second],result.arc,"LINE")
        with self.assertRaisesRegex(ValueError,"identity"):
            DXFCAD.prepare_corner_edit(self.lines,[DXFCAD.parse_entity(result.first).copied(DXFCAD.Vector3D()),result.second],result.arc,"ARC")
        self.assertEqual({o.Name for o in self.doc.Objects},names)
        self.assertEqual([o.DXFData for o in self.lines],[A,B])

    def test_failure_after_connector_creation_rolls_back_both_lines_and_connector(self):
        before={o.Name for o in self.doc.Objects}
        original=DXFCAD.current_drawing_bytes(self.root)
        factory=DXFCAD.create_feature
        def fail_after_create(*args):
            factory(*args)
            raise RuntimeError("injected connector failure")
        plan=DXFCAD.prepare_line_chamfer(self.lines,2,3)
        with patch.object(DXFCAD,"create_feature",side_effect=fail_after_create):
            with self.assertRaisesRegex(RuntimeError,"connector failure"):
                apply_corner_plan(plan,"Fail corner")
        self.assertEqual({o.Name for o in self.doc.Objects},before)
        self.assertEqual(DXFCAD.current_drawing_bytes(self.root),original)
        self.assertEqual([o.DXFData for o in self.lines],[A,B])

    def test_stale_ownership_placement_and_deleted_selection(self):
        plan=DXFCAD.prepare_line_fillet(self.lines,2)
        self.lines[0].DXFSourceIndex=-1
        with self.assertRaisesRegex(ValueError,"changed"): plan.apply()
        self.lines[0].DXFSourceIndex=0
        plan=DXFCAD.prepare_line_fillet(self.lines,2)
        self.lines[0].Placement=FreeCAD.Placement(FreeCAD.Vector(1,0,0),FreeCAD.Rotation())
        with self.assertRaisesRegex(ValueError,"Placement"): plan.apply()
        self.lines[0].Placement=FreeCAD.Placement()
        plan=DXFCAD.prepare_line_fillet(self.lines,2)
        self.doc.removeObject(self.lines[1].Name)
        with self.assertRaisesRegex(ValueError,"changed"): plan.apply()


if __name__=="__main__": unittest.main()
