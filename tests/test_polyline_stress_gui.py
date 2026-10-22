"""Drawing validation and timings in an isolated FreeCAD GUI.

Synthetic fixtures are generated locally. Optional DXFCAD_VALIDATION_FILES is
a JSON list of local DXF paths; original files are read only and never copied
into the repository. Reports contain counts/timings, not customer geometry.
"""
import FreeCAD
import FreeCADGui
from pathlib import Path
import json
import math
import os
import statistics
import tempfile
import time

import DXFCAD
from pivy import coin
from dxfcad_import import import_dxf
from dxfcad_edit import apply_polyline
from dxfcad_export import export_dxf
from dxfcad_polyline_ui import show_editor

assert FreeCAD.GuiUp
profile = Path(FreeCAD.getUserAppDataDir())
assert str(profile) in DXFCAD.__file__
FreeCADGui.activateWorkbench("DXFCADWorkbench")
report = {"freecad": ".".join(FreeCAD.Version()[:3]), "native_objects": DXFCAD.NATIVE_DOCUMENT_OBJECTS,
          "cases": [], "real_drawings_tested": 0}
report_path = profile / "drawing-validation.json"


def save_report():
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")


def synthetic():
    # Drawing construction here is a test fixture, not production DXF logic.
    vertices = [DXFCAD.LWPolylineVertex(i, 2*math.sin(i/10), 0.05 if i%3 == 0 else 0)
                for i in range(120)]
    records = [DXFCAD.create_lwpolyline(DXFCAD.LWPolyline(vertices))]
    for i in range(1000):
        x,y = (i%50)*3, (i//50)*3+20
        records.append(f"0\nLINE\n5\n{i+256:X}\n8\n0\n10\n{x}\n20\n{y}\n11\n{x+1}\n21\n{y+1}\n")
    for i in range(100):
        records.append(f"0\nCIRCLE\n5\n{i+4096:X}\n10\n{i%25*5}\n20\n{i//25*5+85}\n40\n0.5\n")
    for extrusion in [DXFCAD.ExtrusionDirection(), DXFCAD.ExtrusionDirection(1,2,3), DXFCAD.ExtrusionDirection(0,0,-2)]:
        wide = DXFCAD.LWPolyline([DXFCAD.LWPolylineVertex(0,-10,0.4,0.4,0.8,True),
                                  DXFCAD.LWPolylineVertex(10,-10,-0.2,0.8,0.4,True),
                                  DXFCAD.LWPolylineVertex(10,-5,0,0.4,0.4,True),
                                  DXFCAD.LWPolylineVertex(0,-5,0,0.4,0.4,True)],
                                 True, elevation=2, extrusion=extrusion)
        records.append(DXFCAD.create_lwpolyline(wide))
    opaque = "0\nSPLINE\n5\nF000\n999\nsynthetic opaque data\n"
    drawing = ("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n"
               "0\nSECTION\n2\nENTITIES\n" + "".join(records)+opaque+"0\nENDSEC\n0\nEOF\n")
    return drawing


def validate(path, folder, label, generated=False):
    case = {"case": label, "generated": generated, "passed": False}
    report["cases"].append(case)
    save_report()
    original_bytes = path.read_bytes()
    started = time.perf_counter()
    parsed = DXFCAD.DXFDocument.read(str(path))
    case["parse_seconds"] = time.perf_counter()-started
    case["source_bytes"] = len(original_bytes)
    case["supported_entities"] = len(parsed.entities)
    case["warning_count"] = len(parsed.warnings)
    started = time.perf_counter()
    doc = import_dxf(path)
    case["import_seconds"] = time.perf_counter()-started
    save_report()
    objects = [obj for obj in doc.Objects if "DXFData" in obj.PropertiesList]
    assert len(objects) == len(parsed.entities)
    assert all(not obj.DisplayStatus.startswith("Error:") for obj in objects)
    assert all(obj.Shape.isNull() or obj.Shape.isValid() for obj in objects)
    records = {obj.Name:obj.DXFData for obj in objects}
    output = folder / "unmodified.dxf"
    started = time.perf_counter()
    export_dxf(output, doc)
    case["export_seconds"] = time.perf_counter()-started
    assert output.read_bytes() == parsed.serialize_bytes(), "Unmodified export changed source group values"
    polyline = next((obj for obj in objects if obj.DXFType == "LWPOLYLINE"
                     and not obj.Shape.isNull()
                     and len(DXFCAD.read_lwpolyline(obj.DXFData).vertices) >= 2), None)
    if polyline:
        model = DXFCAD.read_lwpolyline(polyline.DXFData)
        view = FreeCADGui.activeDocument().activeView()
        normal = model.extrusion
        view.setCameraOrientation(FreeCAD.Rotation(FreeCAD.Vector(0,0,1),FreeCAD.Vector(normal.x,normal.y,normal.z)).Q)
        view.fitAll()
        panel = show_editor(polyline)
        FreeCADGui.updateGui()
        panel.move_in_view.click()
        editor = panel.viewport_editor
        assert editor is not None and editor.view is not None, panel.error.text()
        point = model.world_vertices()[0]
        screen = view.getPointOnScreen(FreeCAD.Vector(point.x,point.y,point.z))
        editor.index = 0
        times = []
        for _ in range(6):
            started = time.perf_counter()
            editor._snap_target(screen)
            times.append(time.perf_counter()-started)
        case["snap_lookup_median_ms"] = statistics.median(times)*1000
        case["snap_lookup_max_ms"] = max(times)*1000
        case["snap_lookup_first_ms"] = times[0]*1000
        save_report()
        started = time.perf_counter()
        editor._render(model)
        case["preview_ms"] = (time.perf_counter()-started)*1000
        editor.index = None
        panel.snap_vertices.setChecked(False)
        panel.snap_objects.setChecked(False)
        manager = view.getViewer().getSoEventManager()
        def mouse(position,state):
            event = coin.SoMouseButtonEvent()
            event.setButton(coin.SoMouseButtonEvent.BUTTON1)
            event.setState(state)
            event.setPosition(coin.SbVec2s(int(position[0]),int(position[1])))
            manager.processEvent(event)
        mouse(screen,coin.SoButtonEvent.DOWN)
        assert editor.index == 0, "Could not pick the real polyline vertex"
        destination = (screen[0]+24,screen[1]+18)
        motion = coin.SoLocation2Event()
        motion.setPosition(coin.SbVec2s(*destination))
        manager.processEvent(motion)
        assert editor.candidate is not None, panel.error.text()
        mouse(destination,coin.SoButtonEvent.UP)
        assert editor.index is None, "Drag release did not place the vertex"
        edited = panel._model()[0]
        assert (edited.vertices[0].x,edited.vertices[0].y) != (model.vertices[0].x,model.vertices[0].y)
        case["viewport_drag_checked"] = True
        panel.reject()
        assert records == {obj.Name:obj.DXFData for obj in objects}, "Preview/cancel changed native records"
        # Use the actual editing API and verify the whole drawing after restore.
        apply_polyline(polyline, edited)
        updated = polyline.DXFData
        doc.undo()
        doc.recompute()
        assert polyline.DXFData == records[polyline.Name]
        doc.redo()
        doc.recompute()
        assert polyline.DXFData == updated
        records[polyline.Name] = updated
    saved = folder / "restored.FCStd"
    started = time.perf_counter()
    doc.saveAs(str(saved))
    FreeCAD.closeDocument(doc.Name)
    if generated:
        path.unlink()  # Synthetic source is disposable; real sources stay read only.
    restored = FreeCAD.openDocument(str(saved))
    restored.recompute()
    case["save_restore_seconds"] = time.perf_counter()-started
    assert records == {obj.Name:obj.DXFData for obj in restored.Objects if "DXFData" in obj.PropertiesList}
    export_dxf(folder / "edited.dxf", restored)
    reparsed = DXFCAD.DXFDocument.read(str(folder / "edited.dxf"))
    assert len(reparsed.entities) == len(parsed.entities)
    assert len(reparsed.warnings) == len(parsed.warnings)
    if not generated:
        assert original_bytes == path.read_bytes()
    FreeCAD.closeDocument(restored.Name)
    case["passed"] = True
    save_report()


try:
    if os.environ.get("DXFCAD_VALIDATION_SKIP_SYNTHETIC") != "1":
        with tempfile.TemporaryDirectory(dir=profile) as temporary:
            folder = Path(temporary)
            source = folder / "synthetic.dxf"
            source.write_text(synthetic(), encoding="utf-8")
            validate(source,folder,"1104 entities, 120-vertex polyline, wide/tilted/reversed planes",True)
    failures = 0
    for index, filename in enumerate(json.loads(os.environ.get("DXFCAD_VALIDATION_FILES","[]"))):
        try:
            with tempfile.TemporaryDirectory(dir=profile) as temporary:
                validate(Path(filename),Path(temporary),f"local drawing {index+1}")
        except Exception as error:
            report["cases"][-1]["error"] = type(error).__name__ + ": " + str(error)
            failures += 1
            if os.environ.get("DXFCAD_VALIDATION_CONTINUE") != "1":
                raise
            FreeCADGui.Control.closeDialog()
            for name in list(FreeCAD.listDocuments()):
                FreeCAD.closeDocument(name)
        finally:
            report["real_drawings_tested"] += 1
            save_report()
    if failures:
        raise AssertionError(f"Validation failed for {failures} real drawing(s); see drawing-validation.json")
except BaseException as error:
    report["error"] = type(error).__name__ + ": " + str(error)
    save_report()
    raise
