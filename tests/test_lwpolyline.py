"""Exercise the compiled module using the interpreter selected by CMake."""

from pathlib import Path
import sys

# The extension is loaded from the build tree, never from an installed workbench.
sys.path.insert(0, str(Path(sys.argv.pop(1)).resolve()))
import DXFCAD

# FreeCAD's native startup may clean helper names from __main__. Import test
# helpers after the extension has initialized the matching FreeCAD runtime.
import gc
import math
import unittest


class LWPolylineBindingsTests(unittest.TestCase):
    def test_defaults(self):
        polyline = DXFCAD.LWPolyline()
        self.assertEqual(polyline.vertices, [])
        self.assertFalse(polyline.closed)
        self.assertEqual(polyline.constant_width, 0.0)
        self.assertEqual(polyline.elevation, 0.0)
        self.assertEqual(self.direction_values(polyline.extrusion), (0.0, 0.0, 1.0))

    @staticmethod
    def direction_values(direction):
        return direction.x, direction.y, direction.z

    @staticmethod
    def vertex_values(vertex):
        return vertex.x, vertex.y, vertex.bulge, vertex.start_width, vertex.end_width

    def test_native_values_and_ownership(self):
        vertices = [
            DXFCAD.LWPolylineVertex(1.25, -2.5, -0.5, 0.25, 0.75),
            DXFCAD.LWPolylineVertex(x=9, y=3, bulge=2, start_width=1.5, end_width=2.5),
        ]
        polyline = DXFCAD.LWPolyline(
            vertices=vertices, closed=True, constant_width=4, elevation=-7.5,
            extrusion=DXFCAD.ExtrusionDirection(2, -3, 4),
        )
        vertices.clear()
        snapshot = polyline.vertices
        self.assertEqual([self.vertex_values(v) for v in snapshot], [
            (1.25, -2.5, -0.5, 0.25, 0.75), (9, 3, 2, 1.5, 2.5),
        ])
        snapshot.clear()
        self.assertEqual(len(polyline.vertices), 2)
        self.assertTrue(polyline.closed)
        self.assertEqual(polyline.constant_width, 4)
        self.assertEqual(polyline.elevation, -7.5)
        self.assertEqual(self.direction_values(polyline.extrusion), (2, -3, 4))

    def test_snapshots_outlive_polyline(self):
        polyline = DXFCAD.LWPolyline([DXFCAD.LWPolylineVertex(4, 5)])
        vertex = polyline.vertices[0]
        direction = polyline.extrusion
        del polyline
        gc.collect()
        self.assertEqual(self.vertex_values(vertex), (4, 5, 0, 0, 0))
        self.assertEqual(self.direction_values(direction), (0, 0, 1))

    def test_read_only_native_state(self):
        polyline = DXFCAD.LWPolyline([DXFCAD.LWPolylineVertex(4, 5)])
        for name, value in [
            ("vertices", []), ("closed", True), ("constant_width", -1),
            ("elevation", 2), ("extrusion", DXFCAD.ExtrusionDirection()),
        ]:
            with self.subTest(property=name), self.assertRaises(AttributeError):
                setattr(polyline, name, value)
        with self.assertRaises(AttributeError):
            polyline.vertices[0].x = 100
        with self.assertRaises(AttributeError):
            polyline.extrusion.z = 0

    def test_domain_validation_becomes_value_error(self):
        for invalid in [-1, float("nan"), float("inf"), -float("inf")]:
            for field in ["constant_width", "start_width", "end_width"]:
                with self.subTest(field=field, value=invalid):
                    with self.assertRaises(ValueError):
                        if field == "constant_width":
                            DXFCAD.LWPolyline(constant_width=invalid)
                        else:
                            vertex = DXFCAD.LWPolylineVertex(**{field: invalid})
                            DXFCAD.LWPolyline([vertex])
        for invalid in [float("nan"), float("inf"), -float("inf")]:
            with self.assertRaises(ValueError):
                DXFCAD.LWPolyline(elevation=invalid)
            for field in ["x", "y", "bulge"]:
                with self.subTest(field=field, value=invalid), self.assertRaises(ValueError):
                    DXFCAD.LWPolyline([DXFCAD.LWPolylineVertex(**{field: invalid})])
            for field in ["x", "y", "z"]:
                with self.subTest(field=field, value=invalid), self.assertRaises(ValueError):
                    DXFCAD.LWPolyline(extrusion=DXFCAD.ExtrusionDirection(**{field: invalid}))
        with self.assertRaises(ValueError):
            DXFCAD.LWPolyline(extrusion=DXFCAD.ExtrusionDirection(0, 0, 0))

    def test_wrong_types_become_type_error(self):
        for construct in [
            lambda: DXFCAD.LWPolyline(vertices=[object()]),
            lambda: DXFCAD.LWPolyline(extrusion=(0, 0, 1)),
            lambda: DXFCAD.LWPolylineVertex(x="invalid"),
            lambda: DXFCAD.LWPolyline(unknown_argument=True),
        ]:
            with self.assertRaises(TypeError):
                construct()

    def test_segments_and_geometry_errors(self):
        polyline = DXFCAD.LWPolyline([
            DXFCAD.LWPolylineVertex(0, 0, 1),
            DXFCAD.LWPolylineVertex(2, 0),
        ], closed=True)
        segments = polyline.segments()
        self.assertEqual(len(segments), 2)
        self.assertEqual(segments[0].arc.radius, 1)
        self.assertAlmostEqual(segments[0].arc.sweep, math.pi)
        self.assertEqual(segments[0].arc.midpoint.y, -1)
        self.assertIsNone(segments[1].arc)
        with self.assertRaises(AttributeError):
            segments[0].arc.radius = 2
        with self.assertRaises(AttributeError):
            segments[0].start.x = 100
        midpoint = segments[0].arc.midpoint
        del segments, polyline
        gc.collect()
        self.assertEqual((midpoint.x, midpoint.y), (1, -1))
        coincident = DXFCAD.LWPolyline([
            DXFCAD.LWPolylineVertex(0, 0, 1), DXFCAD.LWPolylineVertex(0, 0)])
        with self.assertRaises(ValueError):
            coincident.segments()
        with self.assertRaises(OverflowError):
            DXFCAD.LWPolyline([
                DXFCAD.LWPolylineVertex(0, 0, sys.float_info.max),
                DXFCAD.LWPolylineVertex(2, 0),
            ]).segments()

    def test_world_segments_and_lifetime(self):
        polyline = DXFCAD.LWPolyline([
            DXFCAD.LWPolylineVertex(0, 0, -1), DXFCAD.LWPolylineVertex(2, 0),
        ], elevation=5, extrusion=DXFCAD.ExtrusionDirection(0, 2, 0))
        segment = polyline.world_segments()[0]
        self.assertEqual((segment.end.x, segment.end.y, segment.end.z), (-2, 5, 0))
        self.assertEqual(segment.arc.sweep, -math.pi)
        with self.assertRaises(AttributeError):
            segment.start.z = 100
        with self.assertRaises(AttributeError):
            segment.arc.normal.y = 0
        midpoint, normal = segment.arc.midpoint, segment.arc.normal
        del segment, polyline
        gc.collect()
        self.assertEqual((midpoint.x, midpoint.y, midpoint.z), (-1, 5, 1))
        self.assertEqual((normal.x, normal.y, normal.z), (0, 1, 0))

    def test_world_errors(self):
        with self.assertRaises(OverflowError):
            DXFCAD.LWPolyline([
                DXFCAD.LWPolylineVertex(-sys.float_info.max, -sys.float_info.max),
                DXFCAD.LWPolylineVertex(0, 0),
            ], elevation=sys.float_info.max,
                extrusion=DXFCAD.ExtrusionDirection(1, 1, 1)).world_segments()
        with self.assertRaises(ValueError):
            DXFCAD.LWPolyline([
                DXFCAD.LWPolylineVertex(0, 0, 1), DXFCAD.LWPolylineVertex(0, 0),
            ]).world_segments()

    def test_width_regions_and_native_width_presence(self):
        polyline = DXFCAD.LWPolyline([
            DXFCAD.LWPolylineVertex(0, 0, start_width=2, end_width=4),
            DXFCAD.LWPolylineVertex(4, 0),
        ], elevation=3)
        region = polyline.width_regions()[0]
        self.assertEqual(region.segment_index, 0)
        self.assertEqual(len(region.points), 4)
        self.assertEqual((region.points[0].x, region.points[0].y, region.points[0].z), (0, 1, 3))
        point = region.points[0]
        with self.assertRaises(AttributeError):
            point.y = 99
        del region, polyline
        gc.collect()
        self.assertEqual(point.y, 1)
        for invalid in [0, -1, float("nan"), float("inf")]:
            with self.assertRaises(ValueError):
                DXFCAD.LWPolyline().width_regions(invalid)
        explicit = DXFCAD.LWPolylineVertex(0, 0, has_widths=True)
        self.assertTrue(explicit.has_widths)
        self.assertEqual(DXFCAD.LWPolyline([explicit, DXFCAD.LWPolylineVertex(4, 0)],
                                         constant_width=2).width_regions(), [])

    @unittest.skipUnless("FreeCAD" in sys.modules, "Requires FreeCAD Part runtime")
    def test_world_geometry_against_freecad(self):
        import FreeCAD
        import Part

        vector = FreeCAD.Vector
        # Explicit known OCS frames, independently applied with FreeCAD's
        # Rotation/Placement API instead of reproducing the C++ transform.
        cases = [
            ((0, 0, 2), vector(1, 0, 0), vector(0, 1, 0)),
            ((0, 0, -2), vector(-1, 0, 0), vector(0, 1, 0)),
            ((2, 0, 0), vector(0, 1, 0), vector(0, 0, 1)),
            ((0, 2, 0), vector(-1, 0, 0), vector(0, 0, 1)),
            ((2, 2, 2), vector(-1, 1, 0).normalize(), vector(-1, -1, 2).normalize()),
            ((0.01, 0, math.sqrt(0.9999)), vector(math.sqrt(0.9999), 0, -0.01), vector(0, 1, 0)),
        ]

        def close_vectors(actual, expected):
            self.assertLess((vector(actual.x, actual.y, actual.z) - expected).Length, 1e-10)

        for extrusion, x_axis, y_axis in cases:
            normal = vector(*extrusion).normalize()
            for bulge in [-2.414213562373095, -0.4142135623730951, 1, 2.414213562373095]:
                with self.subTest(extrusion=extrusion, bulge=bulge):
                    polyline = DXFCAD.LWPolyline([
                        DXFCAD.LWPolylineVertex(3, -2, bulge),
                        DXFCAD.LWPolylineVertex(5, 2), DXFCAD.LWPolylineVertex(8, 3),
                    ], closed=True, elevation=-7, extrusion=DXFCAD.ExtrusionDirection(*extrusion))
                    placement = FreeCAD.Placement(normal * -7,
                        FreeCAD.Rotation(x_axis, y_axis, normal, "ZXY"))
                    local, world = polyline.segments(), polyline.world_segments()
                    self.assertEqual(len(world), len(local))
                    for ocs, wcs in zip(local, world):
                        close_vectors(wcs.start, placement.multVec(vector(ocs.start.x, ocs.start.y, 0)))
                        close_vectors(wcs.end, placement.multVec(vector(ocs.end.x, ocs.end.y, 0)))
                    arc = world[0].arc
                    close_vectors(arc.center, placement.multVec(vector(local[0].arc.center.x, local[0].arc.center.y, 0)))
                    close_vectors(arc.normal, normal)
                    edge = Part.Arc(*[vector(p.x, p.y, p.z)
                                      for p in [world[0].start, arc.midpoint, world[0].end]]).toShape()
                    self.assertTrue(edge.isValid())
                    close_vectors(arc.center, edge.Curve.Center)
                    close_vectors(edge.Curve.Axis, normal * math.copysign(1, bulge))
                    self.assertAlmostEqual(edge.Curve.Radius, arc.radius, places=10)
                    self.assertAlmostEqual(edge.Length, arc.radius * abs(arc.sweep), places=10)

    @unittest.skipUnless("FreeCAD" in sys.modules, "Requires FreeCAD Part runtime")
    def test_arcs_against_freecad_part(self):
        import FreeCAD
        import Part

        # Independent CAD-kernel check for minor, semicircular, and major arcs
        # in both directions. Endpoints are translated and rotated in OCS.
        for bulge in [-2.414213562373095, -1, -0.4142135623730951,
                      0.4142135623730951, 1, 2.414213562373095]:
            with self.subTest(bulge=bulge):
                segment = DXFCAD.LWPolyline([
                    DXFCAD.LWPolylineVertex(3, -2, bulge),
                    DXFCAD.LWPolylineVertex(5, 2),
                ]).segments()[0]
                arc = segment.arc
                points = [FreeCAD.Vector(p.x, p.y, 0)
                          for p in [segment.start, arc.midpoint, segment.end]]
                edge = Part.Arc(*points).toShape()
                self.assertTrue(edge.isValid())
                self.assertAlmostEqual(edge.Curve.Radius, arc.radius, places=10)
                self.assertAlmostEqual(edge.Curve.Center.x, arc.center.x, places=10)
                self.assertAlmostEqual(edge.Curve.Center.y, arc.center.y, places=10)
                self.assertAlmostEqual(edge.Length, arc.radius * abs(arc.sweep), places=10)
                self.assertAlmostEqual(edge.Curve.Axis.z, math.copysign(1, bulge), places=10)


if __name__ == "__main__":
    unittest.main()
