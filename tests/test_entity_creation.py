"""Single-entity creation: validation, source context, grouping and transactions."""
import FreeCAD
import Part
from pathlib import Path
import sys
import tempfile
import base64
import unittest
from unittest.mock import patch

module_directory=Path(sys.argv.pop(1)).resolve()
sys.path.insert(0,str(module_directory))
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import DXFCAD
assert Path(DXFCAD.__file__).parent==module_directory
from dxfcad_edit import transaction,_create_entity
from dxfcad_objects import DXFObjectProxy
from dxfcad_import import default_text_font

LINE="0\nLINE\n10\n0\n20\n0\n11\n10\n21\n0\n1001\nTEST\n1000\nretain new entity\n"


def finish(obj):
    if obj.TypeId=="Part::FeaturePython": DXFObjectProxy(obj)
    obj.Document.recompute()


class EntityCreationTests(unittest.TestCase):
    def setUp(self):
        self.doc=FreeCAD.newDocument("EntityCreation")

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()): FreeCAD.closeDocument(name)

    def test_create_in_empty_document_and_reuse_sole_drawing(self):
        records=[LINE,DXFCAD.create_circle(DXFCAD.Vector3D(4,5,0),2),
                 DXFCAD.create_arc(DXFCAD.Vector3D(),3,0,90),
                 DXFCAD.create_rectangle(DXFCAD.Vector3D(),DXFCAD.Vector3D(2,3,0)),
                 DXFCAD.create_text("New text",1,DXFCAD.Vector3D(),False,0)]
        objects=[]
        with transaction(self.doc,"Create five entities"):
            for raw in records:
                obj=DXFCAD.create_entity(self.doc,raw,font_file=default_text_font())
                finish(obj); objects.append(obj)
        root=objects[0].DXFSourceRoot
        self.assertEqual(root.SourceUnitCode,4)
        self.assertTrue(all(o.DXFSourceRoot==root and o.DXFSourceIndex==-1 for o in objects))
        self.assertEqual([o.DXFData for o in objects],records)
        self.assertTrue(all(o.Shape.isValid() and not o.Shape.isNull() for o in objects))
        self.assertEqual(len(root.Group),1)
        self.assertEqual(root.Group[0].DXFLayerName,"0")
        self.assertTrue(all(o in root.Group[0].Group for o in objects))
        self.assertEqual(len(DXFCAD.drawing_document(root).entities),0)
        self.assertEqual(len(DXFCAD.current_drawing(root).entities),5)

    def test_add_new_layer_preserves_source_and_provenance_and_undo_redo(self):
        source=DXFCAD.DXFDocument.parse("0\nSECTION\n2\nENTITIES\n0\nSPLINE\n999\nopaque\n0\nENDSEC\n0\nEOF\n")
        root=DXFCAD.create_drawing(self.doc,source,source_file="original.dxf")
        snapshot=root.DXFSourceData
        raw=DXFCAD.entity_properties(LINE,"Cutting",-1,True)
        with transaction(self.doc,"Add Cutting entity"):
            obj=DXFCAD.add_entity(root,raw)
            finish(obj)
        name=obj.Name
        self.assertEqual(root.SourceFile,"original.dxf")
        self.assertEqual(DXFCAD.drawing_document(root).layers["Cutting"].color,7)
        self.assertEqual(obj.DXFData,raw)
        self.assertIn(obj,next(g for g in root.Group if g.DXFLayerName=="Cutting").Group)
        expected=DXFCAD.current_drawing_bytes(root)
        self.assertIn(b"opaque",expected)
        self.doc.undo(); self.doc.recompute()
        self.assertIsNone(self.doc.getObject(name))
        self.assertEqual(root.DXFSourceData,snapshot)
        self.assertEqual(root.Group,[])
        self.doc.redo(); self.doc.recompute()
        self.assertEqual(DXFCAD.current_drawing_bytes(root),expected)

    def test_invalid_records_leave_empty_document_unchanged(self):
        for raw in ("0\nCIRCLE\n40\n-1\n",LINE+LINE,
                    LINE.replace("0\nLINE\n","0\nLINE\n5\nAB\n",1),
                    LINE+"330\nF\n",LINE+"67\n1\n",
                    DXFCAD.entity_properties(LINE,"Bad|Name",-1,True)):
            with self.assertRaises(ValueError): DXFCAD.create_entity(self.doc,raw)
            self.assertEqual(self.doc.Objects,[])

    def test_ambiguous_foreign_and_legacy_roots(self):
        left=DXFCAD.create_drawing(self.doc,DXFCAD.DXFDocument.empty(4))
        legacy=self.doc.addObject("App::DocumentObjectGroup","Legacy")
        legacy.addProperty("App::PropertyString","DXFSourceData","DXF")
        legacy.DXFSourceData=base64.b64encode(DXFCAD.DXFDocument.empty(4).serialize_bytes()).decode("ascii")
        before={o.Name for o in self.doc.Objects}
        with self.assertRaisesRegex(ValueError,"multiple drawings"): DXFCAD.create_entity(self.doc,LINE)
        foreign=FreeCAD.newDocument("ForeignCreation")
        with self.assertRaisesRegex(ValueError,"active document"):
            DXFCAD.create_entity(foreign,LINE,root=left)
        self.assertEqual({o.Name for o in self.doc.Objects},before)
        with transaction(self.doc,"Create in legacy root"):
            obj=DXFCAD.add_entity(legacy,LINE); finish(obj)
        self.assertEqual(obj.DXFSourceRoot,legacy)
        self.assertEqual(legacy.TypeId,"App::DocumentObjectGroup")
        self.assertEqual(len(DXFCAD.current_drawing(left).entities),0)
        self.assertEqual(len(DXFCAD.current_drawing(legacy).entities),1)

    def test_version_placement_and_layer_ambiguity_are_validated_before_mutation(self):
        old=DXFCAD.DXFDocument.parse("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1009\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nENDSEC\n0\nEOF\n")
        root=DXFCAD.create_drawing(self.doc,old)
        before={o.Name for o in self.doc.Objects}
        with self.assertRaisesRegex(ValueError,"R2000"): DXFCAD.add_entity(root,LINE)
        self.assertEqual({o.Name for o in self.doc.Objects},before)
        modern=DXFCAD.create_drawing(self.doc,DXFCAD.DXFDocument.empty(4))
        obj=DXFCAD.add_entity(modern,LINE); finish(obj)
        obj.Placement=FreeCAD.Placement(FreeCAD.Vector(1,0,0),FreeCAD.Rotation())
        with self.assertRaisesRegex(ValueError,"Placement"): DXFCAD.add_entity(modern,LINE)
        obj.Placement=FreeCAD.Placement()
        duplicate=self.doc.addObject("App::DocumentObjectGroup","DuplicateLayer")
        duplicate.addProperty("App::PropertyString","DXFLayerName","DXF")
        duplicate.DXFLayerName="0"; modern.addObject(duplicate)
        before={o.Name for o in self.doc.Objects}
        with self.assertRaisesRegex(ValueError,"duplicate layer groups"): DXFCAD.add_entity(modern,LINE)
        self.assertEqual({o.Name for o in self.doc.Objects},before)

    def test_insert_resolves_context_from_target_drawing(self):
        block="0\nBLOCK\n2\nSymbol\n10\n0\n20\n0\n0\nLINE\n10\n0\n20\n0\n11\n2\n21\n0\n0\nENDBLK\n"
        source=DXFCAD.DXFDocument.parse("0\nSECTION\n2\nBLOCKS\n"+block+"0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nENDSEC\n0\nEOF\n")
        root=DXFCAD.create_drawing(self.doc,source)
        with transaction(self.doc,"Add block instance"):
            obj=DXFCAD.add_entity(root,"0\nINSERT\n2\nSymbol\n10\n10\n20\n20\n")
            finish(obj)
        self.assertEqual(obj.DXFContext,block)
        self.assertAlmostEqual(obj.Shape.Length,2)
        self.assertEqual(obj.DXFSourceIndex,-1)
        self.assertEqual(len(DXFCAD.current_drawing(root).entities),1)

    def test_creation_and_presentation_failure_roll_back_entire_new_graph(self):
        factory=DXFCAD.create_feature
        def fail_after_create(*args):
            factory(*args)
            raise RuntimeError("injected entity failure")
        with patch.object(DXFCAD,"create_feature",side_effect=fail_after_create):
            with self.assertRaisesRegex(RuntimeError,"entity failure"):
                _create_entity(self.doc,LINE,None,"LINE")
        self.assertEqual(self.doc.Objects,[])
        with patch("dxfcad_edit._check_display",side_effect=RuntimeError("presentation failure")):
            with self.assertRaisesRegex(RuntimeError,"presentation failure"):
                _create_entity(self.doc,LINE,None,"LINE")
        self.assertEqual(self.doc.Objects,[])


if __name__=="__main__": unittest.main()
