"""Drawing population constructs the same persistent domain graph in both builds."""
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
from dxfcad_edit import transaction
from dxfcad_import import import_dxf, initialize_population

DRAWING=("0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n"
         "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n3\n"
         "0\nLAYER\n2\nWalls\n62\n1\n0\nLAYER\n2\nHoles\n62\n-3\n"
         "0\nLAYER\n2\nUnused\n62\n5\n0\nENDTAB\n0\nENDSEC\n"
         "0\nSECTION\n2\nENTITIES\n"
         "0\nLINE\n5\nAB\n8\nWalls\n10\n0\n20\n0\n11\n10\n21\n0\n"
         "0\nSPLINE\n999\nkeep unsupported\n"
         "0\nLWPOLYLINE\n5\nAC\n8\nWalls\n90\n2\n10\n0\n20\n5\n10\n10\n20\n5\n"
         "0\nCIRCLE\n5\nAD\n8\nHoles\n10\n5\n20\n5\n40\n2\n0\nENDSEC\n0\nEOF\n")


def finish(population):
    initialize_population(population)
    population.root.Document.recompute()


class PopulationTests(unittest.TestCase):
    def setUp(self):
        self.doc=FreeCAD.newDocument("PopulationTests")
        self.source=DXFCAD.DXFDocument.parse(DRAWING)

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()): FreeCAD.closeDocument(name)

    def test_population_owns_and_groups_entities_without_altering_source(self):
        with transaction(self.doc,"Populate drawing"):
            result=DXFCAD.populate_drawing(self.doc,self.source,source_file="original.dxf",
                                           label="Floor plan",name="ImportedPlan")
            finish(result)
        self.assertEqual(result.root.Label,"Floor plan")
        self.assertEqual(result.root.Name,"ImportedPlan")
        self.assertEqual(result.root.SourceFile,"original.dxf")
        self.assertEqual(result.root.SourceUnitCode,4)
        self.assertEqual(result.warnings,self.source.warnings)
        self.assertEqual(result.root.ImportWarnings,result.warnings)
        self.assertEqual(set(result.layer_groups),{"Walls","Holes"})
        self.assertEqual(len(result.entities),3)
        expected=["DXFCAD::LineObject","DXFCAD::LWPolylineObject","DXFCAD::CircleObject"]
        for i,(obj,entity) in enumerate(zip(result.entities,result.source.entities)):
            self.assertEqual(obj.TypeId,expected[i] if DXFCAD.NATIVE_DOCUMENT_OBJECTS else "Part::FeaturePython")
            self.assertEqual(obj.DXFData,entity.raw)
            self.assertEqual(obj.DXFSourceRoot,result.root)
            self.assertEqual(obj.DXFSourceIndex,i)
            self.assertIn(obj,result.layer_groups[entity.layer].Group)
            self.assertTrue(obj.Shape.isValid())
            self.assertFalse(obj.Shape.isNull())
        for name,group in result.layer_groups.items():
            self.assertEqual(group.DXFLayerName,name)
            self.assertIn(group,result.root.Group)
        self.assertEqual(result.source.serialize_bytes(),self.source.serialize_bytes())
        self.assertEqual(DXFCAD.current_drawing_bytes(result.root),self.source.serialize_bytes())

    def test_population_is_one_undo_step_and_preserves_existing_objects(self):
        keep=self.doc.addObject("App::FeaturePython","Keep")
        with transaction(self.doc,"Populate drawing"):
            result=DXFCAD.populate_drawing(self.doc,self.source)
            finish(result)
        root_name=result.root.Name
        names={o.Name for o in self.doc.Objects}
        self.doc.undo(); self.doc.recompute()
        self.assertEqual([o.Name for o in self.doc.Objects],[keep.Name])
        self.doc.redo(); self.doc.recompute()
        self.assertEqual({o.Name for o in self.doc.Objects},names)
        root=self.doc.getObject(root_name)
        self.assertEqual(DXFCAD.current_drawing_bytes(root),self.source.serialize_bytes())

    def test_empty_and_unsupported_only_sources_still_have_a_root(self):
        for source in (DXFCAD.DXFDocument.empty(4),DXFCAD.DXFDocument.parse(
                "0\nSECTION\n2\nENTITIES\n0\nSPLINE\n999\nretain me\n0\nENDSEC\n0\nEOF\n")):
            with transaction(self.doc,"Populate empty drawing"):
                result=DXFCAD.populate_drawing(self.doc,source)
                finish(result)
            self.assertEqual(result.entities,[])
            self.assertEqual(result.layer_groups,{})
            self.assertEqual(result.root.Group,[])
            self.assertEqual(DXFCAD.current_drawing_bytes(result.root),source.serialize_bytes())

    def test_repeated_population_keeps_ownership_separate(self):
        results=[]
        with transaction(self.doc,"Populate two drawings"):
            for _ in range(2):
                result=DXFCAD.populate_drawing(self.doc,self.source)
                finish(result)
                results.append(result)
        self.assertNotEqual(results[0].root,results[1].root)
        self.assertNotEqual(results[0].layer_groups["Walls"],results[1].layer_groups["Walls"])
        for result in results:
            self.assertTrue(all(obj.DXFSourceRoot==result.root for obj in result.entities))
            self.assertEqual(DXFCAD.current_drawing_bytes(result.root),self.source.serialize_bytes())

    def test_failure_after_creation_rolls_back_population_in_existing_document(self):
        with transaction(self.doc,"Original drawing"):
            original=DXFCAD.populate_drawing(self.doc,self.source)
            finish(original)
        before={o.Name for o in self.doc.Objects}
        raw=DXFCAD.current_drawing_bytes(original.root)
        create=DXFCAD.create_feature
        calls=[]
        def failing_create(*args):
            calls.append(args)
            if len(calls)==2: raise RuntimeError("injected population failure")
            return create(*args)
        with patch.object(DXFCAD,"create_feature",side_effect=failing_create):
            with self.assertRaisesRegex(RuntimeError,"population failure"):
                with transaction(self.doc,"Fail population"):
                    DXFCAD.populate_drawing(self.doc,self.source)
        self.assertEqual(len(calls),2)
        self.assertEqual({o.Name for o in self.doc.Objects},before)
        self.assertEqual(DXFCAD.current_drawing_bytes(original.root),raw)

    def test_import_presentation_failure_rolls_back_created_document(self):
        before=set(FreeCAD.listDocuments())
        with tempfile.TemporaryDirectory(dir=module_directory) as temp:
            path=Path(temp)/"drawing.dxf"; path.write_text(DRAWING)
            created=[]
            def fail_presentation(population):
                created.extend(population.entities)
                raise RuntimeError("presentation failure")
            with patch("dxfcad_import.initialize_population",side_effect=fail_presentation):
                with self.assertRaisesRegex(RuntimeError,"presentation failure"):
                    import_dxf(path)
            self.assertEqual(len(created),3)
        self.assertEqual(set(FreeCAD.listDocuments()),before)


if __name__=="__main__": unittest.main()
