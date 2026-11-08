"""Native and compatibility BLOCK/INSERT rendering, editing and persistence."""
import FreeCAD
import Part
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

module_directory = Path(sys.argv.pop(1)).resolve()
sys.path.insert(0, str(module_directory))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import DXFCAD
assert Path(DXFCAD.__file__).parent == module_directory
from dxfcad_import import import_dxf, default_text_font
from dxfcad_export import export_dxf
from dxfcad_edit import move_entities, copy_entities

BLOCK = ("0\nBLOCK\n2\nSymbol\n70\n0\n10\n1\n20\n2\n"
         "0\nLINE\n10\n1\n20\n2\n11\n5\n21\n2\n"
         "0\nCIRCLE\n10\n3\n20\n4\n40\n1\n0\nENDBLK\n")
INSERT = ("0\nINSERT\n5\nAB\n2\nSymbol\n10\n10\n20\n20\n41\n2\n42\n3\n50\n90\n"
          "1001\nSynthetic\n1000\nPreserve insert\n")
DRAWING = "0\nSECTION\n2\nBLOCKS\n"+BLOCK+"0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"+INSERT+"0\nENDSEC\n0\nEOF\n"


class InsertTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.source = Path(self.folder.name)/"insert.dxf"
        self.source.write_text(DRAWING)
        self.doc = import_dxf(self.source)
        self.obj = next(o for o in self.doc.Objects if "DXFData" in o.PropertiesList)

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        self.folder.cleanup()

    def test_child_styles_layer_changes_and_restore(self):
        layers=("0\nLAYER\n2\nHost\n62\n2\n0\nLAYER\n2\nChild\n62\n3\n"
                "0\nLAYER\n2\nHidden\n62\n-5\n")
        children=("0\nLINE\n10\n0\n20\n0\n11\n1\n21\n0\n"
                  "0\nLINE\n8\nChild\n10\n0\n20\n1\n11\n1\n21\n1\n"
                  "0\nLINE\n62\n0\n10\n0\n20\n2\n11\n1\n21\n2\n"
                  "0\nLINE\n8\nHidden\n10\n0\n20\n3\n11\n1\n21\n3\n")
        blocks="0\nBLOCK\n2\nStyled\n10\n0\n20\n0\n"+children+"0\nENDBLK\n"
        raw="0\nINSERT\n5\nCD\n2\nStyled\n8\nHost\n62\n1\n10\n0\n20\n0\n"
        source=("0\nSECTION\n2\nTABLES\n"+layers+"0\nENDSEC\n0\nSECTION\n2\nBLOCKS\n"+
                blocks+"0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"+raw+"0\nENDSEC\n0\nEOF\n")
        path=Path(self.folder.name)/'styled.dxf'
        path.write_text(source)
        doc=import_dxf(path)
        obj=next(o for o in doc.Objects if 'DXFData' in o.PropertiesList)
        root=obj.DXFSourceRoot
        self.assertEqual(len(obj.Shape.Edges),3)
        colors,faces=DXFCAD.block_display_colors(obj)
        self.assertEqual(colors,[[1,1,0],[0,1,0],[1,0,0]])
        self.assertEqual(faces,[])
        self.assertEqual(DXFCAD.current_drawing_bytes(root),DXFCAD.Drawing.parse(source).serialize_bytes())
        doc.UndoMode=1
        doc.openTransaction('Change child layer')
        DXFCAD.set_layer_properties(root,'Child',5,True)
        doc.recompute()
        doc.commitTransaction()
        self.assertEqual(DXFCAD.block_display_colors(obj)[0][1],[0,0,1])
        doc.undo(); doc.recompute()
        self.assertEqual(DXFCAD.block_display_colors(obj)[0][1],[0,1,0])
        doc.redo(); doc.recompute()
        self.assertEqual(DXFCAD.block_display_colors(obj)[0][1],[0,0,1])
        saved=Path(self.folder.name)/'styled.FCStd'
        doc.saveAs(str(saved))
        FreeCAD.closeDocument(doc.Name)
        path.unlink()
        script="""import FreeCAD, Part
import sys
sys.path.insert(0,sys.argv[1]); sys.path.insert(0,sys.argv[2])
import DXFCAD, dxfcad_objects
doc=FreeCAD.openDocument(sys.argv[3])
obj=next(o for o in doc.Objects if 'DXFData' in o.PropertiesList)
obj.Shape=Part.Shape(); obj.touch(); doc.recompute()
assert obj.Shape.isValid() and len(obj.Shape.Edges)==3
assert DXFCAD.block_display_colors(obj)[0]==[[1,1,0],[0,0,1],[1,0,0]]
FreeCAD.closeDocument(doc.Name)
"""
        completed=subprocess.run([sys.executable,'-c',script,str(module_directory),
            str(Path(__file__).resolve().parents[1]),str(saved)],capture_output=True,text=True,timeout=60)
        self.assertEqual(completed.returncode,0,completed.stdout+completed.stderr)

    def test_legacy_block_text_keeps_source_bytes(self):
        source=('0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$DWGCODEPAGE\n3\nANSI_1252\n0\nENDSEC\n'
            '0\nSECTION\n2\nBLOCKS\n0\nBLOCK\n2\nLegacy\n10\n0\n20\n0\n'
            '0\nTEXT\n10\n0\n20\n0\n40\n1\n1\n90\u00b0 \u20ac\n0\nENDBLK\n0\nENDSEC\n'
            '0\nSECTION\n2\nENTITIES\n0\nINSERT\n2\nLegacy\n10\n0\n20\n0\n0\nENDSEC\n0\nEOF\n').encode('cp1252')
        path=Path(self.folder.name)/'legacy.dxf'
        path.write_bytes(source)
        doc=import_dxf(path)
        obj=next(o for o in doc.Objects if 'DXFData' in o.PropertiesList)
        self.assertIn('90\u00b0 \u20ac',obj.DXFContext)
        self.assertTrue(obj.Shape.isValid())
        self.assertFalse(obj.Shape.isNull())
        self.assertEqual(DXFCAD.current_drawing_bytes(obj.DXFSourceRoot),source)
        self.assertEqual(path.read_bytes(),source)

    def test_affine_geometry_preview_and_recovery(self):
        obj = self.obj
        self.assertEqual(obj.DXFType,"INSERT")
        self.assertEqual(obj.DXFContext,BLOCK)
        if DXFCAD.NATIVE_DOCUMENT_OBJECTS:
            self.assertEqual(obj.TypeId,"DXFCAD::InsertObject")
            self.assertNotIn("Proxy",obj.PropertiesList)
        self.assertTrue(obj.Shape.isValid())
        self.assertEqual(len(obj.Shape.Edges),2)
        # Circle at block (3,4), relative to base (1,2), becomes an ellipse
        # centered at (4,24), with X radius 3 and Y radius 2.
        bounds=obj.Shape.optimalBoundingBox(False)
        self.assertAlmostEqual(bounds.XMin,1,places=5)
        self.assertAlmostEqual(bounds.XMax,10,places=5)
        self.assertAlmostEqual(bounds.YMin,20,places=5)
        self.assertAlmostEqual(bounds.YMax,28,places=5)
        preview=DXFCAD.entity_preview_shape(obj.DXFData,"",False,obj.DXFContext)
        self.assertTrue(preview.isValid())
        self.assertAlmostEqual(preview.optimalBoundingBox(False).XMin,bounds.XMin,places=5)
        obj.DXFContext=BLOCK.replace("Symbol","Missing")
        self.doc.recompute()
        self.assertTrue(obj.Shape.isNull())
        self.assertTrue(obj.DisplayStatus.startswith("Error:"))
        obj.DXFContext=BLOCK
        self.doc.recompute()
        self.assertTrue(obj.Shape.isValid())

    def test_edit_copy_undo_export(self):
        self.doc.UndoMode=1
        move_entities([self.obj],DXFCAD.Vector3D(5,6,0))
        self.assertAlmostEqual(self.obj.Shape.optimalBoundingBox(False).XMin,6,places=5)
        self.doc.undo(); self.doc.recompute()
        self.assertEqual(self.obj.DXFData,INSERT)
        self.doc.redo(); self.doc.recompute()
        self.assertAlmostEqual(self.obj.Shape.optimalBoundingBox(False).XMin,6,places=5)
        copies=copy_entities([self.obj],DXFCAD.Vector3D(20,0,0))
        self.assertEqual(len(copies),1)
        self.assertEqual(copies[0].DXFContext,BLOCK)
        self.assertAlmostEqual(copies[0].Shape.optimalBoundingBox(False).XMin,26,places=5)
        output=Path(self.folder.name)/"out.dxf"
        export_dxf(output,self.doc)
        parsed=DXFCAD.Drawing.read(str(output))
        self.assertEqual(len(parsed.entities),2)
        self.assertEqual(parsed.warnings,[])
        self.assertNotEqual(parsed.entities[0].handle,parsed.entities[1].handle)
        self.assertIn(BLOCK,parsed.serialize())
        self.assertIn("Preserve insert",parsed.entities[0].raw)

    def test_source_free_cold_restore(self):
        saved=Path(self.folder.name)/"insert.FCStd"
        self.doc.saveAs(str(saved))
        FreeCAD.closeDocument(self.doc.Name)
        self.source.unlink()
        script=Path(self.folder.name)/"restore.py"
        script.write_text(
            "import FreeCAD, Part, sys\n"
            f"sys.path.insert(0,{str(module_directory)!r})\n"
            f"sys.path.insert(0,{str(Path(__file__).resolve().parents[1])!r})\n"
            "import DXFCAD, dxfcad_objects\n"
            f"assert str(__import__('pathlib').Path(DXFCAD.__file__).parent)=={str(module_directory)!r}\n"
            f"doc=FreeCAD.openDocument({str(saved)!r})\n"
            "obj=next(o for o in doc.Objects if 'DXFData' in o.PropertiesList)\n"
            "obj.Shape=Part.Shape(); obj.touch(); doc.recompute()\n"
            "assert not obj.Shape.isNull(), (obj.DisplayStatus, obj.State, getattr(obj,'Proxy',None), obj.DXFContext)\n"
            "assert obj.Shape.isValid() and len(obj.Shape.Edges)==2, obj.DisplayStatus\n"
            "assert abs(obj.Shape.optimalBoundingBox(False).XMin-1)<1e-5\n"
            "from dxfcad_export import export_dxf\n"
            f"export_dxf({str(Path(self.folder.name)/'restored.dxf')!r},doc)\n"
            "print('INSERT cold restore passed')\n")
        result=subprocess.run([sys.executable,str(script)],capture_output=True,text=True,timeout=60)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn("INSERT cold restore passed",result.stdout)

    def test_nested_widths_and_text(self):
        text_block=("0\nBLOCK\n2\nInner\n10\n0\n20\n0\n"
                    "0\nTEXT\n10\n0\n20\n0\n40\n1\n1\nBlock\n"
                    "0\nLWPOLYLINE\n90\n2\n43\n2\n10\n0\n20\n0\n10\n4\n20\n0\n0\nENDBLK\n")
        outer=("0\nBLOCK\n2\nOuter\n10\n0\n20\n0\n"
               "0\nINSERT\n2\nInner\n10\n0\n20\n5\n41\n-2\n42\n3\n0\nENDBLK\n")
        doc=DXFCAD.Drawing.parse("0\nSECTION\n2\nBLOCKS\n"+text_block+outer+
            "0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nINSERT\n2\nOuter\n10\n0\n20\n0\n0\nENDSEC\n0\nEOF\n")
        self.assertEqual(doc.warnings,[])
        obj=DXFCAD.create_feature(self.doc,doc.entities[0],default_text_font())
        if obj.TypeId=="Part::FeaturePython":
            from dxfcad_objects import EntityProxy
            EntityProxy(obj)
        self.doc.recompute()
        self.assertTrue(obj.Shape.isValid(),obj.DisplayStatus)
        self.assertAlmostEqual(obj.Shape.Area,48,places=5)
        self.assertGreater(len(obj.Shape.Edges),4)


if __name__=="__main__":
    unittest.main()
