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
from dxfcad_import import default_text_font
from dxfcad_export import export_dxf


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

    def test_text_justification_in_rotated_tilted_plane_and_restore(self):
        font = default_text_font()
        self.assertTrue(font)
        doc = FreeCAD.newDocument("TextAlignment")
        raw = "0\nTEXT\n10\n0\n20\n0\n40\n2\n1\nAlign gy\n"
        def feature(record):
            obj = DXFCAD.create_feature(doc, DXFCAD.parse_entity(record), font)
            if obj.TypeId == "Part::FeaturePython":
                DXFObjectProxy(obj)
            doc.recompute()
            self.assertFalse(obj.Shape.isNull(), obj.DisplayStatus)
            self.assertTrue(obj.Shape.isValid(), obj.DisplayStatus)
            self.assertEqual(obj.DXFData, record)
            return obj
        original = feature(raw).Shape.copy()
        bounds = original.BoundBox
        for horizontal in range(3):
            for vertical in range(4):
                record = raw.replace("10\n0\n20\n0\n", "10\n7\n20\n8\n30\n9\n") + (
                    f"11\n7\n21\n8\n31\n9\n72\n{horizontal}\n73\n{vertical}\n"
                    "41\n2\n50\n90\n210\n0\n220\n1\n230\n0\n")
                obj = feature(record)
                x = y = 0
                if horizontal or vertical:
                    x = [-bounds.XMin, -(bounds.XMin+bounds.XMax)/2, -bounds.XMax][horizontal]
                    y = [0, -bounds.YMin, -(bounds.YMin+bounds.YMax)/2, -bounds.YMax][vertical]
                self.assertEqual(len(original.Vertexes), len(obj.Shape.Vertexes))
                for old, new in zip(original.Vertexes, obj.Shape.Vertexes):
                    p = old.Point
                    expected = FreeCAD.Vector(p.y+y-7, 9, 2*(p.x+x)+8)
                    self.assertLess((new.Point-expected).Length, 2e-6)
        # Import, export, and FCStd restore must retain both alignment points
        # and opaque data rather than replacing native text with outline data.
        record += "1001\nSynthetic\n1000\nPreserve alignment data\n"
        drawing = "0\nSECTION\n2\nENTITIES\n"+record+"0\nENDSEC\n0\nEOF\n"
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp)/"aligned.dxf"
            source.write_text(drawing)
            imported = import_dxf(source)
            saved = Path(temp)/"aligned.FCStd"
            imported.saveAs(str(saved))
            FreeCAD.closeDocument(imported.Name)
            source.unlink()
            restored = FreeCAD.openDocument(str(saved))
            text = next(obj for obj in restored.Objects if "DXFData" in obj.PropertiesList)
            text.Shape = Part.Shape()
            text.touch()
            restored.recompute()
            self.assertFalse(text.Shape.isNull())
            self.assertTrue(text.Shape.isValid())
            self.assertEqual(text.DXFData, record)
            output = Path(temp)/"exported.dxf"
            export_dxf(output, restored)
            self.assertEqual(output.read_bytes(), DXFCAD.DXFDocument.parse(drawing).serialize_bytes())

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
        self.assertEqual(legacy.DXFLayer,DXFCAD.parse_entity(raw).layer)
        self.assertEqual(legacy.DXFHandle,DXFCAD.parse_entity(raw).handle)

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
