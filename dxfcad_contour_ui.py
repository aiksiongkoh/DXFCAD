"""Contour cleanup task panels; all geometry and topology live in C++."""
import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets
from pivy import coin
from dxfcad_drafting_ui import SelectionTaskPanel
from dxfcad_edit import apply_contour_plan


class ContourPicker:
    def __init__(self,panel):
        self.panel=panel
        self.node=self.scene=self.callback=None
        self.owned_press=False
        self.observing=False
        self.timer=QtCore.QTimer(panel.form)
        self.timer.setInterval(150)
        self.timer.timeout.connect(self.check_view)

    def start(self):
        self.close()
        self.panel.require_document()
        self.view=FreeCADGui.activeDocument().activeView()
        self.node=coin.SoEventCallback()
        self.callback=self.node.addEventCallback(coin.SoEvent.getClassTypeId(),self.event)
        self.scene=self.view.getViewer().getSoEventManager().getSceneGraph()
        self.scene.ref()
        self.scene.insertChild(self.node,0)
        FreeCAD.addDocumentObserver(self)
        self.observing=True
        self.timer.start()
        self.panel.status.setText("Click near the target contour. Escape or right-click stops picking.")

    def event(self,userdata,callback):
        import DXFCAD
        event=callback.getEvent()
        if event.isOfType(coin.SoKeyboardEvent.getClassTypeId()):
            if event.getKey()==coin.SoKeyboardEvent.ESCAPE and event.getState()==coin.SoButtonEvent.DOWN:
                self.close(); callback.setHandled()
            return
        if not event.isOfType(coin.SoMouseButtonEvent.getClassTypeId()):
            return
        if event.getButton()==coin.SoMouseButtonEvent.BUTTON1 and event.getState()==coin.SoButtonEvent.UP:
            if self.owned_press:
                self.owned_press=False; callback.setHandled()
            return
        if event.getState()!=coin.SoButtonEvent.DOWN or event.wasCtrlDown() or event.wasAltDown() or event.wasShiftDown():
            return
        if event.getButton()==coin.SoMouseButtonEvent.BUTTON2:
            self.close(); callback.setHandled(); return
        if event.getButton()!=coin.SoMouseButtonEvent.BUTTON1:
            return
        callback.setHandled(); self.owned_press=True
        try:
            self.panel.require_snapshot()
            a,b=self.view.projectPointToLine(*map(int,event.getPosition().getValue()))
            distance=DXFCAD.Contour.distance_at_ray(self.panel.target_object().DXFData,
                DXFCAD.Vector3D(a.x,a.y,a.z),DXFCAD.Vector3D(b.x,b.y,b.z))
            self.panel.set_pick(distance)
        except Exception as error:
            self.panel.error.setText(str(error))

    def check_view(self):
        try:
            gui=FreeCADGui.activeDocument()
            if gui is None or gui.Document!=self.panel.document or int(gui.activeView().getViewer().getSoEventManager().getSceneGraph().this)!=int(self.scene.this):
                self.close()
        except RuntimeError:
            self.close()

    def slotDeletedDocument(self,document):
        if document==self.panel.document:
            self.close()

    def close(self,*args):
        try: self.timer.stop()
        except RuntimeError: pass
        if self.observing:
            FreeCAD.removeDocumentObserver(self); self.observing=False
        if self.node is not None and self.callback is not None:
            self.node.removeEventCallback(coin.SoEvent.getClassTypeId(),self.callback)
        if self.scene is not None:
            if self.node is not None and self.scene.findChild(self.node)>=0:
                self.scene.removeChild(self.node)
            self.scene.unref()
        self.node=self.scene=self.callback=None
        try: self.panel.pick.setChecked(False)
        except RuntimeError: pass


