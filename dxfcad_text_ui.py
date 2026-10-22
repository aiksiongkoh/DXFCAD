"""Create and edit plain native TEXT/MTEXT with a substitute display font."""
import FreeCAD
import FreeCADGui
from PySide import QtWidgets
from pivy import coin
from dxfcad_line_ui import LineViewportTool, LineTaskPanel
from dxfcad_drafting_ui import RecordPreview
from dxfcad_edit import create_text, apply_entity_update, drawing_root, drawing_roots
from dxfcad_import import default_text_font
from types import SimpleNamespace


class OriginViewportTool(LineViewportTool):
    idle_message = "Click the text origin. Escape stops picking."

    def event(self, userdata, callback):
        event = callback.getEvent()
        try:
            if event.isOfType(coin.SoKeyboardEvent.getClassTypeId()):
                if event.getKey()==coin.SoKeyboardEvent.ESCAPE and event.getState()==coin.SoButtonEvent.DOWN:
                    self.close()
                    callback.setHandled()
                return
            if not event.isOfType(coin.SoMouseButtonEvent.getClassTypeId()) or event.getButton()!=coin.SoMouseButtonEvent.BUTTON1:
                return
            if event.getState()==coin.SoButtonEvent.UP and self.owned_press:
                callback.setHandled()
                self.close()
                return
            if event.getState()!=coin.SoButtonEvent.DOWN or event.wasCtrlDown() or event.wasAltDown() or event.wasShiftDown():
                return
            callback.setHandled()
            self.owned_press = True
            point,label = self.point(event.getPosition().getValue())
            self.panel.set_origin(point)
            self.panel.error.clear()
        except Exception as error:
            self.panel.error.setText(str(error))


