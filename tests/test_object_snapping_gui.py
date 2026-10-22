"""Native midpoints/intersections in viewport picking, with live cache invalidation."""
import FreeCAD
import FreeCADGui
import DXFCAD
from pivy import coin
from pathlib import Path
import tempfile
from dxfcad_edit import create_line, create_circle, _create_entity
from dxfcad_view_snap import ObjectSnapper, _INDEX
from dxfcad_line_ui import show_line_tool
from dxfcad_export import export_dxf

FreeCADGui.activateWorkbench("DXFCADWorkbench")
doc=FreeCAD.newDocument("ObjectSnapping")
V=DXFCAD.Vector3D
h=create_line(doc,V(0,0,0),V(10,0,0))
v=create_line(doc,V(3,-4,0),V(3,5,0))
circle=create_circle(doc,V(20,0,0),5)
circle2=create_circle(doc,V(26,0,0),5)
cut=create_line(doc,V(10,3,0),V(30,3,0))
bulge=_create_entity(doc,"0\nLWPOLYLINE\n90\n2\n70\n0\n10\n35\n20\n-5\n42\n1\n10\n45\n20\n-5\n",None,"LWPolyline")
arc=_create_entity(doc,"0\nARC\n10\n0\n20\n15\n40\n5\n50\n0\n51\n180\n",None,"Arc")
bulge_cut=create_line(doc,V(37,-15,0),V(37,0,0))
apparent_h=create_line(doc,V(45,0,0),V(55,0,0))
apparent_v=create_line(doc,V(47,-4,3),V(47,8,3))
doc.recompute()
originals={obj.Name:obj.DXFData for obj in doc.Objects if "DXFData" in obj.PropertiesList}
view=FreeCADGui.activeDocument().activeView()
view.setCameraType("Orthographic")
camera=view.getCameraNode()
camera.orientation.setValue(coin.SbRotation(0,0,0,1))
camera.position.setValue(25,2,100)
camera.focalDistance.setValue(100)
camera.height.setValue(45)
FreeCADGui.updateGui()
cache={}

def screen(x,y,z=0):
    return view.getPointOnScreen(FreeCAD.Vector(x,y,z))

def snap(x,y,kind,z=0):
    target=ObjectSnapper.nearest(doc.Name,view,screen(x,y,z),cache)
    assert target is not None,(x,y,kind)
    assert kind in target[1],target[1]
    assert abs(target[0].x-x)<1e-7 and abs(target[0].y-y)<1e-7,(x,y,target)
    return target

snap(5,0,"midpoint")
snap(0,20,"midpoint")
snap(40,-10,"midpoint")
snap(3,0,"intersection")
snap(16,3,"intersection")
snap(24,3,"intersection")
snap(23,4,"intersection")
snap(37,-9,"intersection")
camera.height.setValue(35)
snap(3,0,"intersection")
camera.position.setValue(28,3,100)
snap(5,0,"midpoint")
camera.height.setValue(45)
camera.position.setValue(25,2,100)
view.setCameraType("Perspective")
camera=view.getCameraNode()
camera.orientation.setValue(coin.SbRotation(0,0,0,1))
camera.position.setValue(25,2,100)
snap(3,0,"intersection")
snap(5,0,"midpoint")
view.setCameraType("Orthographic")
camera=view.getCameraNode()
camera.orientation.setValue(coin.SbRotation(0,0,0,1))
camera.position.setValue(25,2,100)
camera.height.setValue(45)
assert not (ObjectSnapper.nearest(doc.Name,view,screen(47,0),cache) or (None,""))[1].endswith("intersection")

