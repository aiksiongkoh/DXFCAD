"""Native TEXT persistence, recomputation, font errors, and OCS transforms."""
import FreeCAD

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

module_directory = Path(sys.argv.pop(1)).resolve()
sys.path.insert(0, str(module_directory))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import DXFCAD
from dxfcad_import import default_text_font
assert Path(DXFCAD.__file__).parent == module_directory
assert DXFCAD.NATIVE_DOCUMENT_OBJECTS
font = default_text_font()
assert font, "A test font is required"
raw = ("0\nTEXT\n5\nAB\n8\nExample\n10\n0\n20\n0\n40\n2\n1\nDXFCAD Ω\n"
       "7\nOriginalStyle\n1001\nSynthetic\n1000\npreserve unknown fields\n")


class NativeTextTests(unittest.TestCase):
    def setUp(self):
        self.doc = FreeCAD.newDocument("NativeText")
        self.obj = DXFCAD.create_feature(self.doc, DXFCAD.parse_entity(raw), font)
        self.doc.recompute()

    def tearDown(self):
        FreeCAD.closeDocument(self.doc.Name)

    def test_native_factory_and_geometry(self):
        self.assertEqual(self.obj.TypeId, "DXFCAD::TextObject")
        self.assertTrue(self.obj.isDerivedFrom("Part::Feature"))
        self.assertTrue(self.obj.isDerivedFrom("App::DocumentObject"))
        self.assertNotIn("Proxy", self.obj.PropertiesList)
        self.assertEqual((self.obj.DXFType, self.obj.DXFLayer, self.obj.DXFHandle),
                         ("TEXT", "Example", "AB"))
        self.assertEqual(self.obj.TextFont, font)
        self.assertEqual(self.obj.DXFData, raw)
        self.assertTrue(self.obj.Shape.isValid())
        self.assertGreater(len(self.obj.Shape.Wires), 0)
        self.assertGreater(self.obj.Shape.Length, 0)
        self.assertEqual(self.obj.DisplayStatus, "Text outlines (substitute font)")

    def test_height_width_rotation_and_tilt(self):
        original = self.obj.Shape.copy()
        self.obj.DXFData = raw.replace("40\n2\n", "40\n4\n")
        self.doc.recompute()
        self.assertAlmostEqual(self.obj.Shape.Length, 2*original.Length, places=5)
        # OCS X=(-1,0,0), Y=(0,0,1); rotate 90 degrees and scale local X by 2.
        self.obj.DXFData = raw.replace("10\n0\n20\n0\n", "10\n1\n20\n2\n30\n3\n").replace(
            "1001\n", "41\n2\n50\n90\n210\n0\n220\n1\n230\n0\n1001\n", 1)
        self.doc.recompute()
        self.assertTrue(self.obj.Shape.isValid())
        self.assertEqual(len(original.Vertexes), len(self.obj.Shape.Vertexes))
        for old, new in zip(original.Vertexes, self.obj.Shape.Vertexes):
            p = old.Point
            expected = FreeCAD.Vector(p.y-1, 3, 2*p.x+2)
            self.assertLess((new.Point-expected).Length, 1e-6)
        self.assertEqual(self.obj.DXFData.split("7\n")[1].splitlines()[0], "OriginalStyle")

    def test_font_recompute_and_invalid_data_recovery(self):
        with tempfile.TemporaryDirectory() as folder:
            invalid_font = Path(folder) / "invalid.ttf"
            invalid_font.write_text("not a font")
            for invalid in [str(Path(folder)/"missing.ttf"), str(invalid_font)]:
                self.obj.TextFont = invalid
                self.doc.recompute()
                self.assertTrue(self.obj.Shape.isNull())
                self.assertTrue(self.obj.DisplayStatus.startswith("Error:"))
            self.obj.TextFont = font
            self.doc.recompute()
            self.assertTrue(self.obj.Shape.isValid())
            self.assertGreater(self.obj.Shape.Length, 0)
        for invalid in [raw.replace("40\n2\n", "40\n0\n"), raw.replace("1001\n", "72\n1\n1001\n", 1),
                        raw.replace("0\nTEXT\n", "0\nLINE\n", 1)]:
            self.obj.DXFData = invalid
            self.doc.recompute()
            self.assertTrue(self.obj.Shape.isNull())
            self.assertTrue(self.obj.DisplayStatus.startswith("Error:"))
        self.obj.DXFData = raw
        self.doc.recompute()
        self.assertTrue(self.obj.Shape.isValid())

    def test_empty_text(self):
        self.obj.DXFData = raw.replace("DXFCAD Ω", "")
        self.obj.TextFont = ""
        self.doc.recompute()
        self.assertTrue(self.obj.Shape.isNull())
        self.assertEqual(self.obj.DisplayStatus, "Empty")
        self.obj.DXFData = raw.replace("DXFCAD Ω", "   ")
        self.obj.TextFont = font
        self.doc.recompute()
        self.assertTrue(self.obj.Shape.isNull())
        self.assertEqual(self.obj.DisplayStatus, "Empty")

    def test_undo_redo(self):
        self.doc.UndoMode = 1
        initial = self.obj.Shape.Length
        self.doc.openTransaction("Change text and font")
        changed = raw.replace("DXFCAD Ω", "Changed")
        self.obj.DXFData = changed
        self.obj.TextFont = ""
        self.doc.recompute()
        self.doc.commitTransaction()
        self.assertTrue(self.obj.Shape.isNull())
        self.doc.undo()
        self.doc.recompute()
        self.assertEqual(self.obj.DXFData, raw)
        self.assertEqual(self.obj.TextFont, font)
        self.assertAlmostEqual(self.obj.Shape.Length, initial)
        self.doc.redo()
        self.doc.recompute()
        self.assertEqual(self.obj.DXFData, changed)
        self.assertEqual(self.obj.TextFont, "")
        self.assertTrue(self.obj.Shape.isNull())

    def test_cold_restore(self):
        with tempfile.TemporaryDirectory() as folder:
            saved = Path(folder) / "text.FCStd"
            self.doc.saveAs(str(saved))
            script = """import FreeCAD
import sys
sys.path.insert(0, sys.argv[1])
doc = FreeCAD.openDocument(sys.argv[2])
obj = doc.getObject('DXFTEXT')
assert obj.TypeId == 'DXFCAD::TextObject'
assert 'Proxy' not in obj.PropertiesList
assert 'dxfcad_objects' not in sys.modules
assert obj.TextFont == sys.argv[3]
assert 'OriginalStyle' in obj.DXFData and 'preserve unknown fields' in obj.DXFData
obj.Shape = __import__('Part').Shape()
obj.touch()
doc.recompute()
assert obj.Shape.isValid() and obj.Shape.Length > 0
FreeCAD.closeDocument(doc.Name)
"""
            result = subprocess.run([sys.executable, "-c", script, str(module_directory), str(saved), font],
                                    capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)


if __name__ == "__main__":
    unittest.main()