class TextTaskPanel(LineTaskPanel):
    def __init__(self, document, obj=None, root=None, owns_document=False):
        import DXFCAD
        self.document,self.obj,self.root = document,obj,root
        self.document_name,self.owns_document = document.Name,owns_document
        self.root_name = root.Name if root is not None else None
        self.edit_session = DXFCAD.begin_text_edit(obj) if obj is not None else None
        self.text_plan = self.text_parameters = None
        if obj is not None:
            content,height,width = self.edit_session.text_info
        else:
            content,height,width = "Text",2.5,20
        self.form = QtWidgets.QWidget()
        self.form.setWindowTitle("Edit DXF Text" if obj is not None else "Create DXF Text")
        layout = QtWidgets.QVBoxLayout(self.form)
        note = QtWidgets.QLabel("Edit plain text and height. MTEXT also supports paragraphs and wrapping width (0 means no wrap). "
                               "Coordinates use drawing units. Preview changes no objects; Apply is undoable.")
        note.setWordWrap(True)
        layout.addWidget(note)
        self.content = QtWidgets.QPlainTextEdit(content)
        self.content.setMinimumHeight(100)
        layout.addWidget(self.content)
        self.multiline = QtWidgets.QCheckBox("Multiline (MTEXT)")
        self.multiline.setChecked(obj is not None and obj.DXFType=="MTEXT")
        self.multiline.setEnabled(obj is None)
        layout.addWidget(self.multiline)
        fields = QtWidgets.QFormLayout()
        self.height = QtWidgets.QLineEdit(format(height,".17g"))
        self.width = QtWidgets.QLineEdit(format(width,".17g"))
        fields.addRow("Text height",self.height)
        fields.addRow("MTEXT width",self.width)
        self.origin_fields = [QtWidgets.QLineEdit("0") for _ in range(3)]
        self.elevation = self.origin_fields[2]
        if obj is None:
            for name,field in zip(("Origin X","Origin Y","Origin Z"),self.origin_fields):
                fields.addRow(name,field)
        layout.addLayout(fields)
        self.snap = QtWidgets.QCheckBox("Snap to DXFCAD objects")
        self.snap.setToolTip("Endpoints, vertices, centers, insertion points, midpoints, and intersections.")
        self.snap.setChecked(True)
        self.pick = QtWidgets.QPushButton("Pick text origin")
        self.pick.setCheckable(True)
        self.pick.clicked.connect(self.toggle_pick)
        if obj is None:
            layout.addWidget(self.snap)
            layout.addWidget(self.pick)
        for text,slot in (("Preview",self.preview),("Apply",self.apply)):
            button = QtWidgets.QPushButton(text)
            button.clicked.connect(slot)
            layout.addWidget(button)
        self.status,self.error = QtWidgets.QLabel(),QtWidgets.QLabel()
        for label in (self.status,self.error):
            label.setWordWrap(True)
            layout.addWidget(label)
        self.error.setStyleSheet("color: #bd2929")
        self.viewport_tool = OriginViewportTool(self)
        self.preview_tool = RecordPreview(self)
        self.form.destroyed.connect(self.viewport_tool.close)
        self.form.destroyed.connect(self.preview_tool.close)

    def set_origin(self, point):
        for field,value in zip(self.origin_fields,(point.x,point.y,point.z)):
            field.setText(format(value,".17g"))

    def raw(self):
        import DXFCAD
        self.require_document()
        content,height,width = self.content.toPlainText(),float(self.height.text()),float(self.width.text())
        if self.obj is not None:
            parameters = (content, height, width)
            if self.text_plan is None or parameters != self.text_parameters:
                self.text_plan = self.edit_session.prepare(*parameters)
                self.text_parameters = parameters
            return self.text_plan.preview_records[0][1]
        return DXFCAD.create_text(content,height,DXFCAD.Vector3D(*[float(f.text()) for f in self.origin_fields]),self.multiline.isChecked(),width)

    def preview(self):
        try:
            raw = self.raw()
            display = self.obj if self.obj is not None else SimpleNamespace(TextFont=default_text_font(),ShowWidths=False)
            self.preview_tool.show([(display,raw)])
            self.error.clear()
            self.status.setText("Orange preview. Apply to confirm or Close to cancel.")
        except Exception as error:
            self.preview_tool.close()
            self.error.setText(str(error))

    def apply(self):
        import DXFCAD
        try:
            self.raw()
            if self.obj is not None:
                apply_entity_update(self.text_plan,"Edit DXF text")
                self.edit_session = DXFCAD.begin_text_edit(self.obj)
                self.text_plan = self.text_parameters = None
            else:
                root = self.document.getObject(self.root_name) if self.root_name else None
                if self.root_name and root is None and drawing_roots(self.document):
                    raise ValueError("The drawing changed. Close and reopen this tool.")
                obj = create_text(self.document,self.content.toPlainText(),float(self.height.text()),DXFCAD.Vector3D(*[float(f.text()) for f in self.origin_fields]),self.multiline.isChecked(),float(self.width.text()),root)
                self.root = drawing_root(obj)
                self.root_name = self.root.Name
            self.preview_tool.close()
            self.error.clear()
            self.status.setText("Text applied. Use Undo to reverse it.")
        except Exception as error:
            self.preview_tool.close()
            self.error.setText(str(error))

    def reject(self):
        self.preview_tool.close()
        return super().reject()

    accept = reject


def show_text_tool(obj=None, root=None):
    if FreeCADGui.Control.activeDialog():
        raise ValueError("Close the current task panel first.")
    document = FreeCAD.ActiveDocument
    owns_document = document is None
    if obj is not None and (obj.Document != document or obj.DXFType not in ("TEXT","MTEXT")):
        raise ValueError("Select one TEXT or MTEXT object in the active document.")
    if document is None:
        document = FreeCAD.newDocument("DXFDrawing")
    if root is None and obj is None:
        roots = drawing_roots(document)
        if len(roots)>1:
            raise ValueError("Select a destination DXFCAD drawing.")
        root = roots[0] if roots else None
    if root is not None and root.Document != document:
        raise ValueError("Choose a drawing in the active document.")
    panel = TextTaskPanel(document,obj,root,owns_document)
    FreeCADGui.Control.showDialog(panel)
    return panel
