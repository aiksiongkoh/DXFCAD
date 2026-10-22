"""Drawing container factory, native lifecycle, grouping and source-free restore."""
import FreeCAD
import Part
import base64
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
from dxfcad_edit import create_line, drawing_roots, apply_properties, transaction
from dxfcad_export import export_dxf

DRAWING=("0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n"
         "0\nSECTION\n2\nENTITIES\n0\nLINE\n5\nAB\n8\nOutline\n10\n0\n20\n0\n11\n10\n21\n0\n"
         "1001\nSynthetic\n1000\nretain drawing\n0\nSPLINE\n5\nBC\n999\nRetain unsupported entity\n0\nENDSEC\n0\nEOF\n")


class DrawingObjectTests(unittest.TestCase):
    def setUp(self):
        self.folder=tempfile.TemporaryDirectory(dir=module_directory)
        self.source=Path(self.folder.name)/"drawing.dxf"
        self.source.write_text(DRAWING)
        self.doc=import_dxf(self.source)
        self.doc.UndoMode=1
        self.root=drawing_roots(self.doc)[0]
        self.line=next(o for o in self.doc.Objects if "DXFData" in o.PropertiesList)

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()): FreeCAD.closeDocument(name)
        self.folder.cleanup()

    def test_factory_properties_and_group_membership(self):
        expected="DXFCAD::DXFDocumentObject" if DXFCAD.NATIVE_DOCUMENT_OBJECTS else "App::DocumentObjectGroup"
        self.assertEqual(self.root.TypeId,expected)
        self.assertTrue(self.root.isDerivedFrom("App::DocumentObjectGroup"))
        self.assertNotIn("Proxy",self.root.PropertiesList)
        self.assertEqual(self.root.SourceFile,str(self.source.resolve()))
        self.assertEqual(self.root.SourceUnitCode,4)
        self.assertEqual(len(self.root.ImportWarnings),1)
        self.assertIn("SPLINE",self.root.ImportWarnings[0])
        self.assertEqual(self.line.DXFSourceRoot,self.root)
        self.assertEqual(self.line.DXFSourceIndex,0)
        self.assertEqual(len(self.root.Group),1)
        self.assertIn(self.line,self.root.Group[0].Group)
        self.assertEqual(DXFCAD.drawing_document(self.root).serialize_bytes(),DXFCAD.DXFDocument.parse(DRAWING).serialize_bytes())

    def test_new_drawing_and_metadata_undo(self):
        doc=FreeCAD.newDocument("NewDrawing")
        line=create_line(doc,DXFCAD.Vector3D(),DXFCAD.Vector3D(1,0,0))
        root=drawing_roots(doc)[0]
        self.assertEqual(root.TypeId,self.root.TypeId)
        self.assertEqual(root.SourceUnitCode,4)
        self.assertEqual(root.SourceFile,"")
        self.assertEqual(root.ImportWarnings,[])
        snapshot=root.DXFSourceData
        apply_properties([line],"Cutting",-1,True,layer_color=3)
        self.assertIn("Cutting",DXFCAD.drawing_document(root).layers)
        self.assertEqual(line.DXFLayer,"Cutting")
        doc.undo(); doc.recompute()
        self.assertEqual(root.DXFSourceData,snapshot)
        self.assertEqual(line.DXFLayer,"0")
        doc.redo(); doc.recompute()
        self.assertEqual(line.DXFLayer,"Cutting")
        self.assertEqual(line.DXFSourceRoot,root)

    def test_current_drawing_merges_edits_additions_and_deletions(self):
        snapshot=self.root.DXFSourceData
        self.line.DXFData=DXFCAD.parse_entity(self.line.DXFData).translated(DXFCAD.Vector3D(0,3,0))
        added=create_line(self.doc,DXFCAD.Vector3D(20,0,0),DXFCAD.Vector3D(25,0,0),self.root)
        other=DXFCAD.create_drawing(self.doc,DXFCAD.DXFDocument.empty(4))
        create_line(self.doc,DXFCAD.Vector3D(),DXFCAD.Vector3D(100,0,0),other)
        current=DXFCAD.current_drawing(self.root)
        self.assertEqual(len(current.entities),2)
        data=DXFCAD.current_drawing_bytes(self.root)
        self.assertIn(b"retain drawing",data)
        self.assertIn(b"Retain unsupported entity",data)
        self.assertEqual(self.root.DXFSourceData,snapshot)
        output=Path(self.folder.name)/"current.dxf"
        export_dxf(output,self.doc,self.root)
        self.assertEqual(output.read_bytes(),data)
        with transaction(self.doc,"Delete original entity"):
            self.doc.removeObject(self.line.Name)
        self.assertEqual(len(DXFCAD.current_drawing(self.root).entities),1)
        self.doc.undo(); self.doc.recompute()
        self.assertEqual(len(DXFCAD.current_drawing(self.root).entities),2)
        self.assertEqual(added.DXFSourceIndex,-1)

    def test_export_rejects_invalid_ownership_and_placement_without_overwriting(self):
        added=create_line(self.doc,DXFCAD.Vector3D(),DXFCAD.Vector3D(5,0,0),self.root)
        output=Path(self.folder.name)/"protected.dxf"
        output.write_bytes(b"keep existing destination")
        for invalid in (-2,99,0):
            added.DXFSourceIndex=invalid
            with self.assertRaises(ValueError): export_dxf(output,self.doc,self.root)
            self.assertEqual(output.read_bytes(),b"keep existing destination")
        added.DXFSourceIndex=-1
        added.Placement.Base=FreeCAD.Vector(1,0,0)
        with self.assertRaisesRegex(ValueError,"Placement"):
            export_dxf(output,self.doc,self.root)
        self.assertEqual(output.read_bytes(),b"keep existing destination")

    def test_attachment_validates_before_mutating_and_is_undoable(self):
        obj=DXFCAD.create_feature(self.doc,DXFCAD.parse_entity("0\nLINE\n10\n0\n20\n0\n11\n1\n21\n0\n"),"")
        for index in (-2,99,0):
            with self.assertRaises(ValueError): DXFCAD.attach_entity(obj,self.root,index)
        self.assertIsNone(getattr(obj,"DXFSourceRoot",None))
        with transaction(self.doc,"Attach native entity"):
            DXFCAD.attach_entity(obj,self.root)
        self.assertEqual(obj.DXFSourceRoot,self.root)
        self.doc.undo(); self.doc.recompute()
        self.assertIsNone(getattr(obj,"DXFSourceRoot",None))
        self.doc.redo(); self.doc.recompute()
        self.assertEqual(obj.DXFSourceRoot,self.root)
        other=DXFCAD.create_drawing(self.doc,DXFCAD.DXFDocument.empty(4))
        with self.assertRaisesRegex(ValueError,"another DXF drawing"):
            DXFCAD.attach_entity(obj,other)
        foreign=FreeCAD.newDocument("ForeignDrawing")
        foreign_root=DXFCAD.create_drawing(foreign,DXFCAD.DXFDocument.empty(4))
        with self.assertRaisesRegex(ValueError,"same document"):
            DXFCAD.attach_entity(obj,foreign_root)
        self.assertEqual(obj.DXFSourceRoot,self.root)

    def test_legacy_group_remains_usable(self):
        legacy=self.doc.addObject("App::DocumentObjectGroup","LegacyDrawing")
        legacy.addProperty("App::PropertyString","DXFSourceData","DXF")
        legacy.DXFSourceData=base64.b64encode(DXFCAD.DXFDocument.empty(4).serialize_bytes()).decode("ascii")
        obj=create_line(self.doc,DXFCAD.Vector3D(0,5,0),DXFCAD.Vector3D(5,5,0),legacy)
        apply_properties([obj],"LegacyLayer",-1,True)
        self.assertEqual(legacy.TypeId,"App::DocumentObjectGroup")
        self.assertIn("LegacyLayer",DXFCAD.drawing_document(legacy).layers)
        out=Path(self.folder.name)/"legacy.dxf"
        export_dxf(out,self.doc,legacy)
        self.assertEqual(len(DXFCAD.DXFDocument.read(str(out)).entities),1)

    def test_import_failure_rolls_back_container_and_children(self):
        names={obj.Name for obj in self.doc.Objects}
        with patch.object(DXFCAD,"create_feature",side_effect=RuntimeError("Injected import failure")):
            with self.assertRaisesRegex(RuntimeError,"Injected"):
                import_dxf(self.source,self.doc)
        self.assertEqual({obj.Name for obj in self.doc.Objects},names)

    def test_cold_restore_preserves_group_links_and_export(self):
        if not DXFCAD.NATIVE_DOCUMENT_OBJECTS:
            self.skipTest("Native type autoload; compatibility persistence is covered by GUI tests")
        expected=DXFCAD.drawing_document(self.root).serialize_bytes()
        saved=Path(self.folder.name)/"drawing.FCStd"
        self.doc.saveAs(str(saved))
        FreeCAD.closeDocument(self.doc.Name)
        self.source.unlink()
        output=Path(self.folder.name)/"restored.dxf"
        script="""import FreeCAD,Part,sys
from pathlib import Path
sys.path.insert(0,sys.argv[1]); sys.path.insert(0,sys.argv[2])
if sys.argv[5]=='compatibility':
    import DXFCAD,dxfcad_objects
# Native types must be autoloaded by their saved names when opening the file.
doc=FreeCAD.openDocument(sys.argv[3])
import DXFCAD,dxfcad_objects
from dxfcad_edit import drawing_roots
from dxfcad_export import export_dxf
root=drawing_roots(doc)[0]
expected='DXFCAD::DXFDocumentObject' if DXFCAD.NATIVE_DOCUMENT_OBJECTS else 'App::DocumentObjectGroup'
assert root.TypeId==expected
assert 'Proxy' not in root.PropertiesList
obj=next(o for o in doc.Objects if 'DXFData' in o.PropertiesList)
assert obj.DXFSourceRoot==root and obj in root.Group[0].Group
assert root.SourceUnitCode==4 and len(root.ImportWarnings)==1
obj.Shape=Part.Shape(); obj.touch(); root.touch(); doc.recompute()
assert obj.Shape.isValid() and abs(obj.Shape.Length-10)<1e-7
export_dxf(sys.argv[4],doc,root)
FreeCAD.closeDocument(doc.Name)
print('Drawing container cold restore passed')
"""
        mode="native" if DXFCAD.NATIVE_DOCUMENT_OBJECTS else "compatibility"
        result=subprocess.run([sys.executable,"-c",script,str(module_directory),str(Path(__file__).resolve().parents[1]),str(saved),str(output),mode],capture_output=True,text=True,timeout=60)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertEqual(output.read_bytes(),expected)

    def test_native_source_validation_and_metadata_refresh(self):
        if not DXFCAD.NATIVE_DOCUMENT_OBJECTS: self.skipTest("Native container lifecycle")
        root=self.doc.addObject("DXFCAD::DXFDocumentObject","EmptyDrawing")
        self.doc.recompute()
        self.assertEqual(DXFCAD.drawing_document(root).insertion_units,4)
        for invalid in ("!!!","bm90IGEgRFhG"):
            root.DXFSourceData=invalid
            self.doc.recompute()
            self.assertIn("Invalid",root.State)
            with self.assertRaises((ValueError,RuntimeError)): DXFCAD.drawing_document(root)
        DXFCAD.set_drawing_source(root,DXFCAD.DXFDocument.empty(6))
        self.doc.recompute()
        self.assertNotIn("Invalid",root.State)
        self.assertEqual(root.SourceUnitCode,6)
        before=root.DXFSourceData
        with transaction(self.doc,"Update drawing source"):
            DXFCAD.set_drawing_source(root,DXFCAD.DXFDocument.empty(4))
        self.doc.undo(); self.doc.recompute()
        self.assertEqual(root.DXFSourceData,before)
        self.assertEqual(root.SourceUnitCode,6)


if __name__=="__main__": unittest.main()
