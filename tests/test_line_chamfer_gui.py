"""Chamfer separate native lines: preview, rollback, undo, export and restore."""
import FreeCAD
import FreeCADGui
import DXFCAD
from pathlib import Path
from unittest.mock import patch
import math
import tempfile
import dxfcad_edit
from dxfcad_drafting_ui import LineChamferTaskPanel, show_drafting_tool
from dxfcad_import import import_dxf
from dxfcad_export import export_dxf

FreeCADGui.activateWorkbench("DXFCADWorkbench")
assert "DXFCAD_ChamferLines" in FreeCADGui.listCommands()
V=DXFCAD.Vector3D
profile=Path(FreeCAD.getUserAppDataDir())
first=DXFCAD.entity_properties(DXFCAD.create_line(V(0,0,3),V(10,0,3)),"0",1,True)
first+="1001\nChamferTest\n1000\nkeep source metadata\n"
second=DXFCAD.create_line(V(10,0,3),V(10,10,3))
short=DXFCAD.create_line(V(20,0,3),V(25,0,3))
apart=DXFCAD.create_line(V(30,5,3),V(30,10,3))
tilted=DXFCAD.LWPolyline([DXFCAD.LWPolylineVertex(0,20),DXFCAD.LWPolylineVertex(10,20),DXFCAD.LWPolylineVertex(10,30)],False,0,7,DXFCAD.ExtrusionDirection(1,2,3)).world_vertices()
tilt_a=DXFCAD.create_line(tilted[0],tilted[1])
tilt_b=DXFCAD.create_line(tilted[1],tilted[2])
source=DXFCAD.DXFDocument.empty(4).serialize(added=[first,second,short,apart,tilt_a,tilt_b])

def state(doc):
    return {o.Name:o.DXFData for o in doc.Objects if "DXFData" in o.PropertiesList}

