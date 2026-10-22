"""Task panels for native drafting operations with explicit preview and apply."""
import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets
from pivy import coin
from dxfcad_move_ui import MoveTaskPanel
from dxfcad_edit import prepare_move, apply_records, apply_entity_update, apply_batch_plan, apply_properties, prepare_properties, drawing_root, apply_corner_plan


class RecordPreview:
    def __init__(self, panel):
        self.panel = panel
        self.scene = self.overlay = None
        self.observing = False
        self.timer = QtCore.QTimer(panel.form)
        self.timer.setInterval(150)
        self.timer.timeout.connect(self.check_view)

    def show(self, records):
        import DXFCAD
        self.close()
        self.panel.require_document()
        self.scene = FreeCADGui.activeDocument().activeView().getSceneGraph()
        self.scene.ref()
        self.overlay = coin.SoAnnotation()
        pick = coin.SoPickStyle()
        pick.style = coin.SoPickStyle.UNPICKABLE
        # Part.writeInventor embeds SoMaterial nodes. Override the same material
        # element so those black strokes cannot hide the preview color.
        color = coin.SoMaterial()
        color.diffuseColor = (1.0,0.45,0.05)
        color.emissiveColor = (1.0,0.45,0.05)
        color.setOverride(True)
        lighting = coin.SoLightModel()
        lighting.model = coin.SoLightModel.BASE_COLOR
        lighting.setOverride(True)
        self.overlay.addChild(pick)
        self.overlay.addChild(color)
        self.overlay.addChild(lighting)
        try:
            for obj,raw in records:
                shape = DXFCAD.entity_preview_shape(raw,getattr(obj,"TextFont",""),getattr(obj,"ShowWidths",False),getattr(obj,"DXFContext",""))
                source = coin.SoInput()
                inventor = shape.writeInventor(1)
                source.setBuffer(inventor)
                geometry = coin.SoDB.readAll(source)
                if geometry is None:
                    raise ValueError("Cannot display drafting preview.")
                self.overlay.addChild(geometry)
            self.scene.addChild(self.overlay)
            FreeCAD.addDocumentObserver(self)
            self.observing = True
            self.timer.start()
        except Exception:
            self.close()
            raise

    def check_view(self):
        try:
            gui = FreeCADGui.activeDocument()
            if gui is None or gui.Document != self.panel.document or int(gui.activeView().getSceneGraph().this) != int(self.scene.this):
                self.close()
        except RuntimeError:
            self.close()

    def slotDeletedDocument(self, document):
        if document == self.panel.document:
            self.close()

    def close(self, *args):
        try:
            self.timer.stop()
        except RuntimeError:
            pass
        if self.observing:
            FreeCAD.removeDocumentObserver(self)
            self.observing = False
        if self.scene is not None:
            if self.overlay is not None and self.scene.findChild(self.overlay)>=0:
                self.scene.removeChild(self.overlay)
            self.scene.unref()
        self.scene = self.overlay = None


class SelectionTaskPanel(MoveTaskPanel):
    def __init__(self, objects, title, note):
        import DXFCAD
        self.objects = [obj for obj,raw in prepare_move(objects,DXFCAD.Vector3D())]
        self.document = self.objects[0].Document
        self.document_name = self.document.Name
        self.owns_document = False
        self.capture()
        self.form = QtWidgets.QWidget()
        self.form.setWindowTitle(title)
        layout = QtWidgets.QVBoxLayout(self.form)
        label = QtWidgets.QLabel(note+" Coordinates use drawing units. Preview changes no objects; Apply is one undo step.")
        label.setWordWrap(True)
        layout.addWidget(label)
        self.fields = QtWidgets.QFormLayout()
        layout.addLayout(self.fields)
        for text,slot in (("Preview",self.preview),("Apply",self.apply)):
            button = QtWidgets.QPushButton(text)
            button.clicked.connect(slot)
            layout.addWidget(button)
        self.status, self.error = QtWidgets.QLabel(),QtWidgets.QLabel()
        for label in (self.status,self.error):
            label.setWordWrap(True)
            layout.addWidget(label)
        self.error.setStyleSheet("color: #bd2929")
        self.viewport_tool = RecordPreview(self)
        self.form.destroyed.connect(self.viewport_tool.close)

    def preview(self):
        try:
            self.viewport_tool.show(self.records())
            self.error.clear()
            self.status.setText("Orange preview. Apply to confirm, or Close to cancel.")
        except Exception as error:
            self.viewport_tool.close()
            self.error.setText(str(error))

    def apply(self):
        try:
            self.commit_records(self.records())
            self.capture()
            self.viewport_tool.close()
            self.error.clear()
            self.status.setText("Applied. Use Undo to reverse this operation.")
        except Exception as error:
            self.viewport_tool.close()
            self.error.setText(str(error))

    def commit_records(self, records):
        apply_records(records,self.form.windowTitle())


