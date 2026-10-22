"""Saved DXF viewport UI and camera navigation, including restored snapshots."""
import FreeCAD
import FreeCADGui
import base64
import json
import os
from pathlib import Path
import tempfile
import DXFCAD
from dxfcad_import import import_dxf
from dxfcad_export import export_dxf
from dxfcad_layout_ui import SavedViewsDialog, LayerVisibilityPreview, apply_saved_view, selected_drawing

assert FreeCAD.GuiUp
profile = Path(FreeCAD.getUserAppDataDir())
assert str(profile) in DXFCAD.__file__
FreeCADGui.activateWorkbench("DXFCADWorkbench")
assert "DXFCAD_SavedViews" in FreeCADGui.listCommands()
viewport = ("0\nVIEWPORT\n5\nA\n67\n1\n410\nSheet 1\n69\n2\n68\n2\n10\n100\n20\n200\n"
            "40\n10\n41\n5\n45\n50\n12\n2\n22\n3\n17\n10\n27\n20\n37\n30\n51\n90\n")
frozen_viewport = viewport.replace("5\nA\n", "5\nA3\n").replace("69\n2\n", "69\n3\n")+"331\nAB\n331\nAC\n"
drawing = ("0\nSECTION\n2\nTABLES\n0\nLAYER\n5\nAB\n2\nFrozen\n62\n7\n"
           "0\nLAYER\n5\nAC\n2\nOff\n62\n-7\n0\nENDSEC\n"
           "0\nSECTION\n2\nENTITIES\n0\nLINE\n5\nB\n10\n0\n20\n0\n11\n10\n21\n5\n"
           "0\nLINE\n5\nD\n8\nFROZEN\n10\n2\n20\n0\n11\n2\n21\n5\n"
           "0\nLINE\n5\nE\n8\nFrozen\n10\n4\n20\n0\n11\n4\n21\n5\n"
           "0\nLINE\n5\nF\n8\nOff\n10\n6\n20\n0\n11\n6\n21\n5\n"+
           viewport+viewport.replace("5\nA\n", "5\nC\n").replace("69\n2\n", "69\n1\n")+
           frozen_viewport+"0\nENDSEC\n0\nEOF\n")
report = {"freecad":".".join(FreeCAD.Version()[:3]),"cases":[]}
report_path = profile/"saved-views-validation.json"


def verify_view(viewport, document, layer_preview=None):
    before = {o.Name:o.DXFData for o in document.Objects if "DXFData" in o.PropertiesList}
    apply_saved_view(viewport, document.Name, layer_preview)
    state = viewport.model_view()
    view = FreeCADGui.activeDocument().activeView()
    camera = view.getCameraNode()
    assert view.getCameraType() == "Orthographic"
    assert abs(float(camera.height.getValue())-state.height) < max(1e-4,state.height*1e-6)
    position = camera.position.getValue()
    distance = float(camera.focalDistance.getValue())
    center = FreeCAD.Vector(position[0],position[1],position[2])-FreeCAD.Vector(0,0,distance)
    expected = FreeCAD.Vector(state.center.x,state.center.y,state.center.z)
    assert (center-expected).Length < max(1e-4,expected.Length*1e-6)
    rotation = view.getCameraOrientation()
    axis = rotation.multVec(FreeCAD.Vector(1,0,0))
    assert (axis-FreeCAD.Vector(state.x_axis.x,state.x_axis.y,state.x_axis.z)).Length < 1e-6, (axis, rotation.Q, state.x_axis.x, state.x_axis.y)
    assert before == {o.Name:o.DXFData for o in document.Objects if "DXFData" in o.PropertiesList}