# Live visibility, layer visibility, data, placement, label, undo and deletion
# must invalidate pair results as well as individual midpoint points.
v.ViewObject.Visibility=False
assert ObjectSnapper.nearest(doc.Name,view,screen(3,0),cache,radius=3) is None
v.ViewObject.Visibility=True
snap(3,0,"intersection")
group=next(parent for parent in v.InList if "Group" in parent.PropertiesList)
group.ViewObject.Visibility=False
assert ObjectSnapper.nearest(doc.Name,view,screen(3,0),cache,radius=3) is None
group.ViewObject.Visibility=True
snap(3,0,"intersection")
v.Placement=FreeCAD.Placement(FreeCAD.Vector(1,0,0),FreeCAD.Rotation())
assert ObjectSnapper.nearest(doc.Name,view,screen(3,0),cache,radius=3) is None
snap(4,0,"intersection")
v.Placement=FreeCAD.Placement()
v.Placement=FreeCAD.Placement(FreeCAD.Vector(),FreeCAD.Rotation(FreeCAD.Vector(0,0,1),90))
snap(-0.5,3,"midpoint")
v.Placement=FreeCAD.Placement()
v.Label="Live boundary"
assert "Live boundary" in snap(3,0,"intersection")[1]
doc.openTransaction("Change snap boundary")
v.DXFData=DXFCAD.create_line(V(2,-4,0),V(2,5,0))
doc.recompute()
doc.commitTransaction()
snap(2,0,"intersection")
doc.undo()
doc.recompute()
snap(3,0,"intersection")
doc.redo()
doc.recompute()
snap(2,0,"intersection")
doc.undo()
doc.recompute()
excluded=ObjectSnapper.nearest(doc.Name,view,screen(3,0),cache,exclude=v,radius=3)
assert excluded is None
snap(3,0,"intersection")
name=v.Name
doc.removeObject(name)
doc.recompute()
assert ObjectSnapper.nearest(doc.Name,view,screen(3,0),cache,radius=3) is None
assert name not in cache and all(not isinstance(key,tuple) or name not in key[1:] for key in cache)
v=create_line(doc,V(3,-4,0),V(3,5,0))
doc.recompute()
originals={obj.Name:obj.DXFData for obj in doc.Objects if "DXFData" in obj.PropertiesList}

# Pick through real Coin events: a new line starts at an intersection and ends
# at the midpoint of a curved segment. Preview changes no native records.
panel=show_line_tool()
FreeCADGui.updateGui()
manager=view.getViewer().getSoEventManager()
def click(x,y):
    for state in (coin.SoButtonEvent.DOWN,coin.SoButtonEvent.UP):
        event=coin.SoMouseButtonEvent()
        event.setButton(coin.SoMouseButtonEvent.BUTTON1)
        event.setState(state)
        event.setPosition(coin.SbVec2s(*map(int,screen(x,y))))
        manager.processEvent(event)
click(3,0)
assert panel.viewport_tool.start_point is not None,panel.error.text()
assert abs(panel.viewport_tool.start_point.x-3)<1e-7
assert originals=={name:doc.getObject(name).DXFData for name in originals}
event=coin.SoLocation2Event()
event.setPosition(coin.SbVec2s(*map(int,screen(40,-10))))
manager.processEvent(event)
assert "midpoint" in panel.status.text(),panel.status.text()
click(40,-10)
assert not panel.error.text(),panel.error.text()
created=next(obj for obj in doc.Objects if "DXFData" in obj.PropertiesList and obj.Name not in originals)
points=DXFCAD.parse_entity(created.DXFData).snap_points()
assert abs(points[0].position.x-3)<1e-7 and abs(points[1].position.x-40)<1e-7 and abs(points[1].position.y+10)<1e-7
raw,name=created.DXFData,created.Name
tool_index=panel.viewport_tool.cache[_INDEX]
panel.reject()
assert not tool_index.alive and not panel.viewport_tool.cache
doc.undo()
assert doc.getObject(name) is None
doc.redo()
assert doc.getObject(name).DXFData==raw
assert originals=={name:doc.getObject(name).DXFData for name in originals}
with tempfile.TemporaryDirectory(dir=FreeCAD.getUserAppDataDir()) as temp:
    out=Path(temp)/"snapped.dxf"
    export_dxf(out,doc)
    parsed=DXFCAD.DXFDocument.read(str(out))
    assert len(parsed.entities)==len(originals)+1
    assert any(e.type=="LINE" and abs(e.snap_points()[0].position.x-3)<1e-7 and abs(e.snap_points()[1].position.y+10)<1e-7 for e in parsed.entities)
index=cache[_INDEX]
ObjectSnapper.clear(cache)
assert not cache and not index.alive
snap(3,0,"endpoint")
index=cache[_INDEX]
FreeCAD.closeDocument(doc.Name)
assert not index.alive and not index.entries
print("Midpoint/intersection picking, curved segments, live cache invalidation, undo and export passed.")