class TrimTaskPanel(SelectionTaskPanel):
    def capture(self):
        super().capture()
        self.trim_plan = None
        self.trim_parameters = None

    def __init__(self, objects, extend=False):
        if len(objects)!=2 or any(getattr(obj,"DXFType","") not in ("LINE","ARC","CIRCLE","LWPOLYLINE") for obj in objects):
            raise ValueError("Select two native LINE, ARC, CIRCLE or LWPolyline objects: target and boundary.")
        self.extend = extend
        super().__init__(objects,"Extend Objects" if extend else "Trim Objects",
                         "Choose a LINE, ARC or open polyline target and endpoint. The other selection is a finite boundary. Both must share a plane. The nearest intersection is used; trimming retains the opposite endpoint.")
        self.target = QtWidgets.QComboBox()
        self.target.addItems([obj.Label+" ("+obj.Name+")" for obj in self.objects])
        self.endpoint = QtWidgets.QComboBox()
        self.endpoint.addItems(["Start","End"])
        self.endpoint.setCurrentIndex(1)
        self.fields.addRow("Target object",self.target)
        self.fields.addRow("Endpoint to change",self.endpoint)

    def records(self):
        import DXFCAD
        self.require_snapshot()
        index = self.target.currentIndex()
        parameters = (index, self.endpoint.currentIndex()==0, self.extend)
        if self.trim_plan is None or parameters != self.trim_parameters:
            self.trim_plan = DXFCAD.prepare_trim(self.objects[index], self.objects[1-index],
                                                 parameters[1], parameters[2])
            self.trim_parameters = parameters
        return self.trim_plan.preview_records

    def commit_records(self, records):
        apply_entity_update(self.trim_plan, self.form.windowTitle())


class ExtendTaskPanel(TrimTaskPanel):
    def __init__(self, objects):
        super().__init__(objects,True)


class OffsetTaskPanel(SelectionTaskPanel):
    def capture(self):
        super().capture()
        self.offset_plan = None
        self.offset_parameters = None

    def __init__(self, objects):
        super().__init__(objects,"Offset Objects","Positive distance offsets LINE to its left in world XY and polylines to their left in OCS: counterclockwise arcs shrink and clockwise arcs grow. For standalone circles/arcs it enlarges the radius.")
        self.distance = QtWidgets.QLineEdit("1")
        self.keep = QtWidgets.QCheckBox("Keep originals (create new entities)")
        self.keep.setChecked(True)
        self.fields.addRow("Signed distance",self.distance)
        self.fields.addRow(self.keep)

    def records(self):
        import DXFCAD
        self.require_snapshot()
        parameters = (float(self.distance.text()), self.keep.isChecked())
        if self.offset_plan is None or parameters != self.offset_parameters:
            self.offset_plan = DXFCAD.prepare_offset(self.objects, *parameters)
            self.offset_parameters = parameters
        return self.offset_plan.preview_records

    def commit_records(self, records):
        apply_batch_plan(self.offset_plan, "Offset DXF objects")


