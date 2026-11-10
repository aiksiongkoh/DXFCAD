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
from dxfcad_import import import_dxf, default_text_font, initialize_population
from dxfcad_objects import EntityProxy
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
            EntityProxy(obj)
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
        obj.DXFData = BASE+"1\n\\W2;Unsupported\n"
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

    def test_grouped_font_and_uniform_height(self):
        plain = self.feature(BASE+"1\nSynthetic text\n").Shape.copy()
        grouped = self.feature(BASE+"1\n{\\fArial|b0|i0|c0|p34;Synthetic text}\n").Shape
        self.assertEqual(len(plain.Vertexes), len(grouped.Vertexes))
        for original, rendered in zip(plain.Vertexes, grouped.Vertexes):
            self.assertLess((rendered.Point-original.Point).Length, 2e-6)
        scaled = self.feature(BASE+"1\n{\\H0.75x;Synthetic text}\n").Shape
        self.assertEqual(len(plain.Vertexes), len(scaled.Vertexes))
        for original, rendered in zip(plain.Vertexes, scaled.Vertexes):
            self.assertLess((rendered.Point-original.Point*0.75).Length, 2e-6)

    def test_unicode_escape_outlines(self):
        literal=self.feature(BASE+"1\n60\u00b0\n").Shape.copy()
        escaped=self.feature(BASE+"1\n60\\U+00B0\n").Shape
        self.assertEqual(len(literal.Vertexes),len(escaped.Vertexes))
        for expected,actual in zip(literal.Vertexes,escaped.Vertexes):
            self.assertLess((actual.Point-expected.Point).Length,2e-6)

    def test_windows1252_import_preserves_bytes_and_displays_unicode(self):
        header = "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$DWGCODEPAGE\n3\nANSI_1252\n0\nENDSEC\n"
        content = "M\u00e9dica \u20ac"
        raw = BASE + "3\nM\u00e9dica \n1\n\u20ac\n1001\nSynthetic\n1000\nKeep opaque data\n"
        drawing = (header + "0\nSECTION\n2\nENTITIES\n" + raw + "0\nENDSEC\n0\nEOF\n").encode("cp1252")
        with tempfile.TemporaryDirectory(dir=module_directory) as temp:
            source = Path(temp)/"legacy.dxf"
            source.write_bytes(drawing)
            doc = import_dxf(source)
            obj = next(o for o in doc.Objects if "DXFData" in o.PropertiesList)
            self.assertIn("M\\U+00E9dica", obj.DXFData)
            self.assertEqual(DXFCAD.text_info(obj.DXFData)[0], content)
            self.assertFalse(obj.Shape.isNull())
            self.assertTrue(obj.Shape.isValid())
            self.assertEqual(DXFCAD.current_drawing_bytes(obj.DXFSourceRoot), drawing)
            reference = self.feature(BASE + "1\n" + content + "\n").Shape
            self.assertEqual(len(obj.Shape.Vertexes), len(reference.Vertexes))
            for expected, actual in zip(reference.Vertexes, obj.Shape.Vertexes):
                self.assertLess((actual.Point-expected.Point).Length, 2e-6)
            obj.DXFData = DXFCAD.parse_entity(obj.DXFData).translated(DXFCAD.Vector3D(2,3,0))
            doc.recompute()
            exported = DXFCAD.current_drawing_bytes(obj.DXFSourceRoot)
            self.assertIn("M\u00e9dica \n1\n\u20ac".encode("cp1252"), exported)
            saved = Path(temp)/"legacy.FCStd"
            doc.saveAs(str(saved))
            FreeCAD.closeDocument(doc.Name)
            restored = FreeCAD.openDocument(str(saved))
            self.assertEqual(DXFCAD.current_drawing_bytes(next(o for o in restored.Objects if "DXFSourceData" in o.PropertiesList)), exported)

    def test_windows1252_expansion_splits_chunks_and_utf8_is_unchanged(self):
        header = "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$DWGCODEPAGE\n3\nANSI_1252\n0\nENDSEC\n"
        for version, encoding in (("AC1015", "cp1252"), ("AC1021", "utf-8")):
            content = "\u00e9" * 40
            raw = BASE + "1\n" + content + "\n"
            drawing = (header.replace("AC1015", version) + "0\nSECTION\n2\nENTITIES\n" + raw + "0\nENDSEC\n0\nEOF\n").encode(encoding)
            population = DXFCAD.populate_drawing(self.doc, DXFCAD.Drawing.parse(drawing), font_file=self.font)
            obj = population.entities[0]
            initialize_population(population)
            self.doc.recompute()
            self.assertEqual(DXFCAD.text_info(obj.DXFData)[0], content)
            self.assertEqual(DXFCAD.current_drawing_bytes(population.root), drawing)
            values = obj.DXFData.splitlines()
            self.assertTrue(all(len(values[i+1].encode('utf-8')) <= 250 for i in range(0,len(values),2) if values[i] in ('1','3')))
            if encoding == "utf-8":
                self.assertEqual(obj.DXFData, raw)

    def test_windows1252_single_line_text_import_and_export(self):
        raw = "0\nTEXT\n5\nEF\n10\n0\n20\n0\n40\n2\n1\nL\u00e1piz\n"
        header = "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$DWGCODEPAGE\n3\nANSI_1252\n0\nENDSEC\n"
        drawing = (header + "0\nSECTION\n2\nENTITIES\n" + raw + "0\nENDSEC\n0\nEOF\n").encode('cp1252')
        population = DXFCAD.populate_drawing(self.doc, DXFCAD.Drawing.parse(drawing), font_file=self.font)
        initialize_population(population)
        self.doc.recompute()
        obj = population.entities[0]
        self.assertEqual(DXFCAD.text_info(obj.DXFData)[0], "L\u00e1piz")
        self.assertTrue(obj.Shape.isValid())
        self.assertFalse(obj.Shape.isNull())
        self.assertEqual(DXFCAD.current_drawing_bytes(population.root), drawing)

    def test_native_records_undo_export_and_cold_restore(self):
        raw = BASE+"3\n{\\fArial|b0|i0|c0|p34;\\H0.75x;First\\P\n1\nSecond\\U+00B0}\n71\n5\n41\n8\n1001\nSynthetic\n1000\nPreserve chunks\n"
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
            self.assertEqual(output.read_bytes(), DXFCAD.Drawing.parse(drawing).serialize_bytes())


if __name__ == "__main__":
    unittest.main()
