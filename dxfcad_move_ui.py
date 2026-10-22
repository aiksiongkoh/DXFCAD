"""Move selected native objects with a snapped base/destination or numeric delta."""
import FreeCAD
import FreeCADGui
from PySide import QtWidgets
from pivy import coin
from dxfcad_line_ui import LineViewportTool, LineTaskPanel
from dxfcad_edit import prepare_move, apply_batch_plan


class MoveViewportTool(LineViewportTool):
    idle_message = "Click a base point. Escape while idle stops picking."
    pending_message = "Click the destination; Escape/right-click cancels the preview."
    reset_message = "Preview canceled. Selected objects have not moved."

    def next_start(self, point):
        return None

    def records(self, start, end):
        import DXFCAD
        return self.panel.records(DXFCAD.displacement(start,end))

    def validate(self, start, end):
        self.records(start,end)

    def render(self, end=None):
        super().render(end)
        if self.overlay is None or end is None:
            return
        import DXFCAD
        records = self.records(self.start_point,end)
        color = coin.SoBaseColor()
        color.rgb = (1.0,0.45,0.05)
        color.setOverride(True)
        lighting = coin.SoLightModel()
        lighting.model = coin.SoLightModel.BASE_COLOR
        lighting.setOverride(True)
        self.overlay.addChild(color)
        self.overlay.addChild(lighting)
        for obj,raw in records:
            shape = DXFCAD.entity_preview_shape(raw,getattr(obj,"TextFont",""),getattr(obj,"ShowWidths",False),getattr(obj,"DXFContext",""))
            source = coin.SoInput()
            source.setBuffer(shape.writeInventor(1))
            geometry = coin.SoDB.readAll(source)
            if geometry is None:
                raise ValueError("Cannot display Move preview.")
            self.overlay.addChild(geometry)


class MoveTaskPanel(LineTaskPanel):
    operation = "Move"

    def __init__(self, objects):
        import DXFCAD
        prepared = prepare_move(objects,DXFCAD.Vector3D())
        self.objects = [obj for obj,raw in prepared]
        self.document = self.objects[0].Document
        self.document_name = self.document.Name
        self.owns_document = False
        self.capture()
        self.form = QtWidgets.QWidget()
        self.form.setWindowTitle(self.operation+" DXF Objects")
        layout = QtWidgets.QVBoxLayout(self.form)
        note = QtWidgets.QLabel(f"{self.operation} {len(self.objects)} selected object(s). Pick a base point, then a destination in world XY, "
                               "or enter a 3D displacement. Each move is one undoable transaction. "
                               "Coordinates use drawing units. Escape/right-click cancels the preview; Close exits.")
        note.setWordWrap(True)
        layout.addWidget(note)
        fields = QtWidgets.QFormLayout()
        self.delta_fields = [QtWidgets.QLineEdit("0") for _ in range(3)]
        self.elevation = QtWidgets.QLineEdit("0")
        for name,field in zip(("Delta X","Delta Y","Delta Z","Pick plane Z"),[*self.delta_fields,self.elevation]):
            fields.addRow(name,field)
        layout.addLayout(fields)
        self.base = QtWidgets.QLabel("Base point: not picked")
        self.base.setWordWrap(True)
        layout.addWidget(self.base)
        self.snap = QtWidgets.QCheckBox("Snap to DXFCAD objects")
        self.snap.setToolTip("Endpoints, vertices, centers, insertion points, midpoints, and intersections.")
        self.snap.setChecked(True)
        layout.addWidget(self.snap)
        self.pick = QtWidgets.QPushButton("Pick in view")
        self.pick.setCheckable(True)
        self.pick.clicked.connect(self.toggle_pick)
        layout.addWidget(self.pick)
        numeric = QtWidgets.QPushButton(self.operation+" by displacement")
        numeric.clicked.connect(self.create_numeric)
        layout.addWidget(numeric)
        self.status = QtWidgets.QLabel()
        self.status.setWordWrap(True)
        self.error = QtWidgets.QLabel()
        self.error.setWordWrap(True)
        self.error.setStyleSheet("color: #bd2929")
        layout.addWidget(self.status)
        layout.addWidget(self.error)
        self.viewport_tool = MoveViewportTool(self)
        self.form.destroyed.connect(self.viewport_tool.close)

    def capture(self):
        self.snapshot = [(obj.Name,obj,obj.DXFData,getattr(obj,"DXFContext","")) for obj in self.objects]

    def require_snapshot(self):
        self.require_document()
        for name,obj,raw,context in self.snapshot:
            current = self.document.getObject(name)
            if current != obj or current.DXFData != raw or getattr(current,"DXFContext","") != context:
                raise ValueError("Selected objects changed. Close and reopen this tool.")

    def records(self, offset):
        import DXFCAD
        self.require_snapshot()
        self.batch_plan = DXFCAD.prepare_batch_transform(
            self.objects, self.operation.lower(), offset=offset, allow_identity=True)
        return self.batch_plan.preview_records

    def set_start(self, point):
        self.base.setText("Base point: "+", ".join(format(n,".10g") for n in (point.x,point.y,point.z)))

    def set_end(self, point):
        import DXFCAD
        delta = DXFCAD.displacement(self.viewport_tool.start_point,point)
        for field,value in zip(self.delta_fields,(delta.x,delta.y,delta.z)):
            field.setText(format(value,".17g"))

    def apply(self, offset):
        import DXFCAD
        self.require_snapshot()
        self.batch_plan = DXFCAD.prepare_batch_transform(self.objects, "move", offset=offset)
        apply_batch_plan(self.batch_plan, "Move DXF objects")
        self.capture()

    def commit(self, start, end):
        import DXFCAD
        self.apply(DXFCAD.displacement(start,end))

    def create_numeric(self):
        import DXFCAD
        try:
            self.apply(DXFCAD.Vector3D(*[float(field.text()) for field in self.delta_fields]))
            self.viewport_tool.reset_chain()
            self.status.setText(self.operation+" completed. Use Undo to reverse this operation.")
            self.error.clear()
        except Exception as error:
            self.error.setText(str(error))


class CopyTaskPanel(MoveTaskPanel):
    operation = "Copy"

    def apply(self, offset):
        self.records(offset)
        apply_batch_plan(self.batch_plan, "Copy DXF objects")


def show_copy_tool(objects=None):
    return show_move_tool(objects,CopyTaskPanel)


def show_move_tool(objects=None,panel_type=MoveTaskPanel):
    if FreeCADGui.Control.activeDialog():
        raise ValueError("Close the current task panel before moving objects.")
    objects = FreeCADGui.Selection.getSelection() if objects is None else list(objects)
    if not objects or any(obj.Document != FreeCAD.ActiveDocument for obj in objects):
        raise ValueError("Select DXFCAD objects in the active document.")
    panel = panel_type(objects)
    FreeCADGui.Control.showDialog(panel)
    panel.toggle_pick(True)
    return panel
