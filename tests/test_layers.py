"""C++ layer APIs: identity, native records, membership and transactional edits."""
import FreeCAD
import Part
from pathlib import Path
import sys
import tempfile
import unittest

module_directory=Path(sys.argv.pop(1)).resolve()
sys.path.insert(0,str(module_directory))
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import DXFCAD
assert Path(DXFCAD.__file__).parent==module_directory
from dxfcad_edit import create_line, drawing_root, transaction, apply_properties
from dxfcad_import import import_dxf


class LayerTests(unittest.TestCase):
    def setUp(self):
        self.doc=FreeCAD.newDocument("LayerTests")
        self.line=create_line(self.doc,DXFCAD.Vector3D(),DXFCAD.Vector3D(10,0,0))
        self.root=drawing_root(self.line)

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()): FreeCAD.closeDocument(name)

    def test_ensure_preserves_existing_properties_and_uses_persistent_identity(self):
        with transaction(self.doc,"Create layer"):
            group=DXFCAD.ensure_layer(self.root,"Cutting",3,False)
        snapshot=self.root.DXFSourceData
        group.Label="User display label"
        again=DXFCAD.ensure_layer(self.root,"Cutting",1,True)
        self.assertEqual(group,again)
        self.assertEqual(group.DXFLayerName,"Cutting")
        self.assertEqual(group.Label,"User display label")
        self.assertEqual(self.root.DXFSourceData,snapshot)
        layer=DXFCAD.drawing_document(self.root).layers["Cutting"]
        self.assertEqual((layer.color,layer.visible),(3,False))

    def test_assign_preserves_entity_properties_and_undo_restores_membership(self):
        original_group=next(g for g in self.root.Group if self.line in g.Group)
        self.line.DXFData=DXFCAD.entity_properties(self.line.DXFData,"0",2,False)
        self.doc.recompute()
        before=self.line.DXFData
        snapshot=self.root.DXFSourceData
        with transaction(self.doc,"Assign layer"):
            DXFCAD.assign_layer([self.line,self.line],"NewLayer")
        target=next(g for g in self.root.Group if g.DXFLayerName=="NewLayer")
        self.assertEqual(DXFCAD.property_info(self.line.DXFData),("NewLayer",2,False))
        self.assertIn(self.line,target.Group)
        self.assertNotIn(self.line,original_group.Group)
        self.doc.undo(); self.doc.recompute()
        self.assertEqual(self.root.DXFSourceData,snapshot)
        self.assertEqual(self.line.DXFData,before)
        self.assertIn(self.line,original_group.Group)
        self.doc.redo(); self.doc.recompute()
        self.assertEqual(self.line.DXFLayer,"NewLayer")
        self.assertIn(self.line,next(g for g in self.root.Group if g.DXFLayerName=="NewLayer").Group)

    def test_preview_and_invalid_batch_do_not_mutate_any_drawing(self):
        snapshot=self.root.DXFSourceData
        raw=self.line.DXFData
        names=[o.Name for o in self.doc.Objects]
        plan=DXFCAD.prepare_entity_properties([self.line],"Preview",1,True,4,False,True)
        self.assertIn("Preview",plan[2][self.root.Name].layers)
        self.assertEqual(self.root.DXFSourceData,snapshot)
        self.assertEqual(self.line.DXFData,raw)
        foreign=FreeCAD.newDocument("ForeignLayers")
        other=create_line(foreign,DXFCAD.Vector3D(),DXFCAD.Vector3D(1,0,0))
        with self.assertRaisesRegex(ValueError,"one document"):
            DXFCAD.assign_layer([self.line,other],"InvalidBatch")
        for name,color in (("Bad|Name",7),("",7),("NewLayer",0)):
            with self.assertRaises(ValueError): DXFCAD.set_layer_properties(self.root,name,color,True)
        self.assertEqual(self.root.DXFSourceData,snapshot)
        self.assertEqual(self.line.DXFData,raw)
        self.assertEqual([o.Name for o in self.doc.Objects],names)

    def test_legacy_identity_upgrade_and_ambiguity_rejection(self):
        group=next(g for g in self.root.Group if self.line in g.Group)
        group.removeProperty("DXFLayerName")
        group.Label="Renamed legacy group"
        self.assertEqual(DXFCAD.ensure_layer(self.root,"0"),group)
        self.assertEqual(group.DXFLayerName,"0")
        duplicate=self.doc.addObject("App::DocumentObjectGroup","Duplicate")
        duplicate.addProperty("App::PropertyString","DXFLayerName","DXF")
        duplicate.DXFLayerName="0"
        self.root.addObject(duplicate)
        snapshot=self.root.DXFSourceData
        with self.assertRaisesRegex(ValueError,"duplicate layer groups"):
            DXFCAD.set_layer_properties(self.root,"0",1,False)
        self.assertEqual(self.root.DXFSourceData,snapshot)

    def test_multiple_drawings_keep_layer_tables_and_groups_separate(self):
        other_root=DXFCAD.create_drawing(self.doc,DXFCAD.DXFDocument.empty(4))
        other=create_line(self.doc,DXFCAD.Vector3D(),DXFCAD.Vector3D(2,0,0),other_root)
        with transaction(self.doc,"Assign two drawings"):
            DXFCAD.assign_layer([self.line,other],"SharedName")
        left=next(g for g in self.root.Group if g.DXFLayerName=="SharedName")
        right=next(g for g in other_root.Group if g.DXFLayerName=="SharedName")
        self.assertNotEqual(left,right)
        with transaction(self.doc,"Edit one layer"):
            DXFCAD.set_layer_properties(self.root,"SharedName",5,False)
        self.assertEqual(DXFCAD.drawing_document(self.root).layers["SharedName"].color,5)
        self.assertEqual(DXFCAD.drawing_document(other_root).layers["SharedName"].color,7)

    def test_mixed_legacy_group_is_rejected_using_native_records(self):
        other=create_line(self.doc,DXFCAD.Vector3D(),DXFCAD.Vector3D(2,0,0),self.root)
        group=next(g for g in self.root.Group if self.line in g.Group)
        group.removeProperty("DXFLayerName")
        other.DXFData=DXFCAD.entity_properties(other.DXFData,"Other",-1,True)
        snapshot=self.root.DXFSourceData
        with self.assertRaisesRegex(ValueError,"mixed native layers"):
            DXFCAD.ensure_layer(self.root,"0")
        self.assertEqual(self.root.DXFSourceData,snapshot)
        self.assertNotIn("DXFLayerName",group.PropertiesList)

    def test_import_assign_export_preserves_opaque_layer_data(self):
        drawing=("0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n1\n"
                 "0\nLAYER\n2\nWalls\n62\n3\n420\n16711680\n6\nDASHED\n1001\nTEST\n1000\nkeep layer metadata\n"
                 "0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"
                 "0\nLINE\n8\nWalls\n10\n0\n20\n0\n11\n10\n21\n0\n0\nENDSEC\n0\nEOF\n")
        with tempfile.TemporaryDirectory(dir=module_directory) as temp:
            source=Path(temp)/"layers.dxf"; source.write_text(drawing)
            imported=import_dxf(source)
            line=next(o for o in imported.Objects if hasattr(o,"DXFData"))
            root=drawing_root(line)
            group=next(g for g in root.Group if g.DXFLayerName=="Walls")
            self.assertIn(line,group.Group)
            snapshot=root.DXFSourceData
            DXFCAD.ensure_layer(root,"Walls",1,False)
            self.assertEqual(root.DXFSourceData,snapshot)
            apply_properties([line],"Walls",-1,True,layer_color=2,layer_on=False,edit_layer=True)
            exported=DXFCAD.current_drawing_bytes(root)
            self.assertIn(b"keep layer metadata",exported)
            self.assertIn(b"DASHED",exported)
            self.assertNotIn(b"420\n16711680",exported)
            layer=DXFCAD.current_drawing(root).layers["Walls"]
            self.assertEqual((layer.color,layer.visible),(2,False))

    def test_import_undeclared_layer_preserves_source_snapshot(self):
        drawing=("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1009\n0\nENDSEC\n"
                 "0\nSECTION\n2\nENTITIES\n0\nLINE\n8\nUndeclared\n10\n0\n20\n0\n11\n1\n21\n0\n"
                 "0\nENDSEC\n0\nEOF\n")
        with tempfile.TemporaryDirectory(dir=module_directory) as temp:
            source=Path(temp)/"undeclared.dxf"; source.write_text(drawing)
            doc=import_dxf(source)
            line=next(o for o in doc.Objects if hasattr(o,"DXFData"))
            root=drawing_root(line)
            self.assertEqual(DXFCAD.drawing_document(root).serialize_bytes(),DXFCAD.DXFDocument.parse(drawing).serialize_bytes())
            self.assertIn(line,next(g for g in root.Group if g.DXFLayerName=="Undeclared").Group)
            with self.assertRaises(ValueError): DXFCAD.source_layer_group(root,"Missing")


if __name__=="__main__": unittest.main()
