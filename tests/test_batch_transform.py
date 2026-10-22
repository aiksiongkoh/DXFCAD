"""Batch plans validate complete drawings, preserve identity and reject stale edits."""
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
from dxfcad_edit import apply_batch_plan,create_line

DRAWING=("0\nSECTION\n2\nENTITIES\n0\nLINE\n5\nAB\n10\n1\n20\n2\n11\n10\n21\n2\n"
         "1001\nTEST\n1000\nretain me\n0\nCIRCLE\n5\nBC\n10\n20\n20\n0\n40\n3\n"
         "0\nSPLINE\n999\nopaque\n0\nENDSEC\n0\nEOF\n")


class BatchTransformTests(unittest.TestCase):
    def setUp(self):
        self.folder=tempfile.TemporaryDirectory(dir=module_directory)
        path=Path(self.folder.name)/"batch.dxf"; path.write_text(DRAWING)
        self.doc=import_dxf(path)
        self.doc.UndoMode=1
        self.objects=[o for o in self.doc.Objects if hasattr(o,"DXFData")]
        self.root=self.objects[0].DXFSourceRoot

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()): FreeCAD.closeDocument(name)
        self.folder.cleanup()

    def test_update_plans_preserve_identity_and_match_predicted_export(self):
        added=create_line(self.doc,DXFCAD.Vector3D(),DXFCAD.Vector3D(1,1,0),self.root)
        objects=self.objects+[added]
        before=[o.DXFData for o in objects]
        names=[o.Name for o in objects]
        slots=[o.DXFSourceIndex for o in objects]
        source=self.root.DXFSourceData
        for op,kwargs in (("move",dict(offset=DXFCAD.Vector3D(2,3,4))),
                          ("rotate",dict(value=30,pivot=DXFCAD.Vector3D(1,2,0))),
                          ("scale",dict(value=2)),("mirror",dict(value=45))):
            plan=DXFCAD.prepare_batch_transform(objects+[objects[0]],op,**kwargs)
            self.assertFalse(plan.copies)
            self.assertEqual(len(plan.objects),3)
            self.assertEqual([o.DXFData for o in objects],before)
            predicted=plan.drawing_bytes[self.root.Name]
            preview=[raw for obj,raw in plan.preview_records]
            result=apply_batch_plan(plan,op)
            self.assertEqual([o.Name for o in result],names)
            self.assertEqual([o.DXFSourceIndex for o in result],slots)
            self.assertEqual([o.DXFData for o in result],preview)
            self.assertEqual(self.root.DXFSourceData,source)
            self.assertEqual(DXFCAD.current_drawing_bytes(self.root),predicted)
            self.doc.undo(); self.doc.recompute()
            self.assertEqual([o.DXFData for o in objects],before)

    def test_copy_is_one_shot_with_new_identity_and_undo_redo(self):
        before=[o.DXFData for o in self.objects]
        plan=DXFCAD.prepare_batch_transform(self.objects,"copy",offset=DXFCAD.Vector3D(4,5,6))
        predicted=plan.drawing_bytes[self.root.Name]
        copies=apply_batch_plan(plan,"Copy selection")
        self.assertEqual([o.DXFData for o in self.objects],before)
        self.assertTrue(all(o.DXFSourceRoot==self.root and o.DXFSourceIndex==-1 for o in copies))
        self.assertTrue(all(not DXFCAD.parse_entity(o.DXFData).handle for o in copies))
        self.assertEqual(DXFCAD.current_drawing_bytes(self.root),predicted)
        self.assertEqual(len({e.handle for e in DXFCAD.current_drawing(self.root).entities}),4)
        with self.assertRaisesRegex(ValueError,"already been applied"): plan.apply()
        names=[o.Name for o in copies]
        self.doc.undo(); self.doc.recompute()
        self.assertTrue(all(self.doc.getObject(n) is None for n in names))
        self.doc.redo(); self.doc.recompute()
        self.assertEqual(DXFCAD.current_drawing_bytes(self.root),predicted)

    def test_stale_selected_unselected_context_source_and_ownership(self):
        line,circle=self.objects
        original_line=line.DXFData
        for changed in (line,circle):
            plan=DXFCAD.prepare_batch_transform([line],"move",offset=DXFCAD.Vector3D(1,0,0))
            raw=changed.DXFData
            changed.DXFData=DXFCAD.parse_entity(raw).translated(DXFCAD.Vector3D(0,1,0))
            with self.assertRaisesRegex(ValueError,"changed"): plan.apply()
            changed.DXFData=raw
        plan=DXFCAD.prepare_batch_transform([line],"copy")
        line.DXFSourceIndex=-1
        with self.assertRaisesRegex(ValueError,"changed"): plan.apply()
        line.DXFSourceIndex=0
        plan=DXFCAD.prepare_batch_transform([line],"copy")
        line.addProperty("App::PropertyString","DXFContext","DXF")
        line.DXFContext="changed context"
        with self.assertRaisesRegex(ValueError,"changed"): plan.apply()
        line.removeProperty("DXFContext")
        plan=DXFCAD.prepare_batch_transform([line],"copy")
        source=DXFCAD.drawing_document(self.root)
        DXFCAD.set_drawing_source(self.root,DXFCAD.DXFDocument.parse(DXFCAD.set_layer(source.serialize(),"Extra",3,True)))
        with self.assertRaisesRegex(ValueError,"changed"): plan.apply()
        self.assertEqual(line.DXFData,original_line)

    def test_multiple_drawings_remain_separate(self):
        other_root=DXFCAD.create_drawing(self.doc,DXFCAD.DXFDocument.empty(4))
        other=create_line(self.doc,DXFCAD.Vector3D(),DXFCAD.Vector3D(1,0,0),other_root)
        plan=DXFCAD.prepare_batch_transform([self.objects[0],other],"copy",offset=DXFCAD.Vector3D(1,0,0))
        expected=plan.drawing_bytes
        copies=apply_batch_plan(plan,"Copy two drawings")
        self.assertEqual([o.DXFSourceRoot for o in copies],[self.root,other_root])
        for root in (self.root,other_root): self.assertEqual(DXFCAD.current_drawing_bytes(root),expected[root.Name])

    def test_invalid_parameters_selection_placement_and_source_slots_do_not_mutate(self):
        names={o.Name for o in self.doc.Objects}
        raw=[o.DXFData for o in self.objects]
        for op,kwargs in (("move",{}),("move",dict(offset=DXFCAD.Vector3D(float("nan"),0,0))),
                          ("scale",dict(value=0)),("rotate",dict(value=float("inf"))),("unknown",{})):
            with self.assertRaises(ValueError): DXFCAD.prepare_batch_transform(self.objects,op,**kwargs)
        with self.assertRaises(ValueError): DXFCAD.prepare_batch_transform([],"copy")
        plan=DXFCAD.prepare_batch_transform(self.objects,"copy")
        self.objects[0].Placement=FreeCAD.Placement(FreeCAD.Vector(1,0,0),FreeCAD.Rotation())
        with self.assertRaisesRegex(ValueError,"Placement"): plan.apply()
        self.objects[0].Placement=FreeCAD.Placement()
        self.objects[1].DXFSourceIndex=0
        with self.assertRaisesRegex(ValueError,"duplicate"): DXFCAD.prepare_batch_transform(self.objects,"copy")
        self.objects[1].DXFSourceIndex=1
        self.assertEqual([o.DXFData for o in self.objects],raw)
        self.assertEqual({o.Name for o in self.doc.Objects},names)

    def test_copy_creation_failure_rolls_back_all_pieces(self):
        names={o.Name for o in self.doc.Objects}
        before=DXFCAD.current_drawing_bytes(self.root)
        factory=DXFCAD.create_feature
        count=[]
        def failing_create(*args):
            count.append(args)
            if len(count)==2: raise RuntimeError("injected batch creation failure")
            return factory(*args)
        plan=DXFCAD.prepare_batch_transform(self.objects,"copy",offset=DXFCAD.Vector3D(1,0,0))
        with patch.object(DXFCAD,"create_feature",side_effect=failing_create):
            with self.assertRaisesRegex(RuntimeError,"batch creation"):
                apply_batch_plan(plan,"Fail copy")
        self.assertEqual(len(count),2)
        self.assertEqual({o.Name for o in self.doc.Objects},names)
        self.assertEqual(DXFCAD.current_drawing_bytes(self.root),before)


if __name__=="__main__": unittest.main()
