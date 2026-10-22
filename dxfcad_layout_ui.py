"""Navigate saved DXF viewport views; viewport semantics belong to C++."""

import FreeCAD
import FreeCADGui
from PySide import QtWidgets
from pivy import coin  # Register Coin's SWIG types before getCameraNode().


def selected_drawing():
    from dxfcad_edit import drawing_root, drawing_roots

    document = FreeCAD.ActiveDocument
    if document is None:
        raise ValueError("Open an imported DXFCAD drawing first.")
    selected = {drawing_root(obj) for obj in FreeCADGui.Selection.getSelection()}
    selected.discard(None)
    if len(selected) == 1:
        return selected.pop()
    roots = drawing_roots(document)
    if not selected and len(roots) == 1:
        return roots[0]
    raise ValueError("Select one DXFCAD drawing to inspect its saved views.")


class LayerVisibilityPreview:
    """Restore only visibility changes made by this drawing's preview."""

    def __init__(self, root):
        self.document_name = root.Document.Name
        self.root_name = root.Name
        self.overrides = {}

    def restore(self):
        document = FreeCAD.listDocuments().get(self.document_name)
        if document is not None:
            for name, visible in self.overrides.items():
                obj = document.getObject(name)
                if obj is not None:
                    obj.ViewObject.Visibility = visible
        self.overrides.clear()

    def apply(self, viewport):
        document, root = self.drawing()
        self.restore()
        for obj in document.Objects:
            if (getattr(obj, "DXFSourceRoot", None) == root
                    and hasattr(obj, "DXFLayer") and viewport.freezes_layer(obj.DXFLayer)
                    and obj.ViewObject.Visibility):
                self.overrides[obj.Name] = True
                obj.ViewObject.Visibility = False

    def drawing(self):
        document = FreeCAD.listDocuments().get(self.document_name)
        root = document.getObject(self.root_name) if document else None
        if root is None:
            raise ValueError("The drawing for this saved-view preview is no longer open.")
        return document, root


def apply_saved_view(viewport, document_name, layer_preview=None):
    # Resolve/validate before changing the active camera.
    state = viewport.model_view()
    if FreeCAD.ActiveDocument is None or FreeCAD.ActiveDocument.Name != document_name:
        raise ValueError("Activate the drawing's document before opening its saved view.")
    if layer_preview is not None and layer_preview.document_name != document_name:
        raise ValueError("The layer preview belongs to a different document.")
    if layer_preview is not None:
        layer_preview.drawing()
    if viewport.frozen_layers and layer_preview is None:
        raise ValueError("Open this view through Saved DXF Views to preview its frozen layers.")
    view = FreeCADGui.activeDocument().activeView()
    def vector(p):
        return FreeCAD.Vector(p.x, p.y, p.z)
    rotation = FreeCAD.Rotation(vector(state.x_axis), vector(state.y_axis), vector(state.normal), "ZXY")
    view.setCameraType("Orthographic")
    node = view.getCameraNode()
    # Apply the pose together; setCameraOrientation can start an asynchronous
    # animation which would overwrite the center/height applied below.
    node.orientation.setValue(coin.SbRotation(*rotation.Q))
    distance = max(float(node.focalDistance.getValue()), state.height)
    center = vector(state.center)
    position = center+vector(state.normal)*distance
    node.position.setValue(position.x, position.y, position.z)
    node.focalDistance.setValue(distance)
    node.height.setValue(state.height)
    if layer_preview is not None:
        layer_preview.apply(viewport)
    FreeCADGui.updateGui()


class SavedViewsDialog(QtWidgets.QDialog):
    def __init__(self, root):
        super().__init__(FreeCADGui.getMainWindow())
        import DXFCAD

        self.document_name = root.Document.Name
        self.layer_preview = LayerVisibilityPreview(root)
        parsed = DXFCAD.drawing_document(root)
        self.viewports = list(parsed.viewports)
        self.setWindowTitle("Saved DXF Views — "+root.Label)
        self.resize(760, 340)
        layout = QtWidgets.QVBoxLayout(self)
        note = QtWidgets.QLabel("Open a saved top view of the model. Paper layouts are not rendered; "
                               "horizontal framing follows the FreeCAD window. Frozen layers are previewed while "
                               "this dialog is open; closing restores visibility.")
        note.setWordWrap(True)
        layout.addWidget(note)
        self.table = QtWidgets.QTableWidget(len(self.viewports), 5)
        self.table.setHorizontalHeaderLabels(["Layout", "Viewport", "Paper size", "Paper/model scale", "Availability"])
        self.table.setSelectionBehavior(QtWidgets.QAbstractItemView.SelectRows)
        self.table.setSelectionMode(QtWidgets.QAbstractItemView.SingleSelection)
        self.table.setEditTriggers(QtWidgets.QAbstractItemView.NoEditTriggers)
        for row, viewport in enumerate(self.viewports):
            values = [viewport.layout, str(viewport.id), f"{viewport.paper_width:g} × {viewport.paper_height:g}",
                      f"{viewport.scale:g}", viewport.navigation_issue or
                      (f"Ready ({len(viewport.frozen_layers)} frozen layers)" if viewport.frozen_layers else "Ready")]
            for column, value in enumerate(values):
                self.table.setItem(row, column, QtWidgets.QTableWidgetItem(value))
        self.table.resizeColumnsToContents()
        self.table.horizontalHeader().setStretchLastSection(True)
        layout.addWidget(self.table)
        self.message = QtWidgets.QLabel("" if self.viewports else "This drawing has no valid saved paper-space viewports.")
        self.message.setWordWrap(True)
        layout.addWidget(self.message)
        buttons = QtWidgets.QHBoxLayout()
        self.go = QtWidgets.QPushButton("Go to View")
        self.go.clicked.connect(self.open_selected)
        close = QtWidgets.QPushButton("Close")
        close.clicked.connect(self.accept)
        buttons.addStretch()
        buttons.addWidget(self.go)
        buttons.addWidget(close)
        layout.addLayout(buttons)
        self.table.itemSelectionChanged.connect(self.update_selection)
        ready = next((i for i,v in enumerate(self.viewports) if not v.navigation_issue), -1)
        if ready >= 0:
            self.table.selectRow(ready)
        self.update_selection()

    def update_selection(self):
        row = self.table.currentRow()
        self.go.setEnabled(row >= 0 and not self.viewports[row].navigation_issue)

    def open_selected(self):
        try:
            apply_saved_view(self.viewports[self.table.currentRow()], self.document_name, self.layer_preview)
            self.message.setText("Saved model view opened.")
        except Exception as error:
            self.message.setText(str(error))

    def done(self, result):
        self.layer_preview.restore()
        super().done(result)


def show_saved_views():
    dialog = SavedViewsDialog(selected_drawing())
    dialog.exec()
