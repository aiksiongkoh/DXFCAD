"""Center/radius CIRCLE workflow using native C++ geometry and shared picking."""
import FreeCAD
import FreeCADGui
from PySide import QtWidgets
from pivy import coin
from dxfcad_line_ui import LineViewportTool, LineTaskPanel
from dxfcad_edit import create_circle, drawing_root, drawing_roots


class CircleViewportTool(LineViewportTool):
    idle_message = "Click a center. Escape while idle stops picking."
    pending_message = "Click a point on the circle; Escape/right-click cancels the preview."
    reset_message = "Preview canceled. Click a new center; completed circles are kept."

    def validate(self, start, end):
        import DXFCAD
        DXFCAD.circle_radius(start,end)

    def next_start(self, point):
        return None

    def render(self, end=None):
        # The inherited marker shows the center; C++ supplies the circle shape.
        super().render()
        if self.overlay is None or end is None:
            return
        import DXFCAD
        radius = DXFCAD.circle_radius(self.start_point,end)
        shape = DXFCAD.circle_preview_shape(self.start_point,radius)
        source = coin.SoInput()
        source.setBuffer(shape.writeInventor(1))
        geometry = coin.SoDB.readAll(source)
        color = coin.SoBaseColor()
        color.rgb = (1.0,0.45,0.05)
        color.setOverride(True)
        self.overlay.addChild(color)
        self.overlay.addChild(geometry)


class CircleTaskPanel(LineTaskPanel):
    def __init__(self, document, root=None, owns_document=False):
        self.document, self.root, self.owns_document = document, root, owns_document
        self.document_name = document.Name
        self.root_name = root.Name if root is not None else None
        self.form = QtWidgets.QWidget()
        self.form.setWindowTitle("Draw Circles")
        layout = QtWidgets.QVBoxLayout(self.form)
        note = QtWidgets.QLabel("Click a center, then a point on the circle, or enter a center and radius. "
                               "Circles lie in world XY at the chosen elevation, on layer 0. Coordinates use drawing units. "
                               "Each circle is undoable. Escape/right-click cancels the preview; Close stops the tool.")
        note.setWordWrap(True)
        layout.addWidget(note)
        fields = QtWidgets.QFormLayout()
        self.start_fields = [QtWidgets.QLineEdit("0"),QtWidgets.QLineEdit("0")]
        self.radius = QtWidgets.QLineEdit("10")
        self.elevation = QtWidgets.QLineEdit("0")
        for name,field in zip(("Center X","Center Y","Radius","Elevation Z"),[*self.start_fields,self.radius,self.elevation]):
            fields.addRow(name,field)
        layout.addLayout(fields)
        self.snap = QtWidgets.QCheckBox("Snap to DXFCAD objects")
        self.snap.setToolTip("Endpoints, vertices, centers, insertion points, midpoints, and intersections.")
        self.snap.setChecked(True)
        layout.addWidget(self.snap)
        self.pick = QtWidgets.QPushButton("Pick in view")
        self.pick.setCheckable(True)
        self.pick.clicked.connect(self.toggle_pick)
        layout.addWidget(self.pick)
        numeric = QtWidgets.QPushButton("Create circle from center and radius")
        numeric.clicked.connect(self.create_numeric)
        layout.addWidget(numeric)
        self.status = QtWidgets.QLabel()
        self.status.setWordWrap(True)
        self.error = QtWidgets.QLabel()
        self.error.setWordWrap(True)
        self.error.setStyleSheet("color: #bd2929")
        layout.addWidget(self.status)
        layout.addWidget(self.error)
        self.viewport_tool = CircleViewportTool(self)
        self.form.destroyed.connect(self.viewport_tool.close)

    def set_end(self, point):
        import DXFCAD
        self.radius.setText(format(DXFCAD.circle_radius(self.viewport_tool.start_point,point),".17g"))

    def create(self, center, radius):
        self.require_document()
        root = self.document.getObject(self.root_name) if self.root_name else None
        if self.root_name and root is None and drawing_roots(self.document):
            raise ValueError("The selected drawing was removed. Close and reopen the circle tool.")
        obj = create_circle(self.document,center,radius,root)
        self.root = drawing_root(obj)
        self.root_name = self.root.Name
        self.radius.setText(format(radius,".17g"))
        return obj

    def commit(self, center, circumference):
        import DXFCAD
        return self.create(center,DXFCAD.circle_radius(center,circumference))

    def create_numeric(self):
        import DXFCAD
        try:
            center = DXFCAD.Vector3D(*[float(f.text()) for f in self.start_fields],float(self.elevation.text()))
            self.create(center,float(self.radius.text()))
            self.viewport_tool.reset_chain()
            self.status.setText("Circle created. Click another center or use Undo to remove it.")
            self.error.clear()
        except Exception as error:
            self.error.setText(str(error))


def show_circle_tool(root=None):
    if FreeCADGui.Control.activeDialog():
        raise ValueError("Close the current task panel before drawing circles.")
    document = FreeCAD.ActiveDocument
    owns_document = document is None
    if root is None and document is not None:
        roots = drawing_roots(document)
        if len(roots) > 1:
            raise ValueError("Select one DXFCAD drawing before drawing circles.")
        root = roots[0] if roots else None
    if document is None:
        document = FreeCAD.newDocument("DXFDrawing")
    if root is not None and (root.Document != document or drawing_root(root) != root):
        raise ValueError("Select a DXFCAD drawing in the active document.")
    panel = CircleTaskPanel(document,root,owns_document)
    FreeCADGui.Control.showDialog(panel)
    panel.toggle_pick(True)
    return panel
