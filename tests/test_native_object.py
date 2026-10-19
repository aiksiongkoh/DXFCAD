"""Verify the actual FreeCAD C++ subclass, without a Python document proxy."""

import FreeCAD

from pathlib import Path
import math
import subprocess
import sys
import tempfile
import unittest
from xml.etree import ElementTree
from zipfile import ZipFile

module_directory = Path(sys.argv.pop(1)).resolve()
sys.path.insert(0, str(module_directory))
import DXFCAD

assert Path(DXFCAD.__file__).parent == module_directory
assert DXFCAD.NATIVE_DOCUMENT_OBJECTS

NATIVE_DATA = ("0\nLWPOLYLINE\n5\nABCD\n8\nExample\n90\n2\n70\n0\n38\n3\n"
               "10\n0\n20\n0\n40\n0.2\n41\n0.4\n42\n1\n10\n2\n20\n0\n"
               "1001\nSynthetic\n1000\npreserve unknown fields\n")


class NativeObjectTests(unittest.TestCase):
    def setUp(self):
        self.doc = FreeCAD.newDocument("NativeTest")
        self.obj = self.doc.addObject("DXFCAD::LWPolylineObject", "Polyline")
        self.obj.DXFData = NATIVE_DATA
        self.doc.recompute()

    def tearDown(self):
        if self.doc.Name in FreeCAD.listDocuments():
            FreeCAD.closeDocument(self.doc.Name)

    def test_native_inheritance_geometry_and_metadata(self):
        self.assertEqual(self.obj.TypeId, "DXFCAD::LWPolylineObject")
        self.assertTrue(self.obj.isDerivedFrom("Part::Feature"))
        self.assertTrue(self.obj.isDerivedFrom("App::DocumentObject"))
        self.assertNotIn("Proxy", self.obj.PropertiesList)
        self.assertEqual(self.obj.DXFType, "LWPOLYLINE")
        self.assertEqual(self.obj.DXFLayer, "Example")
        self.assertEqual(self.obj.DXFHandle, "ABCD")
        self.assertTrue(self.obj.Shape.isValid())
        self.assertAlmostEqual(self.obj.Shape.Area, 0.3*math.pi, delta=0.001)
        self.assertAlmostEqual(self.obj.Shape.BoundBox.ZMin, 3)
        self.obj.ShowWidths = False
        self.doc.recompute()
        self.assertEqual(self.obj.Shape.Area, 0)
        self.assertAlmostEqual(self.obj.Shape.Length, math.pi)
        self.assertEqual(self.obj.DXFData, NATIVE_DATA)

    def test_undo_redo(self):
        self.doc.UndoMode = 1
        self.doc.openTransaction("Centerline display")
        self.obj.ShowWidths = False
        self.doc.recompute()
        self.doc.commitTransaction()
        self.assertEqual(self.obj.Shape.Area, 0)
        self.doc.undo()
        self.doc.recompute()
        self.assertTrue(self.obj.ShowWidths)
        self.assertGreater(self.obj.Shape.Area, 0)
        self.doc.redo()
        self.doc.recompute()
        self.assertFalse(self.obj.ShowWidths)
        self.assertEqual(self.obj.Shape.Area, 0)
        self.assertEqual(self.obj.DXFData, NATIVE_DATA)

    def test_invalid_native_data_clears_stale_shape(self):
        self.obj.DXFData = NATIVE_DATA.replace("90\n2\n", "90\n3\n")
        self.doc.recompute()
        self.assertTrue(self.obj.DisplayStatus.startswith("Error:"))
        self.assertTrue(self.obj.Shape.isNull())
        self.obj.DXFData = NATIVE_DATA
        self.doc.recompute()
        self.assertFalse(self.obj.DisplayStatus.startswith("Error:"))
        self.assertTrue(self.obj.Shape.isValid())
        self.assertGreater(self.obj.Shape.Area, 0)

    def test_native_persistence_and_cold_restore(self):
        with tempfile.TemporaryDirectory() as folder:
            saved = Path(folder) / "native.FCStd"
            self.doc.saveAs(str(saved))
            with ZipFile(saved) as archive:
                xml = ElementTree.fromstring(archive.read("Document.xml"))
                native = next(obj for obj in xml.findall("./Objects/Object")
                              if obj.attrib["name"] == self.obj.Name)
                self.assertEqual(native.attrib["type"], "DXFCAD::LWPolylineObject")
                data = next(obj for obj in xml.findall("./ObjectData/Object")
                            if obj.attrib["name"] == self.obj.Name)
                self.assertNotIn("Proxy", [p.attrib["name"] for p in data.find("Properties")])
            # No import of DXFCAD: FreeCAD must auto-load the module from the
            # persisted type name. No dxfcad_objects document proxy is required.
            script = """import sys
import FreeCAD
sys.path.insert(0, sys.argv[1])
doc = FreeCAD.openDocument(sys.argv[2])
obj = doc.getObject('Polyline')
assert obj.TypeId == 'DXFCAD::LWPolylineObject'
assert 'Proxy' not in obj.PropertiesList
assert 'dxfcad_objects' not in sys.modules
obj.Shape = __import__('Part').Shape()
obj.touch()
doc.recompute()
assert obj.Shape.isValid() and obj.Shape.Area > 0
assert 'preserve unknown fields' in obj.DXFData
FreeCAD.closeDocument(doc.Name)
"""
            result = subprocess.run([sys.executable, "-c", script, str(module_directory), str(saved)],
                                    capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)


if __name__ == "__main__":
    unittest.main()
