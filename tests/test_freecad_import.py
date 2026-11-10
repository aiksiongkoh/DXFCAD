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
from dxfcad_objects import EntityProxy
from dxfcad_import import import_dxf, initialize_population
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

    def test_semicircular_width_caps_ignore_collapsed_display_patches(self):
        import math

        x,y = 10.,20.
        end_x,end_y = 10.02,20.02
        radius = math.hypot(end_x-x,end_y-y)/2
        for normal in ((0,0,1),(0,1,0)):
            for bulge in (-1,1):
                for excess in (2e-14,2e-8,.01):
                    width = 2*radius+excess
                    raw = (f"0\nLWPOLYLINE\n5\nAB\n90\n2\n70\n0\n38\n5\n"
                           f"10\n{x}\n20\n{y}\n40\n{width}\n41\n{width}\n42\n{bulge}\n"
                           f"10\n{end_x}\n20\n{end_y}\n"
                           f"210\n{normal[0]}\n220\n{normal[1]}\n230\n{normal[2]}\n"
                           "1001\nSynthetic\n1000\nPreserve native cap widths\n")
                    source = DXFCAD.Drawing.parse("0\nSECTION\n2\nENTITIES\n"+raw+"0\nENDSEC\n0\nEOF\n")
                    # The domain keeps even the tiny secondary region; only
                    # display-kernel construction applies its own tolerance.
                    self.assertGreaterEqual(len(source.entities[0].width_regions()),2)
                    doc = FreeCAD.newDocument('WidthCap')
                    population = DXFCAD.populate_drawing(doc,source)
                    initialize_population(population)
                    doc.recompute()
                    obj = population.entities[0]
                    self.assertFalse(obj.DisplayStatus.startswith('Error:'),obj.DisplayStatus)
                    self.assertFalse(obj.Shape.isNull())
                    self.assertTrue(obj.Shape.isValid())
                    outer,inner = radius+width/2,width/2-radius
                    expected = math.pi*(outer*outer+inner*inner)/2
                    # Both semicircular boundaries are approximated at 1e-4
                    # chord tolerance; perimeter times tolerance bounds area.
                    tolerance = math.pi*(outer+inner)*1e-4
                    self.assertAlmostEqual(obj.Shape.Area,expected,delta=tolerance)
                    self.assertEqual(obj.DXFData,raw)
                    self.assertEqual(DXFCAD.current_drawing_bytes(population.root),source.serialize_bytes())
                    obj.ShowWidths = False
                    doc.recompute()
                    self.assertAlmostEqual(obj.Shape.Length,math.pi*radius,delta=1e-10)
                    obj.ShowWidths = True
                    doc.recompute()
                    self.assertTrue(obj.Shape.isValid())
                    self.assertAlmostEqual(obj.Shape.Area,expected,delta=tolerance)
                    FreeCAD.closeDocument(doc.Name)

    def test_native_arc_angles_handle_nearly_closed_and_shallow_arcs(self):
        import math

        # Independent OCS mappings for XY, tilted, and reversed planes.
        planes = [((0,0,1), lambda x,y,z: FreeCAD.Vector(x,y,z)),
                  ((0,1,0), lambda x,y,z: FreeCAD.Vector(-x,z,y)),
                  ((0,0,-1), lambda x,y,z: FreeCAD.Vector(-x,y,-z))]
        for normal, world in planes:
            for start, end, radius in [(0,359.9999999999999,.25), (270,180,2),
                                       (40,40.001,10000), (350,10,3)]:
                raw = (f"0\nARC\n5\nAB\n10\n2\n20\n-3\n30\n5\n40\n{radius}\n50\n{start}\n51\n{end}\n"
                       f"210\n{normal[0]}\n220\n{normal[1]}\n230\n{normal[2]}\n"
                       "1001\nSynthetic\n1000\nPreserve native arc angles\n")
                source = DXFCAD.Drawing.parse("0\nSECTION\n2\nENTITIES\n"+raw+"0\nENDSEC\n0\nEOF\n")
                doc = FreeCAD.newDocument("AnalyticArc")
                population = DXFCAD.populate_drawing(doc, source)
                initialize_population(population)
                doc.recompute()
                obj = population.entities[0]
                self.assertFalse(obj.DisplayStatus.startswith('Error:'), obj.DisplayStatus)
                self.assertTrue(obj.Shape.isValid())
                self.assertEqual(len(obj.Shape.Edges), 1)
                edge = obj.Shape.Edges[0]
                sweep = math.radians((end-start)%360)
                self.assertAlmostEqual(edge.Length, radius*sweep, delta=1e-8)
                self.assertAlmostEqual(edge.Curve.Radius, radius, delta=1e-10)
                center = edge.Curve.Location if hasattr(edge.Curve,'Location') else edge.Curve.Center
                self.assertLess((center-world(2,-3,5)).Length, 1e-10)
                for fraction in [0,.25,.5,.75,1]:
                    angle = math.radians(start)+fraction*sweep
                    expected = world(2+radius*math.cos(angle),-3+radius*math.sin(angle),5)
                    actual = edge.valueAt(edge.FirstParameter+fraction*(edge.LastParameter-edge.FirstParameter))
                    self.assertLess((actual-expected).Length, 1e-8)
                self.assertEqual(obj.DXFType, 'ARC')
                self.assertEqual(obj.DXFData, raw)
                self.assertEqual(DXFCAD.current_drawing_bytes(population.root), source.serialize_bytes())
                FreeCAD.closeDocument(doc.Name)

    def test_periodic_spline_representations_preserve_analytical_geometry(self):
        import math

        def check_curve(points, knots, degree, flags, weights, expected, parameters):
            raw = f"0\nSPLINE\n5\nAB\n70\n{flags}\n71\n{degree}\n72\n{len(knots)}\n73\n{len(points)}\n"
            raw += ''.join(f"40\n{knot}\n" for knot in knots)
            raw += ''.join(f"10\n{x}\n20\n{y}\n30\n{z}\n" for x,y,z in points)
            raw += ''.join(f"41\n{weight}\n" for weight in weights)
            raw += "1001\nSynthetic\n1000\nPreserve spline data\n"
            source = DXFCAD.Drawing.parse("0\nSECTION\n2\nENTITIES\n"+raw+"0\nENDSEC\n0\nEOF\n")
            doc = FreeCAD.newDocument("SplineRepresentation")
            population = DXFCAD.populate_drawing(doc, source)
            initialize_population(population)
            doc.recompute()
            obj = population.entities[0]
            self.assertFalse(obj.DisplayStatus.startswith('Error:'), obj.DisplayStatus)
            self.assertTrue(obj.Shape.isValid())
            self.assertEqual(len(obj.Shape.Edges), 1)
            edge = obj.Shape.Edges[0]
            self.assertEqual(edge.isClosed(), bool(flags & 1))
            self.assertEqual(edge.Curve.isPeriodic(), bool(flags & 2))
            for parameter in parameters:
                self.assertLess((edge.Curve.value(parameter)-expected(parameter)).Length, 1e-10)
            self.assertEqual(obj.DXFData, raw)
            self.assertEqual(DXFCAD.current_drawing_bytes(population.root), source.serialize_bytes())
            obj.DXFData = DXFCAD.parse_entity(raw).translated(DXFCAD.Vector3D(3,-2,1))
            doc.recompute()
            for parameter in parameters:
                self.assertLess((obj.Shape.Edges[0].Curve.value(parameter)-expected(parameter)-FreeCAD.Vector(3,-2,1)).Length, 1e-10)
            FreeCAD.closeDocument(doc.Name)

        # A closed clamped cubic Bezier loop, stored with the DXF periodic bit.
        # Its Bernstein polynomials supply an independent exact reference.
        points = [(0,0,0), (2,0,0), (2,2,0), (0,0,0)]
        for weights in ([1,1,1,1], [1,2,3,1]):
            def cubic(t):
                basis = [(1-t)**3, 3*t*(1-t)**2, 3*t*t*(1-t), t**3]
                denominator = sum(b*w for b,w in zip(basis,weights))
                return FreeCAD.Vector(*(sum(b*w*p[axis] for b,w,p in zip(basis,weights,points))/denominator for axis in range(3)))
            check_curve(points, [0]*4+[1]*4, 3, 3 | (4 if weights[1]!=1 else 0), weights, cubic, [0,.125,.5,.875,1])

        # A genuinely expanded uniform quadratic periodic net. The active
        # domain is [0,4], within the full knot vector [-2,...,6].
        points = [(0,0,3), (2,0,3), (2,2,3), (0,2,3), (0,0,3), (2,0,3)]
        for weights in ([1]*6, [1,2,1,2,1,2]):
            def quadratic(t):
                span = min(int(t),3)
                u = t-span
                basis = [(1-u)**2/2, (-2*u*u+2*u+1)/2, u*u/2]
                active = points[span:span+3]
                active_weights = weights[span:span+3]
                denominator = sum(b*w for b,w in zip(basis,active_weights))
                return FreeCAD.Vector(*(sum(b*w*p[axis] for b,w,p in zip(basis,active_weights,active))/denominator for axis in range(3)))
            check_curve(points, list(range(-2,7)), 2, 3 | (4 if weights[1]!=1 else 0), weights, quadratic, [0,.25,.5,1,1.5,2,2.5,3,3.5,4])

        # An open rational quadratic remains the exact unit-circle quadrant.
        points = [(1,0,0), (1,1,0), (0,1,0)]
        weights = [1,math.sqrt(.5),1]
        def quadrant(t):
            basis = [(1-t)**2, 2*t*(1-t), t*t]
            denominator = sum(b*w for b,w in zip(basis,weights))
            return FreeCAD.Vector(*(sum(b*w*p[axis] for b,w,p in zip(basis,weights,points))/denominator for axis in range(3)))
        check_curve(points, [0]*3+[1]*3, 2, 4, weights, quadrant, [0,.25,.5,.75,1])

    def test_text_justification_in_rotated_tilted_plane_and_restore(self):
        font = default_text_font()
        self.assertTrue(font)
        doc = FreeCAD.newDocument("TextAlignment")
        raw = "0\nTEXT\n10\n0\n20\n0\n40\n2\n1\nAlign gy\n"
        def feature(record):
            obj = DXFCAD.create_feature(doc, DXFCAD.parse_entity(record), font)
            if obj.TypeId == "Part::FeaturePython":
                EntityProxy(obj)
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
            self.assertEqual(output.read_bytes(), DXFCAD.Drawing.parse(drawing).serialize_bytes())

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
                EntityProxy(obj)
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
        EntityProxy(legacy)
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
