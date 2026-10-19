"""Synthetic integration tests; no customer drawing is stored in the repository."""

import FreeCAD
import Part

from pathlib import Path
import sys
import tempfile
import unittest

module_directory = Path(sys.argv.pop(1)).resolve()

# FreeCAD adds installed Mod folders to sys.path during initialization. Select
# this build afterwards so an older installed DXFCAD cannot mask the module.
sys.path.insert(0, str(module_directory))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import DXFCAD
assert Path(DXFCAD.__file__).parent == module_directory
from dxfcad_objects import DXFObjectProxy
from dxfcad_import import import_dxf


DRAWING = """0
SECTION
2
TABLES
0
LAYER
2
Hidden
62
-5
0
ENDSEC
0
SECTION
2
ENTITIES
0
LWPOLYLINE
5
A1
8
Hidden
90
2
70
0
10
0
20
0
40
0.1
41
0.2
42
1
10
2
20
0
1001
Synthetic
1000
preserve me
0
LINE
5
A2
10
0
20
0
11
2
21
3
0
ARC
5
A3
10
4
20
0
40
1
50
270
51
90
0
CIRCLE
5
A4
10
7
20
0
40
1
0
TEXT
5
A5
10
0
20
5
40
0.5
1
DXFCAD test
0
ENDSEC
0
EOF
"""


class ImportTests(unittest.TestCase):
    def tearDown(self):
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)

    def test_shapes_and_restore_from_native_records(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "synthetic.dxf"
            path.write_text(DRAWING)
            doc = import_dxf(path)
            features = [obj for obj in doc.Objects if "DXFData" in obj.PropertiesList]
            self.assertEqual(len(features), 5)
            self.assertEqual({obj.DXFType for obj in features},
                             {"LWPOLYLINE", "LINE", "ARC", "CIRCLE", "TEXT"})
            for obj in features:
                expected = {"LWPOLYLINE": "DXFCAD::LWPolylineObject",
                            "LINE": "DXFCAD::LineObject", "ARC": "DXFCAD::ArcObject",
                            "CIRCLE": "DXFCAD::CircleObject", "TEXT": "DXFCAD::TextObject"}
                if DXFCAD.NATIVE_DOCUMENT_OBJECTS and obj.DXFType in expected:
                    self.assertEqual(obj.TypeId, expected[obj.DXFType])
                    self.assertNotIn("Proxy", obj.PropertiesList)
                self.assertFalse(obj.Shape.isNull(), obj.Label)
                self.assertTrue(obj.Shape.isValid(), obj.Label)
            wide = next(obj for obj in features if obj.DXFType == "LWPOLYLINE")
            self.assertTrue(wide.ShowWidths)
            self.assertGreater(wide.Shape.Area, 0)
            native = wide.DXFData
            wide.ShowWidths = False
            doc.recompute()
            self.assertEqual(wide.Shape.Area, 0)
            self.assertAlmostEqual(wide.Shape.Length, 3.141592653589793)
            self.assertEqual(wide.DXFData, native)
            wide.ShowWidths = True
            doc.recompute()
            records = {obj.DXFHandle: obj.DXFData for obj in features}
            self.assertIn("preserve me", records["A1"])
            saved = Path(temp) / "synthetic.FCStd"
            doc.saveAs(str(saved))
            FreeCAD.closeDocument(doc.Name)
            path.unlink()  # Restoration must use saved native data, not the source file.
            restored = FreeCAD.openDocument(str(saved))
            for obj in restored.Objects:
                if "DXFData" in obj.PropertiesList:
                    self.assertEqual(records[obj.DXFHandle], obj.DXFData)
                    obj.Shape = Part.Shape()
                    obj.touch()
            restored.recompute()
            for obj in restored.Objects:
                if "DXFData" in obj.PropertiesList:
                    self.assertFalse(obj.Shape.isNull(), obj.Label)
                    self.assertTrue(obj.Shape.isValid(), obj.Label)
                    if obj.DXFType == "LWPOLYLINE":
                        self.assertTrue(obj.ShowWidths)
                        self.assertGreater(obj.Shape.Area, 0)

    def test_wide_major_arcs_union_and_legacy_migration(self):
        import math

        doc = FreeCAD.newDocument("WideArcs")
        for start_width, end_width in [(3, 3), (1, 3), (3, 1)]:
            raw = (f"0\nLWPOLYLINE\n90\n2\n10\n1\n20\n0\n42\n{1+math.sqrt(2)}\n"
                   f"40\n{start_width}\n41\n{end_width}\n10\n0\n20\n-1\n")
            obj = DXFCAD.create_feature(doc, DXFCAD.parse_entity(raw), "")
            if obj.TypeId == "Part::FeaturePython":
                DXFObjectProxy(obj)
            doc.recompute()
            self.assertFalse(obj.DisplayStatus.startswith("Error:"), obj.DisplayStatus)
            self.assertTrue(obj.Shape.isValid())
            self.assertGreater(obj.Shape.Area, 0)
            if start_width == end_width:
                self.assertAlmostEqual(obj.Shape.Area, 4.75*math.pi, delta=0.003)
        # Legacy Python-backed documents remain readable with a native build.
        legacy = doc.addObject("Part::FeaturePython", "Legacy")
        for name, value in [("DXFType", "LWPOLYLINE"), ("DXFData", raw),
                            ("DisplayStatus", ""), ("TextFont", "")]:
            legacy.addProperty("App::PropertyString", name)
            setattr(legacy, name, value)
        DXFObjectProxy(legacy)
        legacy.Proxy.onDocumentRestored(legacy)
        self.assertTrue(legacy.ShowWidths)
        doc.recompute()
        self.assertTrue(legacy.Shape.isValid())

    def test_failed_display_rolls_back_existing_document(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "synthetic.dxf"
            path.write_text(DRAWING)
            doc = FreeCAD.newDocument("Existing")
            keep = doc.addObject("App::FeaturePython", "Keep")
            with self.assertRaises(RuntimeError):
                import_dxf(path, doc, font_file=str(Path(temp) / "missing.ttf"))
            self.assertEqual([obj.Name for obj in doc.Objects], [keep.Name])


if __name__ == "__main__":
    unittest.main()
