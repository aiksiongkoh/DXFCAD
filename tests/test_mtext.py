"""MTEXT display/persistence integration; synthetic, customer-free fixtures."""
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
from dxfcad_objects import DXFObjectProxy
from dxfcad_export import export_dxf

assert Path(DXFCAD.__file__).parent == module_directory
BASE = "0\nMTEXT\n5\nAB\n10\n0\n20\n0\n30\n0\n40\n2\n"


class MTextTests(unittest.TestCase):
    def setUp(self):
        self.doc = FreeCAD.newDocument("MTextTest")
        self.font = default_text_font()
        self.assertTrue(self.font)

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)

    def feature(self, record):
        obj = DXFCAD.create_feature(self.doc, DXFCAD.parse_entity(record), self.font)
        if not DXFCAD.NATIVE_DOCUMENT_OBJECTS:
            DXFObjectProxy(obj)
        self.doc.recompute()
        self.assertEqual(obj.DXFData, record)
        self.assertFalse(obj.DisplayStatus.startswith("Error:"), obj.DisplayStatus)
        self.assertFalse(obj.Shape.isNull())
        self.assertTrue(obj.Shape.isValid())
        return obj

    def test_attachment_and_tilted_wcs(self):
        raw = BASE+"1\nA\\PA\n41\n10\n"
        original = self.feature(raw).Shape.copy()
        for attachment in range(1, 10):
            obj = self.feature(raw+f"71\n{attachment}\n")
            if DXFCAD.NATIVE_DOCUMENT_OBJECTS:
                self.assertEqual(obj.TypeId, "DXFCAD::MTextObject")
                self.assertTrue(obj.isDerivedFrom("DXFCAD::TextObject"))
                self.assertNotIn("Proxy", obj.PropertiesList)
            # The block includes the nominal cap height, which can exceed the
            # substitute font's visible ink height.
            delta = FreeCAD.Vector(-((attachment-1)%3)*5, ((attachment-1)//3)*(-original.BoundBox.YMin)/2, 0)
            self.assertEqual(len(original.Vertexes), len(obj.Shape.Vertexes))
            for old, new in zip(original.Vertexes, obj.Shape.Vertexes):
                self.assertLess((new.Point-old.Point-delta).Length, 2e-6)
        tilted = self.feature(raw.replace("10\n0\n20\n0\n30\n0\n", "10\n10\n20\n20\n30\n30\n")+
                              "210\n0\n220\n1\n230\n0\n11\n0\n21\n0\n31\n2\n")
        for old, new in zip(original.Vertexes, tilted.Shape.Vertexes):
            p = old.Point
            self.assertLess((new.Point-FreeCAD.Vector(10+p.y, 20, 30+p.x)).Length, 2e-6)

    def test_wrap_and_font_failure_recovery(self):
        obj = self.feature(BASE+"1\nWrapping words for a narrow rectangle\n41\n5\n")
        self.assertLessEqual(obj.Shape.BoundBox.XLength, 5.000002)
        self.assertGreater(obj.Shape.BoundBox.YLength, 4)
        obj.TextFont = str(Path(self.font).with_name("missing-test-font.ttf"))
        self.doc.recompute()
        self.assertTrue(obj.Shape.isNull())
        self.assertTrue(obj.DisplayStatus.startswith("Error:"))
        obj.TextFont = self.font
        self.doc.recompute()
        self.assertFalse(obj.Shape.isNull())
        obj.DXFData = BASE+"1\n\\H2;Unsupported\n"
        if DXFCAD.NATIVE_DOCUMENT_OBJECTS:
            self.doc.recompute()
        else:
            with self.assertRaises(ValueError):
                DXFCAD.recompute_feature(obj)
        self.assertTrue(obj.Shape.isNull())
        obj.DXFData = BASE+"1\nRecovered\n"
        self.doc.recompute()
        self.assertTrue(obj.Shape.isValid())
        self.assertGreater(obj.Shape.Length, 0)

    def test_native_records_undo_export_and_cold_restore(self):
        raw = BASE+"3\nFirst\\P\n1\nSecond\n71\n5\n41\n8\n1001\nSynthetic\n1000\nPreserve chunks\n"
        drawing = "0\nSECTION\n2\nENTITIES\n"+raw+"0\nENDSEC\n0\nEOF\n"
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp)/"mtext.dxf"
            source.write_text(drawing)
            doc = import_dxf(source)
            obj = next(o for o in doc.Objects if "DXFData" in o.PropertiesList)
            self.assertEqual(obj.DXFData, raw)
            self.assertTrue(obj.Shape.isValid())
            doc.UndoMode = 1
            doc.openTransaction("Change MTEXT")
            obj.DXFData = raw.replace("Second", "Changed")
            doc.recompute()
            doc.commitTransaction()
            doc.undo()
            doc.recompute()
            self.assertEqual(obj.DXFData, raw)
            doc.redo()
            doc.recompute()
            self.assertIn("Changed", obj.DXFData)
            doc.undo()
            doc.recompute()
            saved = Path(temp)/"mtext.FCStd"
            doc.saveAs(str(saved))
            FreeCAD.closeDocument(doc.Name)
            source.unlink()
            script = """import FreeCAD, Part
import sys
sys.path.insert(0, sys.argv[1])
sys.path.insert(0, sys.argv[2])
import DXFCAD
from dxfcad_export import export_dxf
# This test loads the workbench from a source checkout outside FreeCAD's
# installed-addon roots. Explicitly trust its lifecycle adapter at startup.
import dxfcad_objects
doc = FreeCAD.openDocument(sys.argv[3])
obj = next(o for o in doc.Objects if 'DXFData' in o.PropertiesList)
assert obj.DXFType == 'MTEXT' and 'Preserve chunks' in obj.DXFData
obj.Shape = Part.Shape()
obj.touch()
doc.recompute()
assert not obj.Shape.isNull() and obj.Shape.isValid(), (obj.TypeId, obj.DisplayStatus, DXFCAD.__file__,
    repr(getattr(obj, 'Proxy', None)), obj.State, 'dxfcad_objects' in sys.modules)
assert not obj.DisplayStatus.startswith('Error:')
export_dxf(sys.argv[4], doc)
FreeCAD.closeDocument(doc.Name)
"""
            output = Path(temp)/"exported.dxf"
            result = subprocess.run([sys.executable, "-c", script, str(module_directory),
                                     str(Path(__file__).resolve().parents[1]), str(saved), str(output)],
                                    capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            self.assertEqual(output.read_bytes(), DXFCAD.DXFDocument.parse(drawing).serialize_bytes())


if __name__ == "__main__":
    unittest.main()