class ContourTaskPanel(SelectionTaskPanel):
    def capture(self):
        super().capture()
        self.edit_plan = self.edit_parameters = None

    titles={"join":"Join Contours","split":"Split Contour","break":"Break Contour",
            "close":"Close Polyline","trim":"Trim Interior Section"}
    notes={
        "join":"Join connected coplanar lines, arcs and open polylines. Order and direction are resolved automatically; branches and conflicting styles/metadata are rejected. Replaces the selection with a new polyline.",
        "split":"Split at distances along the native contour, separated by commas. A single cut opens a closed contour at that seam. Pick near the contour to append a distance.",
        "break":"Remove the interval between two distances. On closed contours the interval follows the native direction from First to Second, including across the seam.",
        "close":"Close an open polyline with a straight segment, or merge its duplicate end vertex when within tolerance.",
        "trim":"Choose the target; all other selected objects are finite boundaries. Pick the section to remove, or enter a distance along the target. Tangencies do not cut; intersections and overlaps are ambiguous picks."}

    def __init__(self,objects,operation):
        if operation not in self.titles:
            raise ValueError("Unknown contour cleanup operation.")
        if operation in ("join","trim") and len(objects)<2:
            raise ValueError("Select at least two contours.")
        if operation not in ("join","trim") and len(objects)!=1:
            raise ValueError("Select one contour.")
        if any(getattr(obj,"DXFType","") not in ("LINE","ARC","CIRCLE","LWPOLYLINE") for obj in objects):
            raise ValueError("Contour cleanup supports LINE, ARC, CIRCLE and LWPOLYLINE.")
        self.operation=operation
        super().__init__(objects,self.titles[operation],self.notes[operation])
        self.target=QtWidgets.QComboBox()
        self.target.addItems([obj.Label+" ("+obj.Name+")" for obj in objects])
        if operation=="trim": self.fields.addRow("Target",self.target)
        self.first=QtWidgets.QLineEdit("0")
        self.second=QtWidgets.QLineEdit("1")
        self.tolerance=QtWidgets.QLineEdit("0.0000001")
        self.pick=QtWidgets.QPushButton("Pick on contour")
        self.pick.setCheckable(True)
        self.pick_field=QtWidgets.QComboBox()
        self.pick_field.addItems(["First","Second"])
        if operation in ("join","close"):
            self.fields.addRow("Endpoint tolerance",self.tolerance)
        else:
            self.fields.addRow("Distances" if operation=="split" else "First distance" if operation=="break" else "Pick distance",self.first)
            if operation=="break":
                self.fields.addRow("Second distance",self.second)
                self.fields.addRow("Pick sets",self.pick_field)
            self.fields.addRow(self.pick)
        self.length_label=QtWidgets.QLabel()
        if operation not in ("join","close"): self.fields.addRow("Target length",self.length_label)
        self.picker=ContourPicker(self)
        self.pick.clicked.connect(self.toggle_pick)
        self.target.currentIndexChanged.connect(self.target_changed)
        self.form.destroyed.connect(self.picker.close)
        self.target_changed()

    def target_object(self):
        return self.objects[self.target.currentIndex()]

    def target_changed(self,*args):
        import DXFCAD
        self.viewport_tool.close()
        if self.operation not in ("join","close"):
            self.length_label.setText(format(DXFCAD.Contour.length(self.target_object().DXFData),".12g"))
        self.first.setText("" if self.operation=="split" else "0")

    def toggle_pick(self,enabled):
        if enabled:
            try: self.picker.start()
            except Exception as error: self.picker.close(); self.error.setText(str(error))
        else: self.picker.close()

    def set_pick(self,distance):
        value=format(distance,".17g")
        if self.operation=="split":
            values=[v.strip() for v in self.first.text().split(",") if v.strip()]
            self.first.setText(", ".join(values+[value]))
        elif self.operation=="break":
            (self.second if self.pick_field.currentIndex() else self.first).setText(value)
            self.pick_field.setCurrentIndex(1-self.pick_field.currentIndex())
        else: self.first.setText(value)
        if self.operation!="break" or self.pick_field.currentIndex()==0:
            self.preview()
        if self.operation=="break" and self.pick_field.currentIndex()==1:
            self.error.clear()

    def records(self):
        import DXFCAD
        self.require_snapshot()
        distances = ()
        tolerance = 1e-7
        if self.operation in ("join", "close"):
            tolerance = float(self.tolerance.text())
        elif self.operation == "split":
            distances = tuple(float(v.strip()) for v in self.first.text().split(",") if v.strip())
        else:
            distances = (float(self.first.text()),)
            if self.operation == "break": distances += (float(self.second.text()),)
        parameters = (self.operation, self.target.currentIndex(), distances, tolerance)
        if self.edit_plan is None or parameters != self.edit_parameters:
            self.edit_plan = DXFCAD.prepare_contour_operation(self.objects, self.operation,
                target_index=parameters[1], distances=distances, tolerance=tolerance)
            self.edit_parameters = parameters
        return self.edit_plan.preview_records

    def commit_records(self,records):
        self.result=apply_contour_plan(self.edit_plan,self.titles[self.operation])

    def apply(self):
        try:
            self.commit_records(self.records())
            self.viewport_tool.close()
            self.error.clear()
            self.reject()
        except Exception as error:
            self.viewport_tool.close()
            self.error.setText(str(error))

    def reject(self):
        self.picker.close()
        return super().reject()


def show_contour_tool(operation,objects=None):
    if FreeCADGui.Control.activeDialog():
        raise ValueError("Close the current task panel first.")
    objects=FreeCADGui.Selection.getSelection() if objects is None else list(objects)
    if not objects or any(obj.Document!=FreeCAD.ActiveDocument for obj in objects):
        raise ValueError("Select native contours in the active document.")
    panel=ContourTaskPanel(objects,operation)
    FreeCADGui.Control.showDialog(panel)
    return panel
