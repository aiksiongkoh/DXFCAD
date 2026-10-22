"""Three-point ARC mouse picking and numeric creation through the task panel."""
import FreeCAD
import FreeCADGui
import DXFCAD
from pivy import coin
from pathlib import Path
import math
import tempfile
from dxfcad_arc_ui import show_arc_tool
from dxfcad_import import import_dxf
from dxfcad_export import export_dxf

FreeCADGui.activateWorkbench("DXFCADWorkbench")
assert "DXFCAD_DrawArc" in FreeCADGui.listCommands()
V=DXFCAD.Vector3D
profile=Path(FreeCAD.getUserAppDataDir())
targets=[DXFCAD.create_line(V(5,0,0),V(7,0,0)),DXFCAD.create_line(V(0,5,0),V(2,5,0)),
         DXFCAD.create_line(V(-5,0,0),V(-7,0,0)),DXFCAD.create_line(V(-5,10,0),V(-7,10,0)),
         DXFCAD.create_line(V(8,-5,3),V(10,-5,3)),DXFCAD.create_circle(V(0,-5,0),0.4)]
with tempfile.TemporaryDirectory(dir=profile) as temp:
    folder=Path(temp)
    source=folder/"arc-source.dxf"
    DXFCAD.DXFDocument.empty(4).write(str(source),added=targets)
    doc=import_dxf(source)
    original={o.Name:o.DXFData for o in doc.Objects if "DXFData" in o.PropertiesList}
    view=FreeCADGui.activeDocument().activeView()
    view.setCameraType("Orthographic")
    camera=view.getCameraNode()
    camera.orientation.setValue(coin.SbRotation(0,0,0,1))
    camera.position.setValue(0,1,50)
    camera.focalDistance.setValue(50)
    camera.height.setValue(30)
    panel=show_arc_tool()
    tool=panel.viewport_tool
    FreeCADGui.updateGui()
    assert panel.form.grab().save(str(profile/"draw-arcs-panel.png"))
    manager=view.getViewer().getSoEventManager()
    def screen(x,y,z=0):
        return view.getPointOnScreen(FreeCAD.Vector(x,y,z))
    def click(x,y,button=coin.SoMouseButtonEvent.BUTTON1,ctrl=False):
        for state in (coin.SoButtonEvent.DOWN,coin.SoButtonEvent.UP):
            event=coin.SoMouseButtonEvent()
            event.setButton(button)
            event.setState(state)
            event.setCtrlDown(ctrl)
            event.setPosition(coin.SbVec2s(*map(int,screen(x,y))))
            manager.processEvent(event)
    def move(x,y):
        event=coin.SoLocation2Event()
        event.setPosition(coin.SbVec2s(*map(int,screen(x,y))))
        manager.processEvent(event)
    def escape():
        event=coin.SoKeyboardEvent()
        event.setKey(coin.SoKeyboardEvent.ESCAPE)
        event.setState(coin.SoButtonEvent.DOWN)
        manager.processEvent(event)
    def arcs():
        return [o for o in doc.Objects if getattr(o,"DXFType","")=="ARC"]

    click(5,0)
    assert tool.start_point is not None and tool.start_point.x==5
    assert not panel.elevation.isEnabled()
    click(5,0)
    assert tool.through_point is None and not arcs() and panel.error.text()
    move(0,5)
    assert tool.candidate is not None and tool.overlay is not None and not arcs()
    click(0,5)
    assert tool.through_point is not None and tool.through_point.y==5
    move(-5,10)
    assert tool.candidate is None and "collinear" in panel.error.text()
    click(-5,10)
    assert not arcs() and tool.through_point is not None
    move(8,-5)
    assert tool.candidate is None and "plane" in panel.error.text()
    click(8,-5)
    assert not arcs() and tool.through_point is not None
    move(-5,0)
    assert tool.candidate is not None and tool.overlay is not None and not arcs(),panel.error.text()
    click(-5,0)
    assert len(arcs())==1 and tool.start_point is None and tool.through_point is None,panel.error.text()
    assert abs(arcs()[0].Shape.Length-5*math.pi)<1e-6
    assert panel.elevation.isEnabled() and tool.overlay is None
    click(5,0); click(0,-5); click(-5,0)
    assert len(arcs())==2,panel.error.text()
    assert abs(arcs()[1].Shape.Length-5*math.pi)<1e-6
    assert DXFCAD.parse_entity(arcs()[1].DXFData).snap_points()[3].position.y<0
    click(5,0); click(-5,0); click(0,5)
    assert len(arcs())==3 and abs(arcs()[2].Shape.Length-7.5*math.pi)<1e-6,panel.error.text()
    click(5,0); click(0,5)
    move(-5,0)
    escape()
    assert tool.start_point is None and tool.through_point is None and tool.overlay is None and len(arcs())==3
    click(5,0); click(0,5)
    click(-5,0,coin.SoMouseButtonEvent.BUTTON2)
    assert tool.start_point is None and tool.through_point is None and len(arcs())==3
    click(5,0,ctrl=True)
    assert tool.start_point is None
    escape()
    assert tool.callback is None and not panel.pick.isChecked()
    panel.pick.click()
    assert tool.callback is not None

    # Switching entry mode cancels pending picking and releases its callback.
    click(5,0); click(0,5)
    panel.mode.setCurrentIndex(1)
    assert tool.callback is None and tool.start_point is None and tool.through_point is None
    panel.radius.setText("-2")
    panel.create_numeric()
    assert len(arcs())==3 and "positive" in panel.error.text()
    panel.radius.setText("nan")
    panel.create_numeric()
    assert len(arcs())==3 and "finite" in panel.error.text()
    panel.radius.setText("3")
    panel.start_angle.setText("0"); panel.end_angle.setText("360")
    panel.create_numeric()
    assert len(arcs())==3 and ("CIRCLE" in panel.error.text() or "distinct" in panel.error.text())
    panel.start_angle.setText("10"); panel.end_angle.setText("350")
    panel.clockwise.setChecked(True)
    panel.elevation.setText("2")
    panel.create_numeric()
    assert len(arcs())==4 and not panel.error.text(),panel.error.text()
    assert abs(arcs()[-1].Shape.Length-math.pi/3)<1e-6
    assert abs(arcs()[-1].Shape.BoundBox.ZMin-2)<1e-7
    panel.mode.setCurrentIndex(0)
    for field,value in zip([*panel.start_fields,*panel.through_fields,*panel.end_fields],("2","0","0","2","-2","0")):
        field.setText(value)
    panel.create_numeric()
    assert len(arcs())==5 and abs(arcs()[-1].Shape.Length-2*math.pi)<1e-6,panel.error.text()
    obj=arcs()[-1]
    name,raw=obj.Name,obj.DXFData
    panel.reject()
    assert tool.callback is None and tool.overlay is None and not tool.cache
    doc.undo()
    assert doc.getObject(name) is None and len(arcs())==4
    doc.redo()
    assert doc.getObject(name).DXFData==raw and len(arcs())==5
    assert original=={name:doc.getObject(name).DXFData for name in original}
    output=folder/"arcs.dxf"
    export_dxf(output,doc)
    parsed=DXFCAD.DXFDocument.read(str(output))
    assert len(parsed.entities)==11 and sum(e.type=="ARC" for e in parsed.entities)==5
    assert len({e.handle for e in parsed.entities})==11
    import Import
    independent=FreeCAD.newDocument("IndependentArcs")
    Import.readDXF(str(output),independent.Name,False)
    assert sum(len(o.Shape.Edges) for o in independent.Objects if "Shape" in o.PropertiesList)==11
    FreeCAD.closeDocument(independent.Name)
    FreeCAD.setActiveDocument(doc.Name)
    saved=folder/"arcs.FCStd"
    expected={o.Name:o.DXFData for o in arcs()}
    doc.saveAs(str(saved))
    FreeCAD.closeDocument(doc.Name)
    source.unlink()
    doc=FreeCAD.openDocument(str(saved))
    assert all(doc.getObject(name).DXFData==raw and doc.getObject(name).Shape.isValid() for name,raw in expected.items())
    export_dxf(folder/"restored.dxf",doc)
    assert len(DXFCAD.DXFDocument.read(str(folder/"restored.dxf")).entities)==11

    # Document switching/closure must release both pending points, observers,
    # the borrowed scene reference and the Coin event callback.
    panel=show_arc_tool()
    tool=panel.viewport_tool
    tool.start_point=V(5,0,0); tool.through_point=V(0,5,0)
    other=FreeCAD.newDocument("ArcOtherView")
    tool.check_view()
    assert tool.callback is None and tool.through_point is None
    FreeCAD.closeDocument(other.Name)
    FreeCAD.setActiveDocument(doc.Name)
    panel.reject()
    panel=show_arc_tool()
    tool=panel.viewport_tool
    tool.start_point=V(5,0,0); tool.through_point=V(0,5,0)
    FreeCAD.closeDocument(doc.Name)
    assert tool.callback is None and tool.scene is None and tool.through_point is None
    panel.reject()
    panel=show_arc_tool()
    assert panel.owns_document and not panel.document.Objects
    panel.reject()
    assert not FreeCAD.listDocuments()
print("ARC drawing: three-point picking, direction/major arcs, numeric entry, cancel, undo, export and cleanup passed.")
