"""Native selection transforms, with numeric controls and snapped reference picking."""
import FreeCAD
import FreeCADGui
from PySide import QtWidgets
from dxfcad_move_ui import MoveTaskPanel, MoveViewportTool
from dxfcad_edit import prepare_move, apply_batch_plan


class RotateViewportTool(MoveViewportTool):
    idle_message = "Click a rotation center. Escape while idle stops picking."
    pending_message = "Click a direction for the angle, measured from world +X."
    reset_message = "Preview canceled. Objects are unchanged."

    def records(self, start, end):
        return self.panel.records(self.panel.parameter_for_points(start,end))


class RotateTaskPanel(MoveTaskPanel):
    operation = "Rotate"
    domain_method = "rotated"
    field_label = "Angle (degrees)"
    initial_value = "90"
    note = "Pick a center and an angle direction measured from world +X, or enter a center and angle. Rotation is about world Z."
    viewport_class = RotateViewportTool

    def __init__(self, objects):
        import DXFCAD
        self.objects = [obj for obj,raw in prepare_move(objects,DXFCAD.Vector3D())]
        self.document = self.objects[0].Document
        self.document_name = self.document.Name
        self.owns_document = False
        self.capture()
        self.form = QtWidgets.QWidget()
        self.form.setWindowTitle(self.operation+" DXF Objects")
        layout = QtWidgets.QVBoxLayout(self.form)
        note = QtWidgets.QLabel(self.note+" Each operation is undoable for the whole selection. Escape/right-click cancels the preview; Close exits.")
        note.setWordWrap(True)
        layout.addWidget(note)
        fields = QtWidgets.QFormLayout()
        self.pivot_fields = [QtWidgets.QLineEdit("0") for _ in range(3)]
        self.elevation = self.pivot_fields[2]
        self.parameter = QtWidgets.QLineEdit(self.initial_value)
        for name,field in zip(("Center X","Center Y","Center Z"),self.pivot_fields):
            fields.addRow(name,field)
        fields.addRow(self.field_label,self.parameter)
        layout.addLayout(fields)
        self.snap = QtWidgets.QCheckBox("Snap to DXFCAD objects")
        self.snap.setToolTip("Endpoints, vertices, centers, insertion points, midpoints, and intersections.")
        self.snap.setChecked(True)
        layout.addWidget(self.snap)
        self.pick = QtWidgets.QPushButton("Pick in view")
        self.pick.setCheckable(True)
        self.pick.clicked.connect(self.toggle_pick)
        layout.addWidget(self.pick)
        numeric = QtWidgets.QPushButton(self.operation+" selected objects")
        numeric.clicked.connect(self.create_numeric)
        layout.addWidget(numeric)
        self.status, self.error = QtWidgets.QLabel(),QtWidgets.QLabel()
        for label in (self.status,self.error):
            label.setWordWrap(True)
            layout.addWidget(label)
        self.error.setStyleSheet("color: #bd2929")
        self.viewport_tool = self.viewport_class(self)
        self.form.destroyed.connect(self.viewport_tool.close)

    def pivot(self):
        import DXFCAD
        return DXFCAD.Vector3D(*[float(field.text()) for field in self.pivot_fields])

    def parameter_for_points(self, start, end):
        import DXFCAD
        return DXFCAD.rotation_angle(start,end)

    def records(self, parameter):
        import DXFCAD
        self.require_snapshot()
        self.batch_plan = DXFCAD.prepare_batch_transform(
            self.objects, self.domain_method, value=parameter, pivot=self.pivot())
        return self.batch_plan.preview_records

    def set_start(self, point):
        for field,value in zip(self.pivot_fields,(point.x,point.y,point.z)):
            field.setText(format(value,".17g"))

    def set_end(self, point):
        value = self.parameter_for_points(self.viewport_tool.start_point,point)
        self.parameter.setText(format(value,".17g"))

    def apply(self, parameter):
        self.records(parameter)
        apply_batch_plan(self.batch_plan,self.operation+" DXF objects")
        self.capture()

    def commit(self, start, end):
        self.set_end(end)
        self.apply(self.parameter_for_points(start,end))

    def create_numeric(self):
        try:
            self.apply(float(self.parameter.text()))
            self.viewport_tool.reset_chain()
            self.status.setText(self.operation+" completed. Use Undo to reverse it.")
            self.error.clear()
        except Exception as error:
            self.error.setText(str(error))


class ScaleViewportTool(RotateViewportTool):
    idle_message = "Click a scale center. Escape while idle stops picking."
    pending_message = "Click a point at the desired scaled reference length."


class ScaleTaskPanel(RotateTaskPanel):
    operation = "Scale"
    domain_method = "scaled"
    field_label = "Scale factor"
    initial_value = "2"
    note = "Enter a positive scale factor and center, or pick a center and a point. The picked factor is distance divided by Reference length."
    viewport_class = ScaleViewportTool

    def __init__(self, objects):
        super().__init__(objects)
        self.reference_length = QtWidgets.QLineEdit("1")
        fields = QtWidgets.QFormLayout()
        fields.addRow("Reference length",self.reference_length)
        self.form.layout().insertLayout(2,fields)

    def parameter_for_points(self, start, end):
        import DXFCAD
        return DXFCAD.scale_factor(start,end,float(self.reference_length.text()))


class MirrorViewportTool(RotateViewportTool):
    idle_message = "Click a point on the mirror line."
    pending_message = "Click a second point defining the mirror line."


class MirrorTaskPanel(RotateTaskPanel):
    operation = "Mirror"
    domain_method = "mirrored"
    field_label = "Mirror line angle (degrees)"
    initial_value = "0"
    note = "Pick two points defining the mirror line, or enter a point and line angle from world +X. Geometry and text are reflected across the vertical plane through this line."
    viewport_class = MirrorViewportTool


def show_transform_tool(objects=None,panel_type=RotateTaskPanel):
    if FreeCADGui.Control.activeDialog():
        raise ValueError("Close the current task panel first.")
    objects = FreeCADGui.Selection.getSelection() if objects is None else list(objects)
    if not objects or any(obj.Document != FreeCAD.ActiveDocument for obj in objects):
        raise ValueError("Select DXFCAD objects in the active document.")
    panel = panel_type(objects)
    FreeCADGui.Control.showDialog(panel)
    panel.toggle_pick(True)
    return panel
