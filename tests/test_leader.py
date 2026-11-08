"""Synthetic LEADER import, arrow geometry, native records and cold restore."""
import FreeCAD
import Part
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest

module_directory=Path(sys.argv.pop(1)).resolve()
sys.path.insert(0,str(module_directory))
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import DXFCAD
from dxfcad_import import import_dxf
from dxfcad_export import export_dxf

RAW="0\nLEADER\n5\nAB\n3\nSynthetic\n76\n3\n10\n0\n20\n0\n10\n3\n20\n4\n10\n6\n20\n4\n"
STYLE="0\nDIMSTYLE\n2\nSynthetic\n40\n2\n41\n2.5\n"
SOURCE="0\nSECTION\n2\nTABLES\n"+STYLE+"0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"+RAW+"0\nENDSEC\n0\nEOF\n"

class LeaderTests(unittest.TestCase):
    def test_import_restore_and_export(self):
        with tempfile.TemporaryDirectory() as temp:
            source=Path(temp)/'leader.dxf'
            source.write_text(SOURCE)
            doc=import_dxf(source)
            obj=next(o for o in doc.Objects if 'DXFData' in o.PropertiesList)
            self.assertEqual(obj.DXFData,RAW)
            self.assertEqual(obj.DXFContext,STYLE)
            self.assertTrue(obj.Shape.isValid())
            self.assertAlmostEqual(obj.Shape.Area,25/6,places=9)
            if DXFCAD.NATIVE_DOCUMENT_OBJECTS:
                self.assertEqual(obj.TypeId,'DXFCAD::LeaderObject')
                self.assertTrue(obj.isDerivedFrom('DXFCAD::EntityObject'))
            saved=Path(temp)/'leader.FCStd'
            doc.saveAs(str(saved))
            FreeCAD.closeDocument(doc.Name)
            source.unlink()
            output=Path(temp)/'exported.dxf'
            script="""import FreeCAD, Part
import sys
sys.path.insert(0,sys.argv[1]); sys.path.insert(0,sys.argv[2])
import DXFCAD, dxfcad_objects
from dxfcad_export import export_dxf
doc=FreeCAD.openDocument(sys.argv[3])
obj=next(o for o in doc.Objects if 'DXFData' in o.PropertiesList)
obj.Shape=Part.Shape(); obj.touch(); doc.recompute()
assert obj.Shape.isValid() and not obj.Shape.isNull(), obj.DisplayStatus
assert abs(obj.Shape.Area-25/6)<1e-9
export_dxf(sys.argv[4],doc)
FreeCAD.closeDocument(doc.Name)
"""
            completed=subprocess.run([sys.executable,'-c',script,str(module_directory),
                str(Path(__file__).resolve().parents[1]),str(saved),str(output)],capture_output=True,text=True,timeout=60)
            self.assertEqual(completed.returncode,0,completed.stdout+completed.stderr)
            self.assertEqual(output.read_bytes(),DXFCAD.Drawing.parse(SOURCE).serialize_bytes())

if __name__=='__main__': unittest.main()