with tempfile.TemporaryDirectory(dir=profile) as temp:
    folder=Path(temp)
    path=folder/"line-chamfers.dxf"
    path.write_text(source)
    doc=import_dxf(path)
    lines=[o for o in doc.Objects if getattr(o,"DXFType","")=="LINE"]
    original_handles=[o.DXFHandle for o in lines]
    before=state(doc)
    count=len(doc.Objects)
    view=FreeCADGui.activeDocument().activeView()
    view.viewTop()
    view.fitAll()
    panel=show_drafting_tool(LineChamferTaskPanel,lines[:2])
    panel.amount.setText("2")
    panel.second_amount.setText("3")
    panel.preview()
    assert not panel.error.text(),panel.error.text()
    assert panel.viewport_tool.overlay is not None and state(doc)==before and len(doc.Objects)==count
    FreeCADGui.updateGui()
    assert panel.form.grab().save(str(profile/"chamfer-lines-panel.png"))
    panel.reject()
    assert panel.viewport_tool.scene is None and state(doc)==before
    panel=show_drafting_tool(LineChamferTaskPanel,lines[:2])
    panel.amount.setText("20")
    panel.apply()
    assert "fit" in panel.error.text() and state(doc)==before and len(doc.Objects)==count
    panel.amount.setText("2")
    panel.second_amount.setText("20")
    panel.apply()
    assert "fit" in panel.error.text() and state(doc)==before and len(doc.Objects)==count
    panel.second_amount.setText("3")
    # Fail after actually creating the connector: both edited lines and the new
    # object must roll back together, including group membership.
    create=DXFCAD.create_feature
    def fail_after_connector(*args,**kwargs):
        create(*args,**kwargs)
        raise RuntimeError("Injected connector creation failure")
    with patch.object(DXFCAD,"create_feature",fail_after_connector):
        panel.apply()
    assert "Injected" in panel.error.text(),panel.error.text()
    assert state(doc)==before and len(doc.Objects)==count and panel.created_connector is None
    connectors=[]
    for pair in (lines[:2],lines[2:4],lines[4:6]):
        previous=state(doc)
        if pair!=lines[:2]:
            panel=show_drafting_tool(LineChamferTaskPanel,pair)
            panel.amount.setText("2")
            panel.second_amount.setText("3")
        panel.preview()
        assert not panel.error.text(),panel.error.text()
        assert state(doc)==previous and panel.viewport_tool.overlay is not None
        panel.apply()
        assert panel.created_connector is not None,panel.error.text()
        assert not FreeCADGui.Control.activeDialog() and panel.viewport_tool.scene is None
        connector=panel.created_connector
        assert connector is not None and connector.DXFType=="LINE" and connector.DXFSourceIndex==-1
        assert abs(connector.Shape.Length-math.sqrt(13))<1e-6 and connector.Shape.isValid()
        points=DXFCAD.parse_entity(connector.DXFData).snap_points()
        a=DXFCAD.parse_entity(pair[0].DXFData).snap_points()[1].position
        b=DXFCAD.parse_entity(pair[1].DXFData).snap_points()[0].position
        assert max(abs(points[0].position.x-a.x),abs(points[0].position.y-a.y),abs(points[0].position.z-a.z))<1e-7
        assert max(abs(points[1].position.x-b.x),abs(points[1].position.y-b.y),abs(points[1].position.z-b.z))<1e-7
        name=connector.Name
        after=state(doc)
        doc.undo()
        assert state(doc)==previous and doc.getObject(name) is None
        doc.redo()
        assert state(doc)==after and doc.getObject(name) is not None
        connectors.append(name)
    assert [o.DXFHandle for o in lines]==original_handles
    assert "keep source metadata" in lines[0].DXFData
    assert tuple(doc.getObject(connectors[0]).ViewObject.LineColor)==tuple(lines[0].ViewObject.LineColor)
    # Bevel points extend the two initially separated source segments.
    assert abs(DXFCAD.parse_entity(lines[2].DXFData).snap_points()[1].position.x-28)<1e-7
    assert abs(DXFCAD.parse_entity(lines[3].DXFData).snap_points()[0].position.y-3)<1e-7
    output=folder/"chamfered.dxf"
    export_dxf(output,doc)
    exported=DXFCAD.DXFDocument.read(str(output))
    assert len(exported.entities)==9 and all(e.type=="LINE" for e in exported.entities)
    assert [e.handle for e in exported.entities[:6]]==original_handles and len({e.handle for e in exported.entities})==9
    import Import
    independent=FreeCAD.newDocument("IndependentChamferRead")
    Import.readDXF(str(output),independent.Name,False)
    assert sum(len(o.Shape.Edges) for o in independent.Objects if "Shape" in o.PropertiesList)==9
    FreeCAD.closeDocument(independent.Name)
    FreeCAD.setActiveDocument(doc.Name)
    saved=folder/"chamfered.FCStd"
    expected=state(doc)
    doc.saveAs(str(saved))
    FreeCAD.closeDocument(doc.Name)
    path.unlink()
    doc=FreeCAD.openDocument(str(saved))
    assert state(doc)==expected and all(doc.getObject(name).Shape.isValid() for name in connectors)
    export_dxf(folder/"restored.dxf",doc)
    assert len(DXFCAD.DXFDocument.read(str(folder/"restored.dxf")).entities)==9
    FreeCAD.closeDocument(doc.Name)
    # The shared exporter currently permits new entities only in R2000+.
    # Preflight must reject an older source before editing either source line.
    legacy=folder/"legacy.dxf"
    legacy.write_text(source.replace("AC1015","AC1009"))
    doc=import_dxf(legacy)
    lines=[o for o in doc.Objects if getattr(o,"DXFType","")=="LINE"]
    previous=state(doc)
    panel=show_drafting_tool(LineChamferTaskPanel,lines[:2])
    panel.amount.setText("2")
    panel.second_amount.setText("3")
    panel.apply()
    assert "R2000" in panel.error.text(),panel.error.text()
    assert state(doc)==previous and panel.created_connector is None
    panel.reject()
    other=doc.addObject("App::DocumentObjectGroup","OtherDXFDrawing")
    other.addProperty("App::PropertyString","DXFSourceData","DXF")
    import base64
    other.DXFSourceData=base64.b64encode(DXFCAD.DXFDocument.empty(4).serialize_bytes()).decode("ascii")
    alien=dxfcad_edit.create_line(doc,V(10,0,3),V(10,10,3),root=other)
    previous=state(doc)
    try:
        LineChamferTaskPanel([lines[0],alien])
        raise AssertionError("Cross-drawing chamfer panel accepted")
    except ValueError as error:
        assert "same DXFCAD drawing" in str(error)
    result=DXFCAD.chamfer_lines(lines[0].DXFData,alien.DXFData,2,3)
    try:
        dxfcad_edit.apply_line_chamfer([(lines[0],result.first),(alien,result.second)],result.connector)
        raise AssertionError("Cross-drawing chamfer commit accepted")
    except ValueError as error:
        assert "same DXFCAD drawing" in str(error)
    assert state(doc)==previous
    FreeCAD.closeDocument(doc.Name)
print("Separate LINE chamfers: preview/cancel, trim/extend, tilted planes, rollback, undo/redo, native export and restore passed.")