with tempfile.TemporaryDirectory(dir=profile) as temp:
    folder = Path(temp)
    source = folder/"views.dxf"
    source.write_text(drawing)
    doc = import_dxf(source)
    root = selected_drawing()
    objects = {o.DXFHandle:o for o in doc.Objects if "DXFData" in o.PropertiesList}
    objects["E"].ViewObject.Visibility = False
    layer_group = next(o for o in root.Group if o.Label == "Frozen")
    layer_group.Label = "Renamed group"
    off_group = next(o for o in root.Group if o.Label == "Off")
    baseline = {o.Name:o.ViewObject.Visibility for o in doc.Objects}
    dialog = SavedViewsDialog(root)
    assert dialog.table.rowCount() == 3 and dialog.go.isEnabled()
    dialog.show()
    FreeCADGui.updateGui()
    assert dialog.grab().save(str(profile/"saved-views-dialog.png"))
    dialog.go.click()
    assert dialog.message.text() == "Saved model view opened.", dialog.message.text()
    verify_view(dialog.viewports[0],doc)
    dialog.table.selectRow(2)
    assert dialog.go.isEnabled() and set(dialog.viewports[2].frozen_layers) == {"Frozen","Off"}
    dialog.go.click()
    assert dialog.message.text() == "Saved model view opened.", dialog.message.text()
    assert not objects["D"].ViewObject.Visibility
    assert not objects["E"].ViewObject.Visibility
    assert not objects["F"].ViewObject.Visibility and not off_group.ViewObject.Visibility
    dialog.table.selectRow(1)
    assert not dialog.go.isEnabled()
    camera_before = FreeCADGui.activeDocument().activeView().getCamera()
    try:
        apply_saved_view(dialog.viewports[1],doc.Name)
        assert False
    except ValueError:
        pass
    assert FreeCADGui.activeDocument().activeView().getCamera() == camera_before
    assert not objects["D"].ViewObject.Visibility
    dialog.table.selectRow(0)
    dialog.go.click()
    assert {o.Name:o.ViewObject.Visibility for o in doc.Objects} == baseline
    dialog.table.selectRow(2)
    dialog.go.click()
    dialog.close()
    assert {o.Name:o.ViewObject.Visibility for o in doc.Objects} == baseline
    # Escape/reject also restores the preview.
    rejected = SavedViewsDialog(root)
    rejected.table.selectRow(2)
    rejected.go.click()
    assert not objects["D"].ViewObject.Visibility
    rejected.reject()
    assert {o.Name:o.ViewObject.Visibility for o in doc.Objects} == baseline
    # Multiple drawings require an explicit root selection.
    import_dxf(source,doc)
    other_visibility = {o.Name:o.ViewObject.Visibility for o in doc.Objects
                        if getattr(o,"DXFSourceRoot",None) != root}
    preview = LayerVisibilityPreview(root)
    verify_view(dialog.viewports[2],doc,preview)
    assert not objects["D"].ViewObject.Visibility
    assert other_visibility == {o.Name:o.ViewObject.Visibility for o in doc.Objects
                                if getattr(o,"DXFSourceRoot",None) != root}
    preview.restore()
    try:
        selected_drawing()
        assert False
    except ValueError:
        pass
    FreeCADGui.Selection.addSelection(root)
    assert selected_drawing() == root
    FreeCADGui.Selection.clearSelection()
    saved = folder/"views.FCStd"
    root_name = root.Name
    doc.saveAs(str(saved))
    FreeCAD.closeDocument(doc.Name)
    source.unlink()
    doc = FreeCAD.openDocument(str(saved))
    root = doc.getObject(root_name)
    parsed = DXFCAD.DXFDocument.parse(base64.b64decode(root.DXFSourceData))
    verify_view(parsed.viewports[0],doc)
    restored_baseline = {o.Name:o.ViewObject.Visibility for o in doc.Objects}
    preview = LayerVisibilityPreview(root)
    verify_view(parsed.viewports[2],doc,preview)
    assert all(not o.ViewObject.Visibility for o in doc.Objects
               if getattr(o,"DXFSourceRoot",None) == root and parsed.viewports[2].freezes_layer(o.DXFLayer))
    preview.restore()
    assert {o.Name:o.ViewObject.Visibility for o in doc.Objects} == restored_baseline
    output = folder/"restored.dxf"
    export_dxf(output,doc,root)
    assert output.read_bytes() == DXFCAD.DXFDocument.parse(drawing).serialize_bytes()
    verify_view(parsed.viewports[2],doc,preview)
    FreeCAD.closeDocument(doc.Name)
    preview.restore()  # Closed documents must not leave stale object access.
    report["synthetic_passed"] = True
    report_path.write_text(json.dumps(report,indent=2))

for index, filename in enumerate(json.loads(os.environ.get("DXFCAD_VALIDATION_FILES","[]"))):
    source = Path(filename)
    original = source.read_bytes()
    doc = import_dxf(source)
    root = selected_drawing()
    parsed = DXFCAD.DXFDocument.parse(base64.b64decode(root.DXFSourceData))
    case = {"case":index+1,"viewports":len(parsed.viewports),"navigated":0,"frozen_layer_views":0}
    baseline = {o.Name:o.ViewObject.Visibility for o in doc.Objects}
    preview = LayerVisibilityPreview(root)
    for vp in parsed.viewports:
        if not vp.navigation_issue:
            verify_view(vp,doc,preview)
            assert all(not o.ViewObject.Visibility for o in doc.Objects
                       if getattr(o,"DXFSourceRoot",None) == root and vp.freezes_layer(o.DXFLayer))
            case["navigated"] += 1
            case["frozen_layer_views"] += bool(vp.frozen_layers)
            preview.restore()
            assert {o.Name:o.ViewObject.Visibility for o in doc.Objects} == baseline
    with tempfile.TemporaryDirectory(dir=profile) as temp:
        output = Path(temp)/"views-export.dxf"
        export_dxf(output,doc)
        assert output.read_bytes() == parsed.serialize_bytes()
    assert source.read_bytes() == original
    FreeCAD.closeDocument(doc.Name)
    report["cases"].append(case)
    report_path.write_text(json.dumps(report,indent=2))