class LineCornerTaskPanel(SelectionTaskPanel):
    def __init__(self, objects):
        if len(objects)!=2 or objects[0]==objects[1] or any(getattr(obj,"DXFType","")!="LINE" for obj in objects):
            raise ValueError("Select two distinct native LINE objects.")
        root=drawing_root(objects[0])
        if root is None or drawing_root(objects[1])!=root:
            raise ValueError("Corner lines must belong to the same DXFCAD drawing.")
        self.created_connector=None
        super().__init__(objects,self.title,self.note)
        self.amount=QtWidgets.QLineEdit("1")
        self.keep_first,self.keep_second=QtWidgets.QComboBox(),QtWidgets.QComboBox()
        for combo in (self.keep_first,self.keep_second):
            combo.addItems(["Start","End"])
        self.keep_second.setCurrentIndex(1)
        self.fields.addRow(self.value_label,self.amount)
        self.fields.addRow("Keep on "+self.objects[0].Label,self.keep_first)
        self.fields.addRow("Keep on "+self.objects[1].Label,self.keep_second)

    def commit_records(self, records):
        self.created_connector=apply_corner_plan(self.corner_plan,self.transaction_label)

    def apply(self):
        super().apply()
        if self.created_connector is not None:
            self.reject()


class LineFilletTaskPanel(LineCornerTaskPanel):
    title="Fillet Lines"
    note="Keep one endpoint on each line. The other ends are trimmed or extended to a tangent arc. Lines must share a plane. The arc uses the first line's layer and display properties. Apply finishes this fillet."
    value_label="Radius"
    connector_type="ARC"
    transaction_label="Fillet DXF lines"

    @property
    def created_arc(self):
        return self.created_connector

    def records(self):
        import DXFCAD
        self.require_snapshot()
        self.corner_plan=DXFCAD.prepare_line_fillet(self.objects,float(self.amount.text()),
                                   self.keep_first.currentIndex()==0,self.keep_second.currentIndex()==0)
        return self.corner_plan.preview_records

class LineChamferTaskPanel(LineCornerTaskPanel):
    title="Chamfer Lines"
    note="Keep one endpoint on each line. Distances run from the extended intersection toward the kept endpoints. The other ends are trimmed or extended and joined by a new line. Lines must share a plane. The connector uses the first line's layer and display properties. Apply finishes this chamfer."
    value_label="Distance on first line"
    connector_type="LINE"
    transaction_label="Chamfer DXF lines"

    def __init__(self, objects):
        super().__init__(objects)
        self.second_amount=QtWidgets.QLineEdit("1")
        self.fields.insertRow(1,"Distance on second line",self.second_amount)

    def records(self):
        import DXFCAD
        self.require_snapshot()
        self.corner_plan=DXFCAD.prepare_line_chamfer(self.objects,float(self.amount.text()),float(self.second_amount.text()),
                                    self.keep_first.currentIndex()==0,self.keep_second.currentIndex()==0)
        return self.corner_plan.preview_records


class FilletTaskPanel(SelectionTaskPanel):
    def capture(self):
        super().capture()
        self.corner_plan = self.corner_parameters = None

    title = "Fillet Polyline Corner"
    value_label = "Radius"

    def __init__(self, objects):
        import DXFCAD
        if len(objects)!=1 or getattr(objects[0],"DXFType","")!="LWPOLYLINE":
            raise ValueError("Select one native LWPolyline.")
        super().__init__(objects,self.title,"Choose a vertex index (starting at 0) between two straight segments. Open-polyline endpoints cannot be edited.")
        self.vertex = QtWidgets.QSpinBox()
        self.vertex.setRange(0,len(DXFCAD.read_lwpolyline(self.objects[0].DXFData).vertices)-1)
        self.vertex.setValue(1)
        self.amount = QtWidgets.QLineEdit("1")
        self.fields.addRow("Corner vertex index",self.vertex)
        self.fields.addRow(self.value_label,self.amount)

    def commit_records(self, records):
        import DXFCAD
        apply_entity_update(self.corner_plan, self.form.windowTitle())
        self.vertex.setMaximum(len(DXFCAD.read_lwpolyline(self.objects[0].DXFData).vertices)-1)

    def records(self):
        import DXFCAD
        self.require_snapshot()
        parameters = (self.vertex.value(), float(self.amount.text()))
        if self.corner_plan is None or parameters != self.corner_parameters:
            self.corner_plan = DXFCAD.prepare_polyline_fillet(self.objects[0], *parameters)
            self.corner_parameters = parameters
        return self.corner_plan.preview_records


