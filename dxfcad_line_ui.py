"""Interactive LINE creation. Native records and point validation live in C++."""
import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets
from pivy import coin
from dxfcad_edit import create_line, drawing_root, drawing_roots
from dxfcad_view_snap import ObjectSnapper


class LineViewportTool:
    idle_message = "Click a start point. Escape while idle stops picking."
    pending_message = "Click the next endpoint; Escape or right-click ends the chain."
    reset_message = "Chain ended. Click a new start point; completed lines are kept."

    def validate(self, start, end):
        import DXFCAD
        DXFCAD.create_line(start,end)

    def next_start(self, point):
        return point if self.panel.chain.isChecked() else None

    def __init__(self, panel):
        self.panel = panel
        self.view = self.scene = self.event_scene = self.event_node = self.callback = self.overlay = None
        self.start_point = None
        self.candidate = None
        self.owned_press = False
        self.cache = {}
        self.observing = False
        self.timer = QtCore.QTimer(panel.form)
        self.timer.setInterval(150)
        self.timer.timeout.connect(self.check_view)

    def start(self):
        self.close()
        self.panel.require_document()
        self.view = FreeCADGui.activeDocument().activeView()
        self.scene = self.view.getSceneGraph()
        try:
            self.event_node = coin.SoEventCallback()
            self.callback = self.event_node.addEventCallback(coin.SoEvent.getClassTypeId(), self.event)
            self.event_scene = self.view.getViewer().getSoEventManager().getSceneGraph()
            self.event_scene.ref()
            self.event_scene.insertChild(self.event_node,0)
            FreeCAD.addDocumentObserver(self)
            self.observing = True
            self.timer.start()
            self.panel.pick.setChecked(True)
            self.panel.status.setText(self.idle_message)
        except Exception:
            self.close()
            raise

    def point(self, position):
        import DXFCAD
        elevation = float(self.panel.elevation.text())
        target = ObjectSnapper.nearest(self.panel.document.Name,self.view,position,self.cache) if self.panel.snap.isChecked() else None
        if target is not None:
            p = target[0]
            point = DXFCAD.point_in_xy_plane(DXFCAD.Vector3D(p.x,p.y,p.z),elevation)
            label = "Snapped to "+target[1]
        else:
            a,b = self.view.projectPointToLine(int(position[0]),int(position[1]))
            point = DXFCAD.point_on_xy_plane(DXFCAD.Vector3D(a.x,a.y,a.z),DXFCAD.Vector3D(b.x,b.y,b.z),elevation)
            label = "Click to place the endpoint."
        return point,label

    def render(self, end=None):
        if self.overlay is not None:
            self.scene.removeChild(self.overlay)
            self.overlay = None
        if self.start_point is None:
            return
        overlay = coin.SoAnnotation()
        pick = coin.SoPickStyle()
        pick.style = coin.SoPickStyle.UNPICKABLE
        color = coin.SoBaseColor()
        color.rgb = (1.0,0.45,0.05)
        style = coin.SoDrawStyle()
        style.lineWidth, style.pointSize = 2, 8
        coords = coin.SoCoordinate3()
        points = [(self.start_point.x,self.start_point.y,self.start_point.z)]
        if end is not None:
            points.append((end.x,end.y,end.z))
        coords.point.setValues(0,len(points),points)
        for node in (pick,color,style,coords):
            overlay.addChild(node)
        if end is not None:
            overlay.addChild(coin.SoLineSet())
        overlay.addChild(coin.SoPointSet())
        self.scene.addChild(overlay)
        self.overlay = overlay

    def reset_chain(self):
        self.start_point = self.candidate = None
        self.panel.elevation.setEnabled(True)
        if self.scene is not None:
            self.render()
        self.panel.error.clear()
        self.panel.status.setText(self.reset_message)

    def event(self, userdata, callback):
        event = callback.getEvent()
        try:
            if event.isOfType(coin.SoKeyboardEvent.getClassTypeId()):
                if event.getKey() == coin.SoKeyboardEvent.ESCAPE and event.getState() == coin.SoButtonEvent.DOWN:
                    if self.start_point is None:
                        self.close()
                    else:
                        self.reset_chain()
                    callback.setHandled()
                return
            if event.isOfType(coin.SoMouseButtonEvent.getClassTypeId()):
                if event.getButton() == coin.SoMouseButtonEvent.BUTTON1 and event.getState() == coin.SoButtonEvent.UP:
                    if self.owned_press:
                        self.owned_press = False
                        callback.setHandled()
                    return
                if event.wasCtrlDown() or event.wasAltDown() or event.wasShiftDown() or event.getState() != coin.SoButtonEvent.DOWN:
                    return
                if event.getButton() == coin.SoMouseButtonEvent.BUTTON2 and self.start_point is not None:
                    self.reset_chain()
                    callback.setHandled()
                    return
                if event.getButton() != coin.SoMouseButtonEvent.BUTTON1:
                    return
                self.owned_press = True
                callback.setHandled()
                p,label = self.point(event.getPosition().getValue())
                if self.start_point is None:
                    self.start_point = p
                    self.panel.set_start(p)
                else:
                    self.panel.commit(self.start_point,p)
                    self.start_point = self.next_start(p)
                    if self.start_point is not None:
                        self.panel.set_start(p)
                self.panel.elevation.setEnabled(self.start_point is None)
                self.candidate = None
                self.render()
                self.panel.error.clear()
                self.panel.status.setText(self.pending_message if self.start_point is not None else self.idle_message)
            elif event.isOfType(coin.SoLocation2Event.getClassTypeId()) and self.start_point is not None:
                if event.wasCtrlDown() or event.wasAltDown() or event.wasShiftDown():
                    return
                import DXFCAD
                self.candidate = None
                p,label = self.point(event.getPosition().getValue())
                self.validate(self.start_point,p)
                self.candidate = p
                self.panel.set_end(p)
                self.render(p)
                self.panel.error.clear()
                self.panel.status.setText(label)
                callback.setHandled()
        except Exception as error:
            self.candidate = None
            self.panel.error.setText(str(error))
            if self.scene is not None:
                self.render()

    def check_view(self):
        try:
            active = FreeCADGui.activeDocument()
            if active is None or active.Document != self.panel.document or int(active.activeView().getSceneGraph().this) != int(self.scene.this):
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
            pass  # The form may have destroyed its child timer already.
        if self.observing:
            FreeCAD.removeDocumentObserver(self)
            self.observing = False
        try:
            if self.event_node is not None and self.callback is not None:
                self.event_node.removeEventCallback(coin.SoEvent.getClassTypeId(),self.callback)
            if self.event_scene is not None and self.event_node is not None:
                self.event_scene.removeChild(self.event_node)
            if self.scene is not None and self.overlay is not None:
                self.scene.removeChild(self.overlay)
        except RuntimeError:
            pass
        if self.event_scene is not None:
            self.event_scene.unref()
        self.view = self.scene = self.event_scene = self.event_node = self.callback = self.overlay = None
        self.start_point = self.candidate = None
        self.owned_press = False
        ObjectSnapper.clear(self.cache)
        try:
            self.panel.pick.setChecked(False)
            self.panel.elevation.setEnabled(True)
            self.panel.status.setText("Picking stopped. Enter coordinates or restart Pick in view.")
        except RuntimeError:
            pass


