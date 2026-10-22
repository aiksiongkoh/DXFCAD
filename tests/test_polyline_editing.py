"""FreeCAD edit/save/restore/export lifecycle using synthetic drawings only."""

import FreeCAD

from pathlib import Path
import sys
import tempfile
import unittest

module_directory = Path(sys.argv.pop(1)).resolve()
sys.path.insert(0, str(module_directory))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import DXFCAD
from dxfcad_import import import_dxf
from dxfcad_edit import apply_polyline, create_polyline, create_line, create_circle, move_entities, copy_entities, drawing_roots
from dxfcad_export import export_dxf

assert Path(DXFCAD.__file__).parent == module_directory

NATIVE = ("0\nLWPOLYLINE\n5\nAB\n100\nAcDbEntity\n8\n0\n100\nAcDbPolyline\n"
          "90\n2\n70\n128\n38\n3\n10\n0\n20\n0\n42\n1\n91\n7\n10\n2\n20\n0\n91\n8\n"
          "1001\nTestApp\n1000\npreserve this\n")
OPAQUE = "0\nSPLINE\n5\nAC\n999\nunsupported and preserved\n"
BLOCKS = "0\nSECTION\n2\nBLOCKS\n0\nBLOCK\n2\nSavedBlock\n0\nENDBLK\n0\nENDSEC\n"
DRAWING = ("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n1\n0\nENDSEC\n" +
           BLOCKS + "0\nSECTION\n2\nENTITIES\n" + NATIVE + OPAQUE + "0\nENDSEC\n0\nEOF\n")


