"""Block instance picking, live preview, transforms, snapping and export."""
import FreeCAD
import FreeCADGui
import DXFCAD
from pathlib import Path
import tempfile
from pivy import coin
from dxfcad_import import import_dxf
from dxfcad_export import export_dxf
from dxfcad_move_ui import show_move_tool
from dxfcad_transform_ui import show_transform_tool, RotateTaskPanel, ScaleTaskPanel, MirrorTaskPanel
from dxfcad_edit import copy_entities, apply_properties

profile=Path(FreeCAD.getUserAppDataDir())
assert str(profile) in DXFCAD.__file__
FreeCADGui.activateWorkbench("DXFCADWorkbench")
block=("0\nBLOCK\n2\nSymbol\n10\n0\n20\n0\n"
       "0\nLINE\n10\n0\n20\n0\n11\n4\n21\n0\n"
       "0\nCIRCLE\n10\n2\n20\n2\n40\n1\n0\nENDBLK\n")
raw="0\nINSERT\n5\nAB\n2\nSymbol\n10\n10\n20\n20\n41\n2\n42\n3\n"
drawing="0\nSECTION\n2\nBLOCKS\n"+block+"0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"+raw+"0\nENDSEC\n0\nEOF\n"
with tempfile.TemporaryDirectory(dir=profile) as temp:
    source=Path(temp)/"insert.dxf"
    source.write_text(drawing)
    doc=import_dxf(source)
    doc.UndoMode=1
    obj=next(o for o in doc.Objects if "DXFData" in o.PropertiesList)
    assert obj.Shape.isValid() and len(obj.Shape.Edges)==2
    view=FreeCADGui.activeDocument().activeView()
    view.setCameraType("Orthographic")
    camera=view.getCameraNode()
    camera.orientation.setValue(coin.SbRotation(0,0,0,1))
    camera.position.setValue(15,25,50)
    camera.focalDistance.setValue(50)
    camera.height.setValue(35)
    FreeCADGui.Selection.addSelection(obj)
    assert FreeCADGui.Selection.getSelection()==[obj]
    panel=show_move_tool([obj])
    manager=view.getViewer().getSoEventManager()
    def click(x,y):
        for state in (coin.SoButtonEvent.DOWN,coin.SoButtonEvent.UP):
            event=coin.SoMouseButtonEvent()
            event.setButton(coin.SoMouseButtonEvent.BUTTON1)
            event.setState(state)
            event.setPosition(coin.SbVec2s(*map(int,view.getPointOnScreen(FreeCAD.Vector(x,y,0)))))
            manager.processEvent(event)
    click(10,20)
    assert panel.viewport_tool.start_point is not None
    assert abs(panel.viewport_tool.start_point.x-10)<1e-8
    event=coin.SoLocation2Event()
    event.setPosition(coin.SbVec2s(*map(int,view.getPointOnScreen(FreeCAD.Vector(15,24,0)))))
    manager.processEvent(event)
    assert panel.viewport_tool.overlay is not None,panel.error.text()
    assert obj.DXFData==raw
    FreeCADGui.updateGui()
    view.saveImage(str(profile/"insert-preview.png"),900,600,"Current")
    click(15,24)
    assert obj.DXFData!=raw and obj.Shape.isValid(),panel.error.text()
    panel.reject()
    doc.undo(); doc.recompute()
    assert obj.DXFData==raw
    doc.redo(); doc.recompute()
    for panel_type,value in ((RotateTaskPanel,"90"),(ScaleTaskPanel,"2"),(MirrorTaskPanel,"0")):
        old=obj.DXFData
        panel=show_transform_tool([obj],panel_type)
        panel.parameter.setText(value)
        panel.create_numeric()
        assert not panel.error.text(),panel.error.text()
        assert obj.DXFData!=old and obj.Shape.isValid()
        assert obj.Placement.isIdentity()
        panel.reject()
    apply_properties([obj],"Symbols",-1,True)
    assert obj.DXFLayer=="Symbols" and obj.Shape.isValid()
    clone=copy_entities([obj],DXFCAD.Vector3D(10,0,0))[0]
    assert clone.Shape.isValid() and clone.DXFContext==block
    exported=Path(temp)/"result.dxf"
    export_dxf(exported,doc)
    result=DXFCAD.DXFDocument.read(str(exported))
    assert len(result.entities)==2 and not result.warnings
    assert all(e.type=="INSERT" for e in result.entities)
    assert block in result.serialize()
    FreeCADGui.Selection.clearSelection()
    FreeCAD.closeDocument(doc.Name)
assert not FreeCADGui.Control.activeDialog()
print("INSERT selection, mouse preview, transforms, properties, copy and export passed.")