class ChamferTaskPanel(FilletTaskPanel):
    title = "Chamfer Polyline Corner"
    value_label = "Incoming distance"

    def __init__(self, objects):
        super().__init__(objects)
        self.outgoing = QtWidgets.QLineEdit("1")
        self.fields.addRow("Outgoing distance",self.outgoing)

    def records(self):
        import DXFCAD
        self.require_snapshot()
        parameters = (self.vertex.value(), float(self.amount.text()), float(self.outgoing.text()))
        if self.corner_plan is None or parameters != self.corner_parameters:
            self.corner_plan = DXFCAD.prepare_polyline_chamfer(self.objects[0], *parameters)
            self.corner_parameters = parameters
        return self.corner_plan.preview_records


class PropertiesTaskPanel(SelectionTaskPanel):
    def __init__(self, objects):
        import DXFCAD
        super().__init__(objects,"DXF Properties","Apply entered layer and visibility values to the whole selection. New layers are created when missing. Enable Set ACI color to replace an entity color override.")
        self.sources = {}
        for obj in self.objects:
            root = drawing_root(obj)
            if root is None:
                raise ValueError("Select objects belonging to a DXFCAD drawing.")
            if root.Name not in self.sources:
                self.sources[root.Name] = DXFCAD.drawing_document(root)
        layer,color,visible = DXFCAD.property_info(self.objects[0].DXFData)
        self.layer = QtWidgets.QComboBox()
        self.layer.setEditable(True)
        self.layer.addItems(sorted({name for source in self.sources.values() for name in source.layers}))
        self.layer.setCurrentText(layer)
        self.set_color = QtWidgets.QCheckBox("Set ACI color (replaces true-color override)")
        self.color = QtWidgets.QSpinBox()
        self.color.setRange(0,256)
        self.color.setValue(color)
        self.color.setEnabled(False)
        self.set_color.toggled.connect(self.color.setEnabled)
        self.visible = QtWidgets.QCheckBox("Entity visible")
        self.visible.setChecked(visible)
        self.edit_layer = QtWidgets.QCheckBox("Set existing layer's ACI color and on/off state")
        self.layer_color = QtWidgets.QSpinBox()
        self.layer_color.setRange(1,255)
        self.layer_on = QtWidgets.QCheckBox("Layer on (unfreezes it)")
        self.fields.addRow("Layer",self.layer)
        self.fields.addRow(self.set_color)
        self.fields.addRow("ACI: 0=BYBLOCK, 256=BYLAYER",self.color)
        self.fields.addRow(self.visible)
        self.fields.addRow(self.edit_layer)
        self.fields.addRow("Layer ACI color",self.layer_color)
        self.fields.addRow(self.layer_on)
        self.layer.currentTextChanged.connect(self.layer_defaults)
        self.layer_defaults(layer)

    def layer_defaults(self, name):
        info = next((source.layers[name] for source in self.sources.values() if name in source.layers),None)
        self.layer_color.setValue(info.color if info is not None else 7)
        self.layer_on.setChecked(info.visible if info is not None else True)

    def arguments(self):
        return (self.layer.currentText(),self.color.value() if self.set_color.isChecked() else -1,self.visible.isChecked(),self.layer_color.value(),self.layer_on.isChecked(),self.edit_layer.isChecked())

    def records(self):
        self.require_snapshot()
        document,roots,sources,records = prepare_properties(self.objects,*self.arguments())
        return [(obj,raw) for obj,root,raw in records]

    def commit_records(self, records):
        import DXFCAD
        apply_properties(self.objects,*self.arguments())
        for obj in self.objects:
            root = drawing_root(obj)
            self.sources[root.Name] = DXFCAD.drawing_document(root)


def show_drafting_tool(panel_type, objects=None):
    if FreeCADGui.Control.activeDialog():
        raise ValueError("Close the current task panel first.")
    objects = FreeCADGui.Selection.getSelection() if objects is None else list(objects)
    if not objects or any(obj.Document != FreeCAD.ActiveDocument for obj in objects):
        raise ValueError("Select native DXFCAD objects in the active document.")
    panel = panel_type(objects)
    FreeCADGui.Control.showDialog(panel)
    return panel
