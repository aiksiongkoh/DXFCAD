"""Native LINE, ARC, and CIRCLE lifecycle and geometry integration tests."""
import FreeCAD

from pathlib import Path
import math
import subprocess
import sys
import tempfile
import unittest

module_directory = Path(sys.argv.pop(1)).resolve()
kind = sys.argv.pop(1)
sys.path.insert(0, str(module_directory))
import DXFCAD
assert Path(DXFCAD.__file__).parent == module_directory
assert DXFCAD.NATIVE_DOCUMENT_OBJECTS

DATA = {
    "LINE": "0\nLINE\n5\nAB\n8\nExample\n10\n1\n20\n2\n30\n3\n11\n4\n21\n6\n31\n3\n210\n0\n220\n1\n230\n0\n",
    # Tilted extrusion, nonzero elevation, and a major arc crossing zero.
    "ARC": "0\nARC\n5\nAB\n8\nExample\n10\n0\n20\n0\n30\n3\n40\n2\n50\n270\n51\n180\n210\n0\n220\n1\n230\n0\n",
    "CIRCLE": "0\nCIRCLE\n5\nAB\n8\nExample\n10\n1\n20\n2\n30\n3\n40\n2\n210\n0\n220\n1\n230\n0\n",
}
raw = DATA[kind] + "1001\nSynthetic\n1000\npreserve unknown fields\n"
type_id = "DXFCAD::" + kind.title() + "Object"
length = {"LINE": 5, "ARC": 3 * math.pi, "CIRCLE": 4 * math.pi}[kind]


class NativeSegmentTests(unittest.TestCase):
    def setUp(self):
        self.doc = FreeCAD.newDocument("NativeSegment")
        self.obj = DXFCAD.create_feature(self.doc, DXFCAD.parse_entity(raw), "")
        self.doc.recompute()

    def tearDown(self):
        FreeCAD.closeDocument(self.doc.Name)

    def test_geometry_and_factory(self):
        obj = self.obj
        self.assertEqual(obj.TypeId, type_id)
        self.assertTrue(obj.isDerivedFrom("Part::Feature"))
        self.assertTrue(obj.isDerivedFrom("App::DocumentObject"))
        self.assertNotIn("Proxy", obj.PropertiesList)
        self.assertEqual((obj.DXFType, obj.DXFLayer, obj.DXFHandle), (kind, "Example", "AB"))
        self.assertTrue(obj.Shape.isValid())
        self.assertAlmostEqual(obj.Shape.Length, length)
        self.assertEqual(len(obj.Shape.Edges), 1)
        if kind == "LINE":
            self.assertLess((obj.Shape.Vertexes[0].Point - FreeCAD.Vector(1, 2, 3)).Length, 1e-10)
            self.assertLess((obj.Shape.Vertexes[1].Point - FreeCAD.Vector(4, 6, 3)).Length, 1e-10)
        elif kind == "ARC":
            self.assertAlmostEqual(obj.Shape.BoundBox.YMin, 3)
            self.assertAlmostEqual(obj.Shape.BoundBox.YMax, 3)
            self.assertLess((obj.Shape.Edges[0].valueAt(obj.Shape.Edges[0].FirstParameter)
                             - FreeCAD.Vector(0, 3, -2)).Length, 1e-10)
        else:
            self.assertTrue(obj.Shape.Edges[0].isClosed())
            self.assertLess((obj.Shape.Edges[0].Curve.Center - FreeCAD.Vector(-1, 3, 2)).Length, 1e-10)
            self.assertAlmostEqual(obj.Shape.BoundBox.YMin, 3)
            self.assertAlmostEqual(obj.Shape.BoundBox.YMax, 3)

    def test_edit_undo_redo(self):
        changed = raw.replace("11\n4\n", "11\n7\n") if kind == "LINE" else raw.replace("40\n2\n", "40\n4\n")
        self.doc.UndoMode = 1
        self.doc.openTransaction("Edit native DXF")
        self.obj.DXFData = changed
        self.doc.recompute()
        self.doc.commitTransaction()
        changed_length = self.obj.Shape.Length
        self.assertNotAlmostEqual(changed_length, length)
        self.doc.undo()
        self.doc.recompute()
        self.assertEqual(self.obj.DXFData, raw)
        self.assertAlmostEqual(self.obj.Shape.Length, length)
        self.doc.redo()
        self.doc.recompute()
        self.assertEqual(self.obj.DXFData, changed)
        self.assertAlmostEqual(self.obj.Shape.Length, changed_length)

    def test_invalid_data_and_recovery(self):
        invalid = raw.replace("11\n4\n", "11\nNaN\n") if kind == "LINE" else raw.replace("40\n2\n", "40\n-2\n")
        for bad in [invalid, raw.replace("0\n" + kind + "\n", "0\nTEXT\n", 1)]:
            self.obj.DXFData = bad
            self.doc.recompute()
            self.assertTrue(self.obj.Shape.isNull())
            self.assertTrue(self.obj.DisplayStatus.startswith("Error:"))
        self.obj.DXFData = raw
        self.doc.recompute()
        self.assertTrue(self.obj.Shape.isValid())
        self.assertAlmostEqual(self.obj.Shape.Length, length)

    def test_degenerate_geometry(self):
        if kind == "LINE":
            self.obj.DXFData = raw.replace("11\n4\n21\n6\n", "11\n1\n21\n2\n")
            self.doc.recompute()
            self.assertTrue(self.obj.Shape.isNull())
            self.assertEqual(self.obj.DisplayStatus, "Empty")
        elif kind == "ARC":
            self.obj.DXFData = raw.replace("51\n180\n", "51\n270\n")
            self.doc.recompute()
            self.assertTrue(self.obj.Shape.isNull())
            self.assertTrue(self.obj.DisplayStatus.startswith("Error:"))
        else:
            self.obj.DXFData = raw.replace("40\n2\n", "40\n0\n")
            self.doc.recompute()
            self.assertTrue(self.obj.Shape.isNull())
            self.assertTrue(self.obj.DisplayStatus.startswith("Error:"))

    def test_cold_restore(self):
        with tempfile.TemporaryDirectory() as folder:
            saved = Path(folder) / "segment.FCStd"
            name = self.obj.Name
            self.doc.saveAs(str(saved))
            script = """import FreeCAD
import sys
sys.path.insert(0, sys.argv[1])
doc = FreeCAD.openDocument(sys.argv[2])
obj = doc.getObject(sys.argv[3])
assert obj.TypeId == sys.argv[4]
assert 'Proxy' not in obj.PropertiesList
assert 'dxfcad_objects' not in sys.modules
obj.Shape = __import__('Part').Shape()
obj.touch()
doc.recompute()
assert obj.Shape.isValid()
assert abs(obj.Shape.Length - float(sys.argv[5])) < 1e-9
assert 'preserve unknown fields' in obj.DXFData
FreeCAD.closeDocument(doc.Name)
"""
            result = subprocess.run([sys.executable, "-c", script, str(module_directory),
                                     str(saved), name, type_id, str(length)],
                                    capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
