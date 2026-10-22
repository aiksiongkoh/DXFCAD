"""Topology replacement, transactions, persistence and export in both runtimes."""
import FreeCAD
import Part
from pathlib import Path
import subprocess
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
from dxfcad_export import export_dxf
from dxfcad_edit import replace_contours, apply_contour_plan

LINE="0\nLINE\n5\nAB\n10\n0\n20\n0\n11\n10\n21\n0\n1001\nSynthetic\n1000\nkeep line\n"
CIRCLE="0\nCIRCLE\n5\nBC\n10\n20\n20\n0\n40\n5\n1001\nSynthetic\n1000\nkeep circle\n"
DRAWING="0\nSECTION\n2\nENTITIES\n"+LINE+CIRCLE+"0\nENDSEC\n0\nEOF\n"


class ContourTests(unittest.TestCase):
    def setUp(self):
        self.folder=tempfile.TemporaryDirectory()
        self.source=Path(self.folder.name)/"contours.dxf"
        self.source.write_text(DRAWING)
        self.doc=import_dxf(self.source)
        self.doc.UndoMode=1
        self.line=next(o for o in self.doc.Objects if getattr(o,"DXFType","")=="LINE")
        self.circle=next(o for o in self.doc.Objects if getattr(o,"DXFType","")=="CIRCLE")

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()): FreeCAD.closeDocument(name)
        self.folder.cleanup()

    def test_split_undo_restore_and_export(self):
        original_name=self.line.Name
        pieces=replace_contours([self.line],DXFCAD.Contour.split(self.line.DXFData,[3,7]),"Split")
        self.assertEqual(len(pieces),3)
        self.assertEqual(pieces[0].Name,original_name)
        self.assertEqual(pieces[0].DXFHandle,"AB")
        self.assertEqual([round(p.Shape.Length,7) for p in pieces],[3,4,3])
        after=[p.DXFData for p in pieces]
        self.doc.undo(); self.doc.recompute()
        self.assertEqual(self.doc.getObject(original_name).DXFData,LINE)
        self.assertEqual(len([o for o in self.doc.Objects if "DXFData" in o.PropertiesList]),2)
        self.doc.redo(); self.doc.recompute()
        self.assertEqual(len([o for o in self.doc.Objects if "DXFData" in o.PropertiesList]),4)
        out=Path(self.folder.name)/"out.dxf"
        export_dxf(out,self.doc)
        parsed=DXFCAD.DXFDocument.read(str(out))
        self.assertEqual(len(parsed.entities),4)
        self.assertEqual(parsed.warnings,[])
        self.assertEqual(len({e.handle for e in parsed.entities}),4)
        saved=Path(self.folder.name)/"contours.FCStd"
        self.doc.saveAs(str(saved))
        FreeCAD.closeDocument(self.doc.Name)
        self.source.unlink()
        script="""import FreeCAD,Part,sys
sys.path.insert(0,sys.argv[1]); sys.path.insert(0,sys.argv[2])
import DXFCAD,dxfcad_objects
from dxfcad_export import export_dxf
doc=FreeCAD.openDocument(sys.argv[3])
objects=[o for o in doc.Objects if 'DXFData' in o.PropertiesList]
assert len(objects)==4
for obj in objects:
    obj.Shape=Part.Shape(); obj.touch()
doc.recompute()
assert all(not o.Shape.isNull() and o.Shape.isValid() for o in objects)
export_dxf(sys.argv[4],doc)
print('Contour cold restore passed')
"""
        restored=Path(self.folder.name)/"restored.dxf"
        result=subprocess.run([sys.executable,"-c",script,str(module_directory),str(Path(__file__).resolve().parents[1]),str(saved),str(restored)],capture_output=True,text=True,timeout=60)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertEqual(out.read_bytes(),restored.read_bytes())

    def test_type_changing_break_preserves_source_metadata(self):
        original_name=self.circle.Name
        pieces=replace_contours([self.circle],DXFCAD.Contour.break_between(self.circle.DXFData,1,4),"Break")
        self.assertEqual(len(pieces),1)
        self.assertEqual(pieces[0].DXFType,"ARC")
        self.assertIsNone(self.doc.getObject(original_name))
        self.assertIn("keep circle",pieces[0].DXFData)
        self.assertAlmostEqual(pieces[0].Shape.Length,10*3.141592653589793-3,places=6)
        self.doc.undo(); self.doc.recompute()
        self.assertEqual(self.doc.getObject(original_name).DXFData,CIRCLE)
        self.doc.redo(); self.doc.recompute()
        out=Path(self.folder.name)/"break.dxf"
        export_dxf(out,self.doc)
        types=[e.type for e in DXFCAD.DXFDocument.read(str(out)).entities]
        self.assertEqual(types,["LINE","ARC"])

    def test_plan_preview_matches_applied_drawing_and_is_one_shot(self):
        names={o.Name for o in self.doc.Objects}
        source=self.line.DXFSourceRoot.DXFSourceData
        records=DXFCAD.Contour.split(LINE,[3,7])
        plan=DXFCAD.prepare_contour_edit([self.line],records)
        self.assertTrue(plan.preserves_first)
        self.assertEqual([raw for obj,raw in plan.preview_records],records)
        self.assertEqual({o.Name for o in self.doc.Objects},names)
        self.assertEqual(self.line.DXFData,LINE)
        self.assertEqual(plan.root.DXFSourceData,source)
        predicted=plan.drawing_bytes
        pieces=apply_contour_plan(plan,"Apply prepared split")
        self.assertEqual(len(plan.created_objects),2)
        self.assertEqual(pieces[0],self.line)
        self.assertEqual(DXFCAD.current_drawing_bytes(plan.root),predicted)
        with self.assertRaisesRegex(ValueError,"already been applied"):
            plan.apply()

    def test_stale_selected_and_unselected_records_are_rejected(self):
        for changed in (self.line,self.circle):
            plan=DXFCAD.prepare_contour_edit([self.line],DXFCAD.Contour.split(LINE,[3]))
            before=changed.DXFData
            changed.DXFData=DXFCAD.parse_entity(before).translated(DXFCAD.Vector3D(0,1,0))
            names={o.Name for o in self.doc.Objects}
            with self.assertRaisesRegex(ValueError,"changed"):
                plan.apply()
            self.assertEqual({o.Name for o in self.doc.Objects},names)
            self.assertEqual(changed.DXFData,DXFCAD.parse_entity(before).translated(DXFCAD.Vector3D(0,1,0)))
            changed.DXFData=before
        plan=DXFCAD.prepare_contour_edit([self.line],DXFCAD.Contour.split(LINE,[3]))
        self.doc.removeObject(self.line.Name)
        with self.assertRaisesRegex(ValueError,"changed"):
            plan.apply()

    def test_invalid_selection_and_result_leave_drawing_unchanged(self):
        from dxfcad_edit import create_line
        root=DXFCAD.create_drawing(self.doc,DXFCAD.DXFDocument.empty(4))
        foreign=create_line(self.doc,DXFCAD.Vector3D(),DXFCAD.Vector3D(1,0,0),root)
        names={o.Name for o in self.doc.Objects}
        snapshot=self.line.DXFSourceRoot.DXFSourceData
        for objects,records in (([],[LINE]),([self.line],[]),([self.line,self.line],[LINE]),
                                ([self.line,foreign],[LINE]),([self.line],["0\nCIRCLE\n40\n-1\n"])):
            with self.assertRaises(ValueError): DXFCAD.prepare_contour_edit(objects,records)
        self.assertEqual({o.Name for o in self.doc.Objects},names)
        self.assertEqual(self.line.DXFData,LINE)
        self.assertEqual(self.line.DXFSourceRoot.DXFSourceData,snapshot)

    def test_plan_rejects_corrupt_unselected_source_slot(self):
        self.circle.DXFSourceIndex=self.line.DXFSourceIndex
        with self.assertRaisesRegex(ValueError,"duplicate"):
            DXFCAD.prepare_contour_edit([self.line],DXFCAD.Contour.split(LINE,[3]))
        self.assertEqual(self.line.DXFData,LINE)

    def test_stale_source_snapshot_and_placement_are_rejected(self):
        root=self.line.DXFSourceRoot
        plan=DXFCAD.prepare_contour_edit([self.line],DXFCAD.Contour.split(LINE,[3]))
        source=DXFCAD.drawing_document(root)
        DXFCAD.set_drawing_source(root,DXFCAD.DXFDocument.parse(DXFCAD.set_layer(source.serialize(),"Extra",3,True)))
        with self.assertRaisesRegex(ValueError,"changed"):
            plan.apply()
        self.assertEqual(self.line.DXFData,LINE)
        plan=DXFCAD.prepare_contour_edit([self.line],DXFCAD.Contour.split(LINE,[3]))
        self.circle.Placement=FreeCAD.Placement(FreeCAD.Vector(1,0,0),FreeCAD.Rotation())
        with self.assertRaisesRegex(ValueError,"Placement"):
            plan.apply()

    def test_rollback_on_piece_creation_failure(self):
        original_names={o.Name for o in self.doc.Objects}
        with patch.object(DXFCAD,"create_feature",side_effect=RuntimeError("injected failure")):
            with self.assertRaisesRegex(RuntimeError,"injected"):
                replace_contours([self.line],DXFCAD.Contour.split(LINE,[3]),"Split")
        self.assertEqual(self.line.DXFData,LINE)
        self.assertEqual({o.Name for o in self.doc.Objects},original_names)
        self.assertAlmostEqual(self.line.Shape.Length,10)

    def test_rollback_after_a_piece_has_been_created(self):
        original_names={o.Name for o in self.doc.Objects}
        original_create=DXFCAD.create_feature
        calls=[]
        def create(*args):
            calls.append(args)
            if len(calls)==2: raise RuntimeError("injected second-piece failure")
            return original_create(*args)
        with patch.object(DXFCAD,"create_feature",side_effect=create):
            with self.assertRaisesRegex(RuntimeError,"second-piece"):
                replace_contours([self.line],DXFCAD.Contour.split(LINE,[3,7]),"Split")
        self.assertEqual(len(calls),2)
        self.assertEqual(self.line.DXFData,LINE)
        self.assertEqual({o.Name for o in self.doc.Objects},original_names)
        self.assertAlmostEqual(self.line.Shape.Length,10)

    def test_join_close_and_trim(self):
        from dxfcad_edit import create_line
        a=create_line(self.doc,DXFCAD.Vector3D(0,5,0),DXFCAD.Vector3D(5,5,0))
        b=create_line(self.doc,DXFCAD.Vector3D(5,10,0),DXFCAD.Vector3D(5,5,0))
        joined=replace_contours([a,b],[DXFCAD.Contour.join([a.DXFData,b.DXFData])],"Join",False)[0]
        self.assertEqual(joined.DXFType,"LWPOLYLINE")
        closed=replace_contours([joined],[DXFCAD.Contour.close(joined.DXFData)],"Close")[0]
        self.assertTrue(DXFCAD.read_lwpolyline(closed.DXFData).closed)
        boundaries=[DXFCAD.create_line(DXFCAD.Vector3D(x,-2,0),DXFCAD.Vector3D(x,2,0)) for x in (3,7)]
        pieces=replace_contours([self.line],DXFCAD.Contour.trim_interior(LINE,boundaries,5),"Trim interior")
        self.assertEqual(len(pieces),2)
        self.assertEqual([round(p.Shape.Length) for p in pieces],[3,3])


if __name__=="__main__": unittest.main()