class EditingTests(unittest.TestCase):
    def imported_polyline(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "polyline.dxf"
            path.write_text(DRAWING)
            doc = import_dxf(path)
        return doc, next(o for o in doc.Objects if getattr(o, "DXFType", "") == "LWPOLYLINE")

    def test_polyline_plan_provenance_preview_and_export(self):
        from dxfcad_edit import apply_entity_update
        doc, obj = self.imported_polyline()
        session = DXFCAD.begin_polyline_edit(obj)
        self.assertEqual(session.original_data, NATIVE)
        model = session.polyline.inserted_vertex(1, DXFCAD.LWPolylineVertex(1, 1))
        plan = session.prepare(model, [0, -1, 1])
        self.assertEqual(obj.DXFData, NATIVE)
        raw = plan.preview_records[0][1]
        self.assertEqual(raw.count("\n91\n"), 2)
        self.assertIn("\n91\n7\n", raw)
        self.assertIn("\n91\n8\n", raw)
        self.assertIn("preserve this", raw)
        expected = plan.drawing_bytes[obj.DXFSourceRoot.Name]
        apply_entity_update(plan, "Edit polyline")
        self.assertEqual(DXFCAD.current_drawing_bytes(obj.DXFSourceRoot), expected)
        self.assertIn(OPAQUE.encode(), expected)
        self.assertEqual(obj.DXFHandle, "AB")
        self.assertEqual(obj.DXFSourceIndex, 0)
        with self.assertRaises(ValueError): session.prepare(model, [0, -1, 1])
        doc.undo(); doc.recompute()
        self.assertEqual(obj.DXFData, NATIVE)
        doc.redo(); doc.recompute()
        self.assertEqual(obj.DXFData, raw)

    def test_polyline_sessions_reject_changes_since_open_and_prepare(self):
        from dxfcad_edit import apply_entity_update
        doc, obj = self.imported_polyline()
        session = DXFCAD.begin_polyline_edit(obj)
        model = session.polyline.translated(DXFCAD.Vector3D(1, 0, 0))
        create_line(doc, DXFCAD.Vector3D(), DXFCAD.Vector3D(2, 0, 0), obj.DXFSourceRoot)
        with self.assertRaises(ValueError): session.prepare(model)
        self.assertEqual(obj.DXFData, NATIVE)
        session = DXFCAD.begin_polyline_edit(obj)
        plan = session.prepare(model)
        obj.Placement = FreeCAD.Placement(FreeCAD.Vector(1, 0, 0), FreeCAD.Rotation())
        with self.assertRaises(ValueError): apply_entity_update(plan, "Stale edit")
        self.assertEqual(obj.DXFData, NATIVE)

    def test_polyline_plan_invalid_provenance_and_display_rollback(self):
        from unittest.mock import patch
        doc, obj = self.imported_polyline()
        session = DXFCAD.begin_polyline_edit(obj)
        model = session.polyline.inserted_vertex(1, DXFCAD.LWPolylineVertex(1, 1))
        for origins in ([], [0, 1], [0, 0, 1], [0, 2, 1], [0, -2, 1]):
            with self.assertRaises(ValueError): session.prepare(model, origins)
        self.assertEqual(obj.DXFData, NATIVE)
        with patch("dxfcad_edit._check_display", side_effect=ValueError("display failure")):
            with self.assertRaisesRegex(ValueError, "display failure"):
                apply_polyline(obj, model, [0, -1, 1])
        self.assertEqual(obj.DXFData, NATIVE)
        line = create_line(doc, DXFCAD.Vector3D(), DXFCAD.Vector3D(2, 0, 0))
        with self.assertRaises(ValueError): DXFCAD.begin_polyline_edit(line)

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)

    def test_copy_selection_atomic_undo_export(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp)/"copy.dxf"
            extras = ("0\nARC\n5\nB1\n10\n0\n20\n0\n40\n2\n50\n0\n51\n90\n"
                      "0\nTEXT\n5\nB2\n10\n0\n20\n0\n40\n2\n1\nCopy text\n"
                      "0\nMTEXT\n5\nB3\n10\n0\n20\n0\n40\n2\n41\n10\n1\nCopy multiline\n")
            source.write_text(DRAWING.replace(OPAQUE,extras+OPAQUE))
            doc = import_dxf(source)
            create_line(doc,DXFCAD.Vector3D(0,0,0),DXFCAD.Vector3D(1,2,0))
            create_circle(doc,DXFCAD.Vector3D(2,3,0),1)
            objects = [o for o in doc.Objects if "DXFData" in o.PropertiesList]
            before = {o.Name:o.DXFData for o in objects}
            copies = copy_entities(objects,DXFCAD.Vector3D(4,5,6))
            self.assertEqual(len(copies),6)
            names = [o.Name for o in copies]
            self.assertEqual(before,{o.Name:o.DXFData for o in objects})
            for obj,clone in zip(objects,copies):
                self.assertEqual(clone.DXFSourceIndex,-1)
                self.assertFalse(DXFCAD.parse_entity(clone.DXFData).handle)
                a,b = DXFCAD.parse_entity(obj.DXFData).snap_points(),DXFCAD.parse_entity(clone.DXFData).snap_points()
                for old,new in zip(a,b):
                    self.assertAlmostEqual(new.position.x,old.position.x+4)
                    self.assertAlmostEqual(new.position.y,old.position.y+5)
                    self.assertAlmostEqual(new.position.z,old.position.z+6)
            doc.undo()
            self.assertTrue(all(doc.getObject(n) is None for n in names))
            doc.redo()
            self.assertTrue(all(doc.getObject(n) is not None for n in names))
            output = Path(temp)/"copies.dxf"
            export_dxf(output,doc)
            parsed = DXFCAD.DXFDocument.read(str(output))
            self.assertEqual(len(parsed.entities),12)
            self.assertEqual(len(set(e.handle for e in parsed.entities)),12)
            self.assertIn(OPAQUE,output.read_text())

    def test_move_selection_atomic_undo_persistence_and_export(self):
        from unittest.mock import patch
        import dxfcad_edit
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp)/"move.dxf"
            source.write_text(DRAWING)
            doc = import_dxf(source)
            poly = next(o for o in doc.Objects if getattr(o,"DXFType","") == "LWPOLYLINE")
            line = create_line(doc,DXFCAD.Vector3D(1,2,3),DXFCAD.Vector3D(4,6,3))
            circle = create_circle(doc,DXFCAD.Vector3D(1,2,3),2.5)
            selected = [poly,line,circle]
            names = [o.Name for o in selected]
            before = [o.DXFData for o in selected]
            origins = [[p.position for p in DXFCAD.parse_entity(o.DXFData).snap_points()] for o in selected]
            offset = DXFCAD.Vector3D(3,-5,7)
            moved = move_entities(selected+[line],offset)
            self.assertEqual(len(moved),3)
            after = [o.DXFData for o in selected]
            for obj,points in zip(selected,origins):
                self.assertTrue(obj.Placement.isIdentity())
                for a,b in zip(points,DXFCAD.parse_entity(obj.DXFData).snap_points()):
                    self.assertAlmostEqual(b.position.x,a.x+3)
                    self.assertAlmostEqual(b.position.y,a.y-5)
                    self.assertAlmostEqual(b.position.z,a.z+7)
            doc.undo()
            doc.recompute()
            self.assertEqual([o.DXFData for o in selected],before)
            doc.redo()
            doc.recompute()
            self.assertEqual([o.DXFData for o in selected],after)
            part = doc.addObject("Part::Feature","NonDXF")
            with self.assertRaises(ValueError):
                move_entities([line,part],offset)
            with self.assertRaises(ValueError):
                move_entities(selected,DXFCAD.Vector3D())
            line.Placement.Base = FreeCAD.Vector(1,0,0)
            with self.assertRaises(ValueError):
                move_entities(selected,offset)
            line.Placement = FreeCAD.Placement()
            self.assertEqual([o.DXFData for o in selected],after)
            # A renderer failure after the first write rolls back the entire selection.
            real_check = dxfcad_edit._check_display
            def fail_second(obj):
                real_check(obj)
                if obj == line:
                    raise ValueError("Simulated display failure")
            with patch.object(dxfcad_edit,"_check_display",side_effect=fail_second):
                with self.assertRaises(ValueError):
                    move_entities(selected,offset)
            self.assertEqual([o.DXFData for o in selected],after)
            output = Path(temp)/"moved.dxf"
            export_dxf(output,doc)
            parsed = DXFCAD.DXFDocument.read(str(output))
            self.assertEqual(len(parsed.entities),3)
            self.assertIn(OPAQUE,output.read_text())
            self.assertEqual(parsed.entities[0].handle,"AB")
            for entity,raw in zip(parsed.entities,after):
                self.assertEqual([(p.position.x,p.position.y,p.position.z) for p in entity.snap_points()],
                                 [(p.position.x,p.position.y,p.position.z) for p in DXFCAD.parse_entity(raw).snap_points()])
            saved = Path(temp)/"moved.FCStd"
            doc.saveAs(str(saved))
            FreeCAD.closeDocument(doc.Name)
            source.unlink()
            doc = FreeCAD.openDocument(str(saved))
            self.assertEqual([doc.getObject(n).DXFData for n in names],after)

    def test_circle_creation_undo_and_export(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp)/"source.dxf"
            source.write_text(DRAWING)
            doc = import_dxf(source)
            before = {o.Name:o.DXFData for o in doc.Objects if "DXFData" in o.PropertiesList}
            circle = create_circle(doc,DXFCAD.Vector3D(1,2,3),2.5)
            name, raw = circle.Name, circle.DXFData
            self.assertEqual(circle.DXFType,"CIRCLE")
            self.assertEqual(circle.DXFSourceIndex,-1)
            self.assertAlmostEqual(circle.Shape.Length,5*3.141592653589793)
            self.assertAlmostEqual(circle.Shape.BoundBox.ZMin,3)
            doc.undo()
            doc.recompute()
            self.assertIsNone(doc.getObject(name))
            doc.redo()
            doc.recompute()
            self.assertEqual(doc.getObject(name).DXFData,raw)
            self.assertEqual(before,{n:doc.getObject(n).DXFData for n in before})
            for radius in (0,-1,float("nan"),float("inf")):
                with self.assertRaises(ValueError):
                    create_circle(doc,DXFCAD.Vector3D(0,0,0),radius)
            output = Path(temp)/"out.dxf"
            export_dxf(output,doc)
            restored = DXFCAD.DXFDocument.read(str(output))
            self.assertEqual(restored.insertion_units,1)
            self.assertEqual(restored.entities[-1].type,"CIRCLE")
            self.assertIn("\n40\n2.5\n",restored.entities[-1].raw)
            self.assertIn(OPAQUE,output.read_text())
            saved = Path(temp)/"circle.FCStd"
            doc.saveAs(str(saved))
            FreeCAD.closeDocument(doc.Name)
            source.unlink()
            doc = FreeCAD.openDocument(str(saved))
            self.assertEqual(doc.getObject(name).DXFData,raw)
            self.assertAlmostEqual(doc.getObject(name).Shape.Length,5*3.141592653589793)

    def test_line_creation_undo_and_export(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp)/"source.dxf"
            source.write_text(DRAWING)
            doc = import_dxf(source)
            before = {o.Name:o.DXFData for o in doc.Objects if "DXFData" in o.PropertiesList}
            start, end = DXFCAD.Vector3D(1,2,3), DXFCAD.Vector3D(4,6,3)
            line = create_line(doc,start,end)
            name, raw = line.Name, line.DXFData
            self.assertEqual(line.DXFType,"LINE")
            self.assertEqual(line.DXFSourceIndex,-1)
            self.assertAlmostEqual(line.Shape.Length,5)
            self.assertEqual(line.TypeId,"DXFCAD::LineObject" if DXFCAD.NATIVE_DOCUMENT_OBJECTS else "Part::FeaturePython")
            doc.undo()
            doc.recompute()
            self.assertIsNone(doc.getObject(name))
            self.assertEqual(before,{o.Name:o.DXFData for o in doc.Objects if "DXFData" in o.PropertiesList})
            doc.redo()
            doc.recompute()
            self.assertEqual(doc.getObject(name).DXFData,raw)
            count = len(doc.Objects)
            with self.assertRaises(ValueError):
                create_line(doc,start,start)
            self.assertEqual(len(doc.Objects),count)
            saved = Path(temp)/"line.FCStd"
            doc.saveAs(str(saved))
            FreeCAD.closeDocument(doc.Name)
            source.unlink()
            doc = FreeCAD.openDocument(str(saved))
            doc.recompute()
            self.assertEqual(doc.getObject(name).DXFData,raw)
            output = Path(temp)/"line.dxf"
            export_dxf(output,doc)
            parsed = DXFCAD.DXFDocument.read(str(output))
            self.assertEqual(len(parsed.entities),2)
            self.assertEqual(parsed.entities[-1].type,"LINE")
            self.assertIn(OPAQUE,output.read_text())
            self.assertIn(BLOCKS,output.read_text())
            self.assertEqual(parsed.insertion_units,1)

    def test_edit_undo_restore_export_reimport(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "source.dxf"
            source.write_text(DRAWING, encoding="utf-8")
            doc = import_dxf(source)
            obj = next(o for o in doc.Objects if "DXFData" in o.PropertiesList)
            model = DXFCAD.read_lwpolyline(obj.DXFData)
            edited = model.translated(DXFCAD.Vector3D(2, 3, 4)).scaled(2).mirrored(DXFCAD.Point2D(), DXFCAD.Point2D(0,1))
            apply_polyline(obj, edited)
            native = obj.DXFData
            self.assertTrue(obj.Shape.isValid())
            self.assertIn("preserve this", native)
            self.assertEqual(obj.DXFHandle, "AB")
            doc.undo()
            doc.recompute()
            self.assertEqual(obj.DXFData, NATIVE)
            doc.redo()
            doc.recompute()
            self.assertEqual(obj.DXFData, native)
            new = create_polyline(doc, DXFCAD.LWPolyline([DXFCAD.LWPolylineVertex(), DXFCAD.LWPolylineVertex(5,0)]))
            self.assertEqual(new.DXFSourceIndex, -1)
            saved = Path(temp) / "saved.FCStd"
            doc.saveAs(str(saved))
            FreeCAD.closeDocument(doc.Name)
            source.unlink()
            restored = FreeCAD.openDocument(str(saved))
            restored.recompute()
            destination = Path(temp) / "exported.dxf"
            export_dxf(destination, restored)
            output = destination.read_text(encoding="utf-8")
            self.assertIn(OPAQUE, output)
            self.assertIn(BLOCKS, output)
            parsed = DXFCAD.DXFDocument.read(str(destination))
            self.assertEqual(len(parsed.entities), 2)
            self.assertEqual(parsed.insertion_units, 1)
            self.assertEqual(parsed.entities[0].raw, native)
            self.assertNotEqual(parsed.entities[1].handle, "AB")
            imported = import_dxf(destination)
            for item in imported.Objects:
                if "DXFData" in item.PropertiesList:
                    self.assertTrue(item.Shape.isValid())

    def test_create_undo_and_joined_widths(self):
        doc = FreeCAD.newDocument("Created")
        model = DXFCAD.LWPolyline([DXFCAD.LWPolylineVertex(0,0), DXFCAD.LWPolylineVertex(10,0),
                                  DXFCAD.LWPolylineVertex(10,10)], constant_width=2)
        obj = create_polyline(doc, model)
        self.assertTrue(obj.Shape.isValid())
        self.assertAlmostEqual(obj.Shape.Area, 40, places=7)
        doc.undo()
        self.assertEqual(len(doc.Objects), 0)
        doc.redo()
        doc.recompute()
        obj = next(o for o in doc.Objects if "DXFData" in o.PropertiesList)
        square = model.inserted_vertex(3, DXFCAD.LWPolylineVertex(0,10)).with_properties(True,2,0,DXFCAD.ExtrusionDirection())
        apply_polyline(obj, square, [-1,-1,-1,-1])
        self.assertTrue(obj.Shape.isValid())
        self.assertAlmostEqual(obj.Shape.Area, 80, places=7)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "new.dxf"
            export_dxf(path, doc)
            parsed = DXFCAD.DXFDocument.read(str(path))
            self.assertEqual(parsed.insertion_units, 4)
            self.assertTrue(DXFCAD.read_lwpolyline(parsed.entities[0].raw).closed)
            # Independent interoperability check: use FreeCAD's own C++ DXF
            # reader, not DXFCAD's parser, on the newly generated drawing.
            import Import
            independent = FreeCAD.newDocument("IndependentDXF")
            Import.readDXF(str(path), independent.Name, False)
            independent.recompute()
            shapes = [o.Shape for o in independent.Objects if hasattr(o, "Shape") and not o.Shape.isNull()]
            self.assertTrue(shapes, "FreeCAD's DXF reader did not read the exported polyline")
            self.assertTrue(all(shape.isValid() for shape in shapes))

    def test_failed_edits_and_export_leave_data_untouched(self):
        doc = FreeCAD.newDocument("Invalid")
        p = DXFCAD.LWPolyline([DXFCAD.LWPolylineVertex(0,0,1),DXFCAD.LWPolylineVertex(2,0)])
        obj = create_polyline(doc, p)
        original = obj.DXFData
        with self.assertRaises(ValueError):
            apply_polyline(obj, p.with_vertex(1, DXFCAD.LWPolylineVertex(0,0)))
        self.assertEqual(obj.DXFData, original)
        # Centerline data is valid, but width display cannot meet its bounded
        # subdivision requirement. The transaction must restore the old shape.
        with self.assertRaises(ValueError):
            apply_polyline(obj, p.with_properties(False, 1e300, 0, DXFCAD.ExtrusionDirection()))
        self.assertEqual(obj.DXFData, original)
        self.assertTrue(obj.Shape.isValid())
        self.assertFalse(obj.DisplayStatus.startswith("Error:"))
        with tempfile.TemporaryDirectory() as temp:
            destination = Path(temp) / "keep.dxf"
            destination.write_text("existing drawing", encoding="utf-8")
            obj.Placement.Base = FreeCAD.Vector(1,2,3)
            with self.assertRaises(ValueError):
                export_dxf(destination, doc)
            self.assertEqual(destination.read_text(), "existing drawing")
            obj.Placement = FreeCAD.Placement()
            obj.DXFData = "0\nSPLINE\n"
            with self.assertRaises(ValueError):
                export_dxf(destination, doc)
            self.assertEqual(destination.read_text(), "existing drawing")

    def test_curved_and_tapered_joins(self):
        doc = FreeCAD.newDocument("JoinedCurves")
        for bulge in (-0.414213562373095, 0.414213562373095):
            for closed in (False, True):
                with self.subTest(bulge=bulge, closed=closed):
                    model = DXFCAD.LWPolyline([
                        DXFCAD.LWPolylineVertex(0,0,bulge,0.4,0.6),
                        DXFCAD.LWPolylineVertex(2,0,-bulge,0.6,0.4),
                        DXFCAD.LWPolylineVertex(2,2,0,0.4,0.4),
                        DXFCAD.LWPolylineVertex(0,2,0,0.4,0.4),
                    ], closed)
                    obj = create_polyline(doc, model)
                    self.assertFalse(obj.DisplayStatus.startswith("Error:"), obj.DisplayStatus)
                    self.assertTrue(obj.Shape.isValid())
                    self.assertGreater(obj.Shape.Area, 0)
                    area = obj.Shape.Area
                    apply_polyline(obj, model.mirrored(DXFCAD.Point2D(), DXFCAD.Point2D(1,0)))
                    self.assertTrue(obj.Shape.isValid())
                    self.assertAlmostEqual(obj.Shape.Area, area, places=7)

    def test_deletion_and_separate_drawings(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "source.dxf"
            source.write_text(DRAWING)
            doc = import_dxf(source)
            first = drawing_roots(doc)[0]
            import_dxf(source, doc)
            with self.assertRaises(ValueError):
                export_dxf(Path(temp)/"ambiguous.dxf", doc)
            obj = next(o for o in doc.Objects if getattr(o,"DXFSourceRoot",None) == first)
            doc.removeObject(obj.Name)
            destination = Path(temp) / "deleted.dxf"
            export_dxf(destination, doc, first)
            parsed = DXFCAD.DXFDocument.read(str(destination))
            self.assertEqual(len(parsed.entities), 0)
            self.assertIn(OPAQUE, destination.read_text())


if __name__ == "__main__":
    unittest.main()
