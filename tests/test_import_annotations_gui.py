"""Synthetic child-style presentation, cached radial dimension and leader GUI."""
import FreeCAD
import FreeCADGui
import DXFCAD
from pathlib import Path
import tempfile
import os
import json
from dxfcad_import import import_dxf

profile=Path(FreeCAD.getUserAppDataDir())
assert str(profile) in DXFCAD.__file__
FreeCADGui.activateWorkbench('DXFCADWorkbench')
layers='0\nLAYER\n2\nHost\n62\n1\n0\nLAYER\n2\nChild\n62\n3\n'
style='0\nDIMSTYLE\n2\nSynthetic\n41\n2.5\n'
blocks=('0\nBLOCK\n2\nB\n10\n0\n20\n0\n'
        '0\nLINE\n10\n0\n20\n0\n11\n3\n21\n0\n'
        '0\nLINE\n8\nChild\n10\n0\n20\n1\n11\n3\n21\n1\n'
        '0\nLINE\n62\n0\n10\n0\n20\n2\n11\n3\n21\n2\n0\nENDBLK\n'
        '0\nBLOCK\n2\n*D1\n10\n0\n20\n0\n'
        '0\nLINE\n62\n0\n10\n8\n20\n0\n11\n8\n21\n3\n0\nENDBLK\n')
insert='0\nINSERT\n5\nAB\n2\nB\n8\nHost\n62\n4\n10\n0\n20\n0\n'
leader='0\nLEADER\n5\nCD\n3\nSynthetic\n76\n3\n10\n0\n20\n4\n10\n3\n20\n8\n10\n6\n20\n8\n'
dimension='0\nDIMENSION\n5\nEF\n2\n*D1\n70\n36\n10\n8\n20\n0\n11\n8\n21\n3\n15\n8\n25\n3\n'
source=('0\nSECTION\n2\nTABLES\n'+layers+style+'0\nENDSEC\n0\nSECTION\n2\nBLOCKS\n'+
        blocks+'0\nENDSEC\n0\nSECTION\n2\nENTITIES\n'+insert+leader+dimension+'0\nENDSEC\n0\nEOF\n')

def colors(obj):
    FreeCADGui.updateGui()
    return [tuple(c[:3]) for c in obj.ViewObject.LineColorArray]

with tempfile.TemporaryDirectory(dir=profile) as temp:
    path=Path(temp)/'annotations.dxf'
    path.write_text(source)
    doc=import_dxf(path)
    objects={o.DXFType:o for o in doc.Objects if 'DXFData' in o.PropertiesList}
    obj=objects['INSERT']
    assert colors(obj)==[(1.,0.,0.),(0.,1.,0.),(0.,1.,1.)], colors(obj)
    assert objects['LEADER'].ViewObject.DisplayMode=='Flat Lines'
    assert objects['DIMENSION'].ViewObject.DisplayMode=='Flat Lines'
    assert abs(objects['LEADER'].Shape.Area-2.5**2/6)<1e-9
    root=obj.DXFSourceRoot
    doc.UndoMode=1
    doc.openTransaction('Change child layer')
    DXFCAD.set_layer_properties(root,'Child',5,True)
    doc.recompute(); doc.commitTransaction()
    assert colors(obj)==[(1.,0.,0.),(0.,0.,1.),(0.,1.,1.)], colors(obj)
    doc.undo(); doc.recompute()
    assert colors(obj)[1]==(0.,1.,0.)
    doc.redo(); doc.recompute()
    doc.openTransaction('Hide child layer')
    DXFCAD.set_layer_properties(root,'Child',5,False)
    doc.recompute(); doc.commitTransaction()
    assert len(obj.Shape.Edges)==2
    assert colors(obj)==[(1.,0.,0.),(0.,1.,1.)], colors(obj)
    view=FreeCADGui.activeDocument().activeView()
    view.viewTop(); view.fitAll()
    saved=Path(temp)/'annotations.FCStd'
    doc.saveAs(str(saved))
    FreeCAD.closeDocument(doc.Name)

    path.unlink()
    doc=FreeCAD.openDocument(str(saved))
    obj=next(o for o in doc.Objects if getattr(o,'DXFType','')=='INSERT')
    obj.touch(); doc.recompute()
    assert colors(obj)==[(1.,0.,0.),(0.,1.,1.)], colors(obj)
    FreeCADGui.activeDocument().activeView().viewTop()
    FreeCADGui.activeDocument().activeView().fitAll()
    FreeCADGui.updateGui()
    FreeCADGui.activeDocument().activeView().saveImage(str(profile/'annotations.png'),1000,700,'White')
    FreeCAD.closeDocument(doc.Name)

# Optional local validation: no customer file or identifying fixture is stored
# in the repository. The default run above is completely synthetic.
real_source=os.environ.get('DXFCAD_VALIDATION_DRAWING')
if real_source:
    doc=import_dxf(real_source)
    root=next(o for o in doc.Objects if 'ImportWarnings' in o.PropertiesList)
    assert not root.ImportWarnings, list(root.ImportWarnings)
    entities=[o for o in doc.Objects if 'DXFData' in o.PropertiesList]
    assert not any(o.DisplayStatus.startswith('Error:') for o in entities)
    FreeCADGui.activeDocument().activeView().viewTop()
    FreeCADGui.activeDocument().activeView().fitAll()
    FreeCADGui.updateGui()
    FreeCADGui.activeDocument().activeView().saveImage(str(profile/'real-drawing.png'),1600,1100,'White')
    (profile/'real-drawing-result.json').write_text(json.dumps({'entities':len(entities),'warnings':list(root.ImportWarnings)}))
    FreeCAD.closeDocument(doc.Name)
