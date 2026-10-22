"""LWPolyline task panel. All edits and geometry are delegated to C++."""

import math

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

from dxfcad_edit import apply_entity_update, create_polyline


class PolylineTaskPanel:
    def __init__(self, obj=None, root=None):
        import DXFCAD

        self.obj = obj
        self.document = obj.Document if obj else FreeCAD.ActiveDocument
        self.root = root
        self.edit_session = DXFCAD.begin_polyline_edit(obj) if obj else None
        self.form = QtWidgets.QWidget()
        self.form.setObjectName("DXFCADPolylineEditor")
        self.form.setWindowTitle("Edit LWPolyline" if obj else "Create LWPolyline")
        layout = QtWidgets.QVBoxLayout(self.form)
        info = QtWidgets.QLabel("Coordinates use the polyline's OCS and source drawing units. New drawings use millimeters. Bulge and widths apply to the outgoing segment.")
        info.setWordWrap(True)
        layout.addWidget(info)
        self.table = QtWidgets.QTableWidget(0, 6)
        self.table.setHorizontalHeaderLabels(["X", "Y", "Bulge", "Start width", "End width", "Use widths"])
        self.table.horizontalHeader().setSectionResizeMode(QtWidgets.QHeaderView.Stretch)
        self.table.horizontalHeaderItem(5).setToolTip("Keep explicit variable widths, including zero widths, instead of using constant width.")
        self.table.setSelectionBehavior(QtWidgets.QAbstractItemView.SelectRows)
        layout.addWidget(self.table)
        buttons = QtWidgets.QHBoxLayout()
        for text, action in [("Insert vertex", self.insert_vertex), ("Remove selected", self.remove_vertices)]:
            button = QtWidgets.QPushButton(text)
            button.clicked.connect(action)
            buttons.addWidget(button)
        layout.addLayout(buttons)
        self.viewport_editor = None
        self.move_in_view = QtWidgets.QPushButton("Move vertices in view")
        self.move_in_view.setCheckable(True)
        self.move_in_view.clicked.connect(self._toggle_view)
        layout.addWidget(self.move_in_view)
        self.snap_vertices = QtWidgets.QCheckBox("Snap to this polyline's vertices")
        self.snap_vertices.setChecked(True)
        layout.addWidget(self.snap_vertices)
        self.snap_objects = QtWidgets.QCheckBox("Snap to other DXFCAD objects")
        self.snap_objects.setToolTip("Endpoints, vertices, centers, insertion points, midpoints, and intersections.")
        self.snap_objects.setChecked(True)
        layout.addWidget(self.snap_objects)
        self.view_status = QtWidgets.QLabel()
        self.view_status.setWordWrap(True)
        layout.addWidget(self.view_status)
        properties = QtWidgets.QFormLayout()
        self.closed = QtWidgets.QCheckBox()
        properties.addRow("Closed", self.closed)
        self.width = QtWidgets.QLineEdit("0")
        properties.addRow("Constant width", self.width)
        self.elevation = QtWidgets.QLineEdit("0")
        properties.addRow("Elevation", self.elevation)
        self.extrusion = [QtWidgets.QLineEdit(value) for value in ("0", "0", "1")]
        for label, field in zip(("Extrusion X", "Extrusion Y", "Extrusion Z"), self.extrusion):
            properties.addRow(label, field)
        layout.addLayout(properties)
        transform = QtWidgets.QGroupBox("Transform vertices")
        transform_layout = QtWidgets.QFormLayout(transform)
        self.operation = QtWidgets.QComboBox()
        self.operation.addItems(["Move (world coordinates)", "Rotate (OCS plane)", "Scale (OCS plane)", "Mirror (OCS line)"])
        transform_layout.addRow(self.operation)
        self.parameter_labels = [QtWidgets.QLabel() for _ in range(4)]
        self.parameters = [QtWidgets.QLineEdit("0") for _ in range(4)]
        for label, field in zip(self.parameter_labels, self.parameters):
            transform_layout.addRow(label, field)
        button = QtWidgets.QPushButton("Apply transform to table")
        button.clicked.connect(self.transform)
        transform_layout.addRow(button)
        self.operation.currentIndexChanged.connect(self._transform_fields)
        self._transform_fields()
        layout.addWidget(transform)
        self.error = QtWidgets.QLabel()
        self.error.setWordWrap(True)
        self.error.setStyleSheet("color: #bd2929")
        layout.addWidget(self.error)
        if obj:
            model = self.edit_session.polyline
            self._load(model, list(range(len(model.vertices))))
        else:
            self._load(DXFCAD.LWPolyline([DXFCAD.LWPolylineVertex(), DXFCAD.LWPolylineVertex(10, 0)]), [-1, -1])
        self._view_update = QtCore.QTimer(self.form)
        self._view_update.setSingleShot(True)
        self._view_update.timeout.connect(self._sync_view)
        self.table.itemChanged.connect(self._schedule_view)
        self.table.model().rowsInserted.connect(self._schedule_view)
        self.table.model().rowsRemoved.connect(self._schedule_view)
        self.closed.toggled.connect(self._schedule_view)
        for field in [self.width, self.elevation, *self.extrusion]:
            field.textChanged.connect(self._schedule_view)
        self.form.destroyed.connect(self._close_view)

    def _toggle_view(self, enabled):
        if not enabled:
            self._close_view()
            return
        try:
            from dxfcad_polyline_view import PolylineViewportEditor
            self._close_view()
            self.viewport_editor = PolylineViewportEditor(self)
            self.viewport_editor.start()
            self.move_in_view.setChecked(True)
            self.error.clear()
        except Exception as error:
            self._close_view()
            self.error.setText(str(error))

    def _schedule_view(self, *args):
        self._view_update.start(0)

    def _sync_view(self):
        if self.viewport_editor is not None:
            self.viewport_editor.sync()

    def _close_view(self, *args):
        if self.viewport_editor is not None:
            self.viewport_editor.close()
            self.viewport_editor = None

    def _transform_fields(self, *args):
        index = self.operation.currentIndex()
        names = [("Delta X", "Delta Y", "Delta Z"), ("Angle (degrees)", "Pivot X", "Pivot Y"),
                 ("Scale factor", "Pivot X", "Pivot Y"), ("Line start X", "Line start Y", "Line end X", "Line end Y")][index]
        for i, (label, field) in enumerate(zip(self.parameter_labels, self.parameters)):
            label.setVisible(i < len(names))
            field.setVisible(i < len(names))
            if i < len(names):
                label.setText(names[i])
            field.setText("1" if (index == 2 and i == 0) or (index == 3 and i == 2) else "0")

    def _add_row(self, row, vertex, origin):
        self.table.insertRow(row)
        for column, value in enumerate((vertex.x, vertex.y, vertex.bulge, vertex.start_width, vertex.end_width)):
            item = QtWidgets.QTableWidgetItem(format(value, ".17g"))
            if column == 0:
                item.setData(QtCore.Qt.UserRole, origin)
            self.table.setItem(row, column, item)
        explicit = QtWidgets.QTableWidgetItem()
        explicit.setFlags(QtCore.Qt.ItemIsEnabled | QtCore.Qt.ItemIsUserCheckable | QtCore.Qt.ItemIsSelectable)
        explicit.setCheckState(QtCore.Qt.Checked if vertex.has_widths else QtCore.Qt.Unchecked)
        self.table.setItem(row, 5, explicit)

    def _load(self, model, origins):
        self.table.setRowCount(0)
        for i, vertex in enumerate(model.vertices):
            self._add_row(i, vertex, origins[i])
        self.closed.setChecked(model.closed)
        self.width.setText(format(model.constant_width, ".17g"))
        self.elevation.setText(format(model.elevation, ".17g"))
        for field, value in zip(self.extrusion, (model.extrusion.x, model.extrusion.y, model.extrusion.z)):
            field.setText(format(value, ".17g"))

    def _model(self):
        import DXFCAD

        vertices = []
        origins = []
        for row in range(self.table.rowCount()):
            values = []
            for column in range(5):
                try:
                    values.append(float(self.table.item(row, column).text()))
                except ValueError:
                    raise ValueError(f"Vertex {row + 1}: enter a number in {self.table.horizontalHeaderItem(column).text()}.") from None
            vertices.append(DXFCAD.LWPolylineVertex(*values, self.table.item(row, 5).checkState() == QtCore.Qt.Checked))
            origins.append(int(self.table.item(row, 0).data(QtCore.Qt.UserRole)))
        model = DXFCAD.LWPolyline(vertices, self.closed.isChecked(), float(self.width.text()),
                                 float(self.elevation.text()), DXFCAD.ExtrusionDirection(*(float(f.text()) for f in self.extrusion)))
        return model, origins

    def insert_vertex(self):
        import DXFCAD

        row = self.table.currentRow()
        row = self.table.rowCount() if row < 0 else row + 1
        self._add_row(row, DXFCAD.LWPolylineVertex(), -1)
        self.table.setCurrentCell(row, 0)

    def remove_vertices(self):
        for row in sorted({item.row() for item in self.table.selectedIndexes()}, reverse=True):
            self.table.removeRow(row)

    def transform(self):
        import DXFCAD

        try:
            model, origins = self._model()
            a, b, c, d = [float(field.text()) for field in self.parameters]
            index = self.operation.currentIndex()
            if index == 0:
                model = model.translated(DXFCAD.Vector3D(a, b, c))
            elif index == 1:
                model = model.rotated(math.radians(a), DXFCAD.Point2D(b, c))
            elif index == 2:
                model = model.scaled(a, DXFCAD.Point2D(b, c))
            else:
                model = model.mirrored(DXFCAD.Point2D(a, b), DXFCAD.Point2D(c, d))
            self._load(model, origins)
            self.error.clear()
        except Exception as error:
            self.error.setText(str(error))

    def accept(self):
        created_document = None
        try:
            model, origins = self._model()
            if self.obj:
                plan = self.edit_session.prepare(model, origins)
                apply_entity_update(plan, "Edit DXF LWPolyline")
            else:
                document = self.document
                if document is None:
                    created_document = document = FreeCAD.newDocument("DXFDrawing")
                self.obj = create_polyline(document, model, self.root)
            self._close_view()
            FreeCADGui.Control.closeDialog()
            return True
        except Exception as error:
            if created_document:
                FreeCAD.closeDocument(created_document.Name)
            self.error.setText(str(error))
            return False

    def reject(self):
        self._close_view()
        FreeCADGui.Control.closeDialog()
        return True


def show_editor(obj=None, root=None):
    if FreeCADGui.Control.activeDialog():
        raise ValueError("Close the current task panel before editing a polyline.")
    panel = PolylineTaskPanel(obj, root)
    FreeCADGui.Control.showDialog(panel)
    return panel
