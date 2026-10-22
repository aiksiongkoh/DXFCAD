"""Cached DIMENSION picture display, source-free persistence, and export."""
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
from dxfcad_import import import_dxf, default_text_font
from dxfcad_export import export_dxf

assert Path(DXFCAD.__file__).parent == module_directory
DIM = "0\nDIMENSION\n5\nAB\n2\n*D1\n70\n32\n10\n0\n20\n2\n11\n5\n21\n2\n13\n0\n23\n0\n14\n10\n24\n0\n1001\nSynthetic\n1000\nPreserve dimension\n"
BLOCK = ("0\nBLOCK\n2\n*D1\n70\n1\n10\n0\n20\n0\n"
         "0\nLINE\n62\n0\n10\n0\n20\n2\n11\n10\n21\n2\n"
         "0\nSOLID\n62\n0\n10\n0\n20\n2\n11\n1\n21\n2.25\n12\n1\n22\n1.75\n13\n1\n23\n1.75\n"
         "0\nMTEXT\n62\n0\n10\n5\n20\n2.5\n40\n0.5\n71\n5\n1\n\\A1;10\n0\nENDBLK\n")
DRAWING = "0\nSECTION\n2\nBLOCKS\n"+BLOCK+"0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"+DIM+"0\nENDSEC\n0\nEOF\n"


class DimensionTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.source = Path(self.folder.name)/"dimension.dxf"
        self.source.write_text(DRAWING)
        self.doc = import_dxf(self.source)
        self.obj = next(o for o in self.doc.Objects if "DXFData" in o.PropertiesList)

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        self.folder.cleanup()

    def test_geometry_and_dependency_recovery(self):
        obj = self.obj
        self.assertEqual(obj.DXFType, "DIMENSION")
        self.assertEqual(obj.DXFData, DIM)
        self.assertEqual(obj.DXFContext, BLOCK)
        if DXFCAD.NATIVE_DOCUMENT_OBJECTS:
            self.assertEqual(obj.TypeId, "DXFCAD::DimensionObject")
            self.assertTrue(obj.isDerivedFrom("DXFCAD::TextObject"))
            self.assertNotIn("Proxy", obj.PropertiesList)
        self.assertTrue(obj.Shape.isValid())
        self.assertAlmostEqual(obj.Shape.Area, 0.25)
        self.assertGreater(obj.Shape.Length, 10)
        obj.TextFont = ""
        self.doc.recompute()
        self.assertTrue(obj.Shape.isNull())
        obj.TextFont = default_text_font()
        self.doc.recompute()
        self.assertTrue(obj.Shape.isValid())
        obj.DXFContext = BLOCK.replace("*D1", "*Wrong")
        self.doc.recompute()
        self.assertTrue(obj.Shape.isNull())
        self.assertTrue(obj.DisplayStatus.startswith("Error:"))
        obj.DXFContext = BLOCK
        self.doc.recompute()
        self.assertFalse(obj.Shape.isNull())

    def test_export_rejects_edits_and_retains_blocks(self):
        output = Path(self.folder.name)/"output.dxf"
        export_dxf(output, self.doc)
        self.assertEqual(output.read_bytes(), DXFCAD.DXFDocument.parse(DRAWING).serialize_bytes())
        original = output.read_bytes()
        self.obj.DXFData = DIM.replace("20\n2\n", "20\n3\n")
        with self.assertRaises(ValueError):
            export_dxf(output, self.doc)
        self.assertEqual(output.read_bytes(), original)

    def test_undo_and_cold_restore_without_source(self):
        self.doc.UndoMode = 1
        self.doc.openTransaction("Dimension display font")
        self.obj.TextFont = ""
        self.doc.recompute()
        self.doc.commitTransaction()
        self.assertTrue(self.obj.Shape.isNull())
        self.doc.undo()
        self.doc.recompute()
        self.assertFalse(self.obj.Shape.isNull())
        saved = Path(self.folder.name)/"dimension.FCStd"
        self.doc.saveAs(str(saved))
        FreeCAD.closeDocument(self.doc.Name)
        self.source.unlink()
        script = """import FreeCAD, Part
import sys
sys.path.insert(0,sys.argv[1])
sys.path.insert(0,sys.argv[2])
import DXFCAD, dxfcad_objects
from dxfcad_export import export_dxf
doc=FreeCAD.openDocument(sys.argv[3])
obj=next(o for o in doc.Objects if 'DXFData' in o.PropertiesList)
assert obj.DXFType == 'DIMENSION' and '*D1' in obj.DXFContext
obj.Shape=Part.Shape()
obj.touch()
doc.recompute()
assert not obj.Shape.isNull() and obj.Shape.isValid(), obj.DisplayStatus
assert abs(obj.Shape.Area-0.25) < 1e-7
export_dxf(sys.argv[4],doc)
FreeCAD.closeDocument(doc.Name)
"""
        output = Path(self.folder.name)/"restored.dxf"
        result = subprocess.run([sys.executable,"-c",script,str(module_directory),
                                 str(Path(__file__).resolve().parents[1]),str(saved),str(output)],
                                capture_output=True,text=True,timeout=60)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertEqual(output.read_bytes(),DXFCAD.DXFDocument.parse(DRAWING).serialize_bytes())


if __name__ == "__main__":
    unittest.main()