class LineTaskPanel:
    viewport_class = LineViewportTool
    title = "Draw Lines"
    note = ("Draw in the world XY plane at the chosen elevation. Each completed line is kept and undoable. "
            "Escape/right-click ends a chain; Close stops the tool. Coordinates use drawing units; new lines use layer 0.")
    numeric_label = "Create line from coordinates"
    success_message = "Line created. Use Undo to remove a completed segment."

    def __init__(self, document, root=None, owns_document=False):
        self.document, self.root, self.owns_document = document, root, owns_document
        self.document_name = document.Name
        self.root_name = root.Name if root is not None else None
        self.form = QtWidgets.QWidget()
        self.form.setWindowTitle(self.title)
        layout = QtWidgets.QVBoxLayout(self.form)
        note = QtWidgets.QLabel(self.note)
        note.setWordWrap(True)
        layout.addWidget(note)
        fields = QtWidgets.QFormLayout()
        self.start_fields = [QtWidgets.QLineEdit("0"),QtWidgets.QLineEdit("0")]
        self.end_fields = [QtWidgets.QLineEdit("10"),QtWidgets.QLineEdit("0")]
        self.elevation = QtWidgets.QLineEdit("0")
        for name,field in zip(("Start X","Start Y","End X","End Y","Elevation Z"),[*self.start_fields,*self.end_fields,self.elevation]):
            fields.addRow(name,field)
        layout.addLayout(fields)
        self.snap = QtWidgets.QCheckBox("Snap to DXFCAD objects")
        self.snap.setToolTip("Endpoints, vertices, centers, insertion points, midpoints, and intersections.")
        self.snap.setChecked(True)
        self.chain = QtWidgets.QCheckBox("Continue from the last endpoint")
        self.chain.setChecked(True)
        layout.addWidget(self.snap)
        layout.addWidget(self.chain)
        self.pick = QtWidgets.QPushButton("Pick in view")
        self.pick.setCheckable(True)
        self.pick.clicked.connect(self.toggle_pick)
        layout.addWidget(self.pick)
        numeric = QtWidgets.QPushButton(self.numeric_label)
        numeric.clicked.connect(self.create_numeric)
        layout.addWidget(numeric)
        self.status = QtWidgets.QLabel()
        self.status.setWordWrap(True)
        self.error = QtWidgets.QLabel()
        self.error.setWordWrap(True)
        self.error.setStyleSheet("color: #bd2929")
        layout.addWidget(self.status)
        layout.addWidget(self.error)
        self.viewport_tool = self.viewport_class(self)
        self.form.destroyed.connect(self.viewport_tool.close)

    def require_document(self):
        if FreeCAD.ActiveDocument != self.document:
            raise ValueError("Activate this drawing tool's document first.")

    def set_start(self, point):
        for field,value in zip(self.start_fields,(point.x,point.y)):
            field.setText(format(value,".17g"))

    def set_end(self, point):
        for field,value in zip(self.end_fields,(point.x,point.y)):
            field.setText(format(value,".17g"))

    def commit(self, start, end):
        self.require_document()
        root = self.document.getObject(self.root_name) if self.root_name else None
        if self.root_name and root is None and drawing_roots(self.document):
            raise ValueError("The selected drawing was removed. Close and reopen the line tool.")
        obj = create_line(self.document,start,end,root)
        self.root = drawing_root(obj)
        self.root_name = self.root.Name
        self.set_end(end)
        self.status.setText(self.success_message)
        return obj

    def create_numeric(self):
        import DXFCAD
        try:
            z = float(self.elevation.text())
            a = DXFCAD.Vector3D(*[float(f.text()) for f in self.start_fields],z)
            b = DXFCAD.Vector3D(*[float(f.text()) for f in self.end_fields],z)
            self.commit(a,b)
            self.viewport_tool.reset_chain()
            if self.chain.isChecked():
                self.set_start(b)
                if self.viewport_tool.view is not None:
                    self.viewport_tool.start_point = b
                    self.elevation.setEnabled(False)
                    self.viewport_tool.render()
            self.status.setText(self.success_message)
            self.error.clear()
        except Exception as error:
            self.error.setText(str(error))

    def toggle_pick(self, enabled):
        try:
            self.viewport_tool.start() if enabled else self.viewport_tool.close()
        except Exception as error:
            self.error.setText(str(error))

    def getStandardButtons(self):
        button = QtWidgets.QDialogButtonBox.Close
        return int(getattr(button,"value",button))

    def reject(self):
        self.viewport_tool.close()
        FreeCADGui.Control.closeDialog()
        document = FreeCAD.listDocuments().get(self.document_name)
        if self.owns_document and document is not None and not document.Objects:
            FreeCAD.closeDocument(document.Name)
        return True

    accept = reject


def show_line_tool(root=None,panel_type=LineTaskPanel):
    if FreeCADGui.Control.activeDialog():
        raise ValueError("Close the current task panel before drawing lines.")
    document = FreeCAD.ActiveDocument
    owns_document = document is None
    if root is None and document is not None:
        roots = drawing_roots(document)
        if len(roots) > 1:
            raise ValueError("Select one DXFCAD drawing before drawing lines.")
        root = roots[0] if roots else None
    if document is None:
        document = FreeCAD.newDocument("DXFDrawing")
    if root is not None and (root.Document != document or drawing_root(root) != root):
        raise ValueError("Select a DXFCAD drawing in the active document.")
    panel = panel_type(document,root,owns_document)
    FreeCADGui.Control.showDialog(panel)
    panel.toggle_pick(True)
    return panel
