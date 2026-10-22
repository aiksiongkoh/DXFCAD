"""Three-point ARC picking and numeric center/angle creation."""
from PySide import QtWidgets
from pivy import coin
from dxfcad_line_ui import LineViewportTool, LineTaskPanel, show_line_tool
from dxfcad_edit import create_arc, create_arc_through_points, drawing_root, drawing_roots


class ArcViewportTool(LineViewportTool):
    idle_message="Click the arc start point. Escape while idle stops picking."
    reset_message="Arc preview canceled. Click a new start point."

    def __init__(self, panel):
        self.through_point=None
        super().__init__(panel)

    def reset_chain(self):
        self.through_point=None
        super().reset_chain()

    def close(self, *args):
        self.through_point=None
        super().close(*args)

    def render(self, end=None):
        if self.through_point is None:
            super().render(end)
            return
        super().render()
        if self.overlay is None:
            return
        coords=coin.SoCoordinate3()
        p=self.through_point
        coords.point.setValue(p.x,p.y,p.z)
        self.overlay.addChild(coords)
        self.overlay.addChild(coin.SoPointSet())
        if end is not None:
            import DXFCAD
            raw=DXFCAD.create_arc_through_points(self.start_point,self.through_point,end)
            shape=DXFCAD.entity_preview_shape(raw)
            source=coin.SoInput()
            source.setBuffer(shape.writeInventor(1))
            geometry=coin.SoDB.readAll(source)
            if geometry is None:
                raise ValueError("Cannot display arc preview.")
            self.overlay.addChild(geometry)

    def event(self, userdata, callback):
        event=callback.getEvent()
        modified=event.wasCtrlDown() or event.wasAltDown() or event.wasShiftDown()
        click=(event.isOfType(coin.SoMouseButtonEvent.getClassTypeId())
               and event.getButton()==coin.SoMouseButtonEvent.BUTTON1
               and event.getState()==coin.SoButtonEvent.DOWN and not modified)
        motion=(event.isOfType(coin.SoLocation2Event.getClassTypeId())
                and self.start_point is not None and not modified)
        if not click and not motion:
            # Reuse navigation gestures, owned button releases, Escape/right
            # click handling and document/view cleanup from the shared tool.
            return super().event(userdata,callback)
        try:
            import DXFCAD
            callback.setHandled()
            if click:
                self.owned_press=True
            p,label=self.point(event.getPosition().getValue())
            self.candidate=None
            if click:
                if self.start_point is None:
                    self.start_point=p
                    self.panel.set_start(p)
                elif self.through_point is None:
                    DXFCAD.displacement(self.start_point,p)
                    self.through_point=p
                    self.panel.set_through(p)
                else:
                    self.panel.commit(self.start_point,self.through_point,p)
                    self.start_point=self.through_point=None
                self.panel.elevation.setEnabled(self.start_point is None)
                self.render()
                self.panel.status.setText(self.idle_message if self.start_point is None else
                                          "Click a point through the arc." if self.through_point is None else
                                          "Click the arc end point. Escape/right-click cancels the preview.")
            else:
                if self.through_point is None:
                    DXFCAD.displacement(self.start_point,p)
                    self.panel.set_through(p)
                else:
                    DXFCAD.create_arc_through_points(self.start_point,self.through_point,p)
                    self.panel.set_end(p)
                self.candidate=p
                self.render(p)
                self.panel.status.setText(label)
            self.panel.error.clear()
        except Exception as error:
            self.candidate=None
            self.panel.error.setText(str(error))
            if self.scene is not None:
                self.render()


class ArcTaskPanel(LineTaskPanel):
    title="Draw Arcs"

    def __init__(self, document, root=None, owns_document=False):
        self.document,self.root,self.owns_document=document,root,owns_document
        self.document_name=document.Name
        self.root_name=root.Name if root is not None else None
        self.form=QtWidgets.QWidget()
        self.form.setWindowTitle(self.title)
        layout=QtWidgets.QVBoxLayout(self.form)
        note=QtWidgets.QLabel("Pick Start, Through, then End, or enter numeric values. Arcs lie in world XY at Elevation Z, on layer 0. Coordinates use drawing units; angles use degrees from world +X. Each arc is undoable. Escape/right-click cancels a pending arc; Close stops the tool.")
        note.setWordWrap(True)
        layout.addWidget(note)
        self.mode=QtWidgets.QTabWidget()
        points=QtWidgets.QWidget()
        fields=QtWidgets.QFormLayout(points)
        self.start_fields=[QtWidgets.QLineEdit("5"),QtWidgets.QLineEdit("0")]
        self.through_fields=[QtWidgets.QLineEdit("0"),QtWidgets.QLineEdit("5")]
        self.end_fields=[QtWidgets.QLineEdit("-5"),QtWidgets.QLineEdit("0")]
        for name,field in zip(("Start X","Start Y","Through X","Through Y","End X","End Y"),
                              [*self.start_fields,*self.through_fields,*self.end_fields]):
            fields.addRow(name,field)
        numeric=QtWidgets.QWidget()
        fields=QtWidgets.QFormLayout(numeric)
        self.center_fields=[QtWidgets.QLineEdit("0"),QtWidgets.QLineEdit("0")]
        self.radius,self.start_angle,self.end_angle=QtWidgets.QLineEdit("5"),QtWidgets.QLineEdit("0"),QtWidgets.QLineEdit("90")
        for name,field in zip(("Center X","Center Y","Radius","Start angle","End angle"),
                              [*self.center_fields,self.radius,self.start_angle,self.end_angle]):
            fields.addRow(name,field)
        self.clockwise=QtWidgets.QCheckBox("Clockwise")
        fields.addRow(self.clockwise)
        self.mode.addTab(points,"Three points")
        self.mode.addTab(numeric,"Center and angles")
        layout.addWidget(self.mode)
        fields=QtWidgets.QFormLayout()
        self.elevation=QtWidgets.QLineEdit("0")
        fields.addRow("Elevation Z",self.elevation)
        layout.addLayout(fields)
        self.snap=QtWidgets.QCheckBox("Snap to DXFCAD objects")
        self.snap.setToolTip("Endpoints, vertices, centers, insertion points, midpoints, and intersections.")
        self.snap.setChecked(True)
        layout.addWidget(self.snap)
        self.pick=QtWidgets.QPushButton("Pick three points in view")
        self.pick.setCheckable(True)
        self.pick.clicked.connect(self.toggle_pick)
        layout.addWidget(self.pick)
        self.numeric=QtWidgets.QPushButton("Create arc from three points")
        self.numeric.clicked.connect(self.create_numeric)
        layout.addWidget(self.numeric)
        self.status,self.error=QtWidgets.QLabel(),QtWidgets.QLabel()
        for label in (self.status,self.error):
            label.setWordWrap(True)
            layout.addWidget(label)
        self.error.setStyleSheet("color: #bd2929")
        self.viewport_tool=ArcViewportTool(self)
        self.form.destroyed.connect(self.viewport_tool.close)
        self.mode.currentChanged.connect(self.mode_changed)

    def mode_changed(self, index):
        if index!=0:
            self.viewport_tool.close()
        self.numeric.setText("Create arc from three points" if index==0 else "Create arc from center and angles")

    def toggle_pick(self, enabled):
        if enabled:
            self.mode.setCurrentIndex(0)
        super().toggle_pick(enabled)

    def set_through(self, point):
        for field,value in zip(self.through_fields,(point.x,point.y)):
            field.setText(format(value,".17g"))

    def target_root(self):
        self.require_document()
        root=self.document.getObject(self.root_name) if self.root_name else None
        if self.root_name and root is None and drawing_roots(self.document):
            raise ValueError("The selected drawing was removed. Close and reopen the arc tool.")
        return root

    def adopt(self, obj):
        self.root=drawing_root(obj)
        self.root_name=self.root.Name
        return obj

    def commit(self, start, through, end):
        return self.adopt(create_arc_through_points(self.document,start,through,end,self.target_root()))

    def create_numeric(self):
        import DXFCAD
        try:
            z=float(self.elevation.text())
            if self.mode.currentIndex()==0:
                points=[DXFCAD.Vector3D(*[float(field.text()) for field in fields],z)
                        for fields in (self.start_fields,self.through_fields,self.end_fields)]
                self.commit(*points)
            else:
                center=DXFCAD.Vector3D(*[float(field.text()) for field in self.center_fields],z)
                self.adopt(create_arc(self.document,center,float(self.radius.text()),float(self.start_angle.text()),
                                      float(self.end_angle.text()),self.clockwise.isChecked(),self.target_root()))
            self.viewport_tool.reset_chain()
            self.error.clear()
            self.status.setText("Arc created. Pick another arc or use Undo to remove it.")
        except Exception as error:
            self.error.setText(str(error))


def show_arc_tool(root=None):
    return show_line_tool(root,panel_type=ArcTaskPanel)
