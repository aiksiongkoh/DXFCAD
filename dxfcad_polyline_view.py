"""Transient Coin handles and viewport events for LWPolyline vertex moves."""

import FreeCAD
import FreeCADGui
from PySide import QtCore
from pivy import coin
from dxfcad_view_snap import ObjectSnapper


class PolylineViewportEditor:
    pick_radius = 12  # Screen pixels, independent of drawing units and zoom.

    def __init__(self, panel):
        self.panel = panel
        self.view = None
        self.scene = None
        self.overlay = None
        self.callback = None
        self.event_node = None
        self.event_scene = None
        self.index = None
        self.candidate = None
        self.press_position = None
        self.dragged = False
        self.observing = False
        self._snap_cache = {}
        self.timer = QtCore.QTimer(panel.form)
        self.timer.setInterval(150)
        self.timer.timeout.connect(self._check_view)

    def start(self):
        document = FreeCADGui.activeDocument()
        if document is None or (self.panel.document is not None and document.Document != self.panel.document):
            raise ValueError("Activate this polyline's document before moving vertices in the view.")
        self.model, self.origins = self.panel._model()
        self.model.world_segments()
        self.view = document.activeView()
        self.document_name = document.Document.Name
        self.scene = self.view.getSceneGraph()
        try:
            self._render(self.model)
            # FreeCAD's built-in selection callback can consume releases over
            # other objects. A leading Coin callback receives the entire owned
            # gesture before selection and leaves unrelated events unhandled.
            self.event_node = coin.SoEventCallback()
            # Pivy removal requires the returned registration token, not the
            # callable (older bindings can corrupt memory with the latter).
            self.callback = self.event_node.addEventCallback(coin.SoEvent.getClassTypeId(), self._coin_event)
            # The event-manager root includes viewer-owned selection nodes that
            # sit outside getSceneGraph() in some FreeCAD versions.
            self.event_scene = self.view.getViewer().getSoEventManager().getSceneGraph()
            # This getter returns a borrowed Coin node. Keep it alive until
            # document-close notifications have detached our callback.
            self.event_scene.ref()
            self.event_scene.insertChild(self.event_node, 0)
            FreeCAD.addDocumentObserver(self)
            self.observing = True
            self.timer.start()
            self.panel.view_status.setText("Drag a vertex handle and release to place, or click a handle then its new position. Escape or right-click cancels.")
        except Exception:
            self.close()
            raise

    def _render(self, model):
        import DXFCAD

        # FreeCAD derives/tessellates the analytic centerline. Nothing is added
        # to the document, and the source shape/visibility are never changed.
        shape = DXFCAD.polyline_preview_shape(model)
        source = coin.SoInput()
        source.setBuffer(shape.writeInventor(1))
        geometry = coin.SoDB.readAll(source)
        if geometry is None:
            raise ValueError("Cannot display the polyline preview.")
        # Keep handles/centerlines visible over the source's filled width faces.
        overlay = coin.SoAnnotation()
        pick = coin.SoPickStyle()
        pick.style = coin.SoPickStyle.UNPICKABLE
        color = coin.SoBaseColor()
        color.rgb = (1.0, 0.45, 0.05)
        color.setOverride(True)  # Part's Inventor output carries its own colors.
        lighting = coin.SoLightModel()
        lighting.model = coin.SoLightModel.BASE_COLOR
        lighting.setOverride(True)
        style = coin.SoDrawStyle()
        style.lineWidth = 2
        style.pointSize = 9
        coordinates = coin.SoCoordinate3()
        points = [(p.x, p.y, p.z) for p in model.world_vertices()]
        coordinates.point.setValues(0, len(points), points)
        for node in (pick, color, lighting, style, geometry, coordinates, coin.SoPointSet()):
            overlay.addChild(node)
        self.scene.addChild(overlay)
        if self.overlay is not None:
            self.scene.removeChild(self.overlay)
        self.overlay = overlay

    def _nearest(self, position, exclude=None):
        nearest, distance = None, self.pick_radius**2
        for index, point in enumerate(self.model.world_vertices()):
            if index == exclude:
                continue
            screen = self.view.getPointOnScreen(FreeCAD.Vector(point.x, point.y, point.z))
            squared = (screen[0]-position[0])**2 + (screen[1]-position[1])**2
            if squared < distance:
                nearest, distance = index, squared
        return nearest

    def _preview(self, position):
        import DXFCAD

        try:
            target = self._snap_target(position)
            if target is not None:
                point, label, own_index = target
                candidate = (self.model.with_vertex_at_vertex(self.index, own_index) if own_index is not None else
                             self.model.with_vertex_at_world_point(self.index, DXFCAD.Vector3D(point.x,point.y,point.z)))
            else:
                start, end = self.view.projectPointToLine(int(position[0]), int(position[1]))
                candidate = self.model.with_vertex_on_line(self.index,
                    DXFCAD.Vector3D(start.x, start.y, start.z), DXFCAD.Vector3D(end.x, end.y, end.z))
            self._render(candidate)
            self.candidate = candidate
            self.panel.error.clear()
            self.panel.view_status.setText("Snapped to %s. Release or click to place." % target[1]
                                           if target is not None else "Release or click to place; Escape or right-click cancels this move.")
        except Exception as error:
            self.candidate = None
            self.panel.error.setText(str(error))

    _visible = staticmethod(ObjectSnapper.visible)

    def _snap_target(self, position):
        import DXFCAD

        nearest, distance = None, self.pick_radius**2

        def consider(point, label, own_index=None):
            nonlocal nearest, distance
            screen = self.view.getPointOnScreen(FreeCAD.Vector(point.x,point.y,point.z))
            squared = (screen[0]-position[0])**2 + (screen[1]-position[1])**2
            if squared < distance:
                nearest, distance = (point,label,own_index), squared

        if self.panel.snap_vertices.isChecked():
            for index, point in enumerate(self.model.world_vertices()):
                if index != self.index:
                    consider(point,"vertex %d" % (index+1),index)
        if self.panel.snap_objects.isChecked():
            from dxfcad_view_snap import ObjectSnapper
            target = ObjectSnapper.nearest(self.document_name, self.view, position,
                                           self._snap_cache, self.panel.obj, self.pick_radius)
            if target is not None:
                consider(target[0], target[1])
        return nearest

    def _place(self):
        if self.candidate is None:
            return False
        index = self.index
        self.model = self.candidate
        self.index = self.candidate = self.press_position = None
        self.dragged = False
        self.panel._load(self.model, self.origins)
        self.panel.table.setCurrentCell(index, 0)
        self.panel.view_status.setText("Vertex placed in the table. Move another handle, or press OK to commit.")
        return True

    def _cancel_move(self):
        self.index = None
        self.candidate = None
        self.press_position = None
        self.dragged = False
        self._render(self.model)
        self.panel.error.clear()
        self.panel.view_status.setText("Click a vertex handle, then click its new position.")

    def _event(self, callback):
        event = callback.getEvent()
        try:
            if event.isOfType(coin.SoKeyboardEvent.getClassTypeId()):
                if event.getKey() == coin.SoKeyboardEvent.ESCAPE and event.getState() == coin.SoButtonEvent.DOWN:
                    if self.index is None:
                        self.close()
                    else:
                        self._cancel_move()
                    callback.setHandled()
            elif event.isOfType(coin.SoMouseButtonEvent.getClassTypeId()):
                position = event.getPosition().getValue()
                if event.getButton() == coin.SoMouseButtonEvent.BUTTON1 and event.getState() == coin.SoButtonEvent.UP:
                    if self.press_position is not None:
                        delta = (position[0]-self.press_position[0])**2 + (position[1]-self.press_position[1])**2
                        self.dragged = self.dragged or delta >= 16
                        if event.wasAltDown() or event.wasCtrlDown() or event.wasShiftDown():
                            self._cancel_move()
                        elif self.dragged:
                            self._preview(position)
                            if not self._place():
                                error = self.panel.error.text()
                                self._cancel_move()
                                self.panel.error.setText(error)
                        self.press_position = None
                        self.dragged = False
                        callback.setHandled()
                    return
                if event.wasAltDown() or event.wasCtrlDown() or event.wasShiftDown():
                    return  # Leave modified mouse gestures to FreeCAD navigation.
                if event.getState() != coin.SoButtonEvent.DOWN:
                    return
                if event.getButton() == coin.SoMouseButtonEvent.BUTTON2 and self.index is not None:
                    self._cancel_move()
                    callback.setHandled()
                elif event.getButton() == coin.SoMouseButtonEvent.BUTTON1:
                    if self.index is None:
                        index = self._nearest(position)
                        if index is None:
                            return
                        self.index = index
                        self.press_position = position
                        self.dragged = False
                        self.panel.table.setCurrentCell(index, 0)
                        self._preview(position)
                    else:
                        self._preview(position)
                        self._place()
                    callback.setHandled()
            elif event.isOfType(coin.SoLocation2Event.getClassTypeId()) and self.index is not None:
                if event.wasAltDown() or event.wasCtrlDown() or event.wasShiftDown():
                    return
                position = event.getPosition().getValue()
                if self.press_position is not None:
                    delta = (position[0]-self.press_position[0])**2 + (position[1]-self.press_position[1])**2
                    self.dragged = self.dragged or delta >= 16
                self._preview(position)
                callback.setHandled()
        except Exception as error:
            self.panel.error.setText(str(error))

    def _coin_event(self, userdata, callback):
        self._event(callback)

    def sync(self):
        if self.view is None:
            return
        try:
            model, origins = self.panel._model()
            self._render(model)
            self.model, self.origins = model, origins
            self.index = None
            self.candidate = None
            self.press_position = None
            self.dragged = False
            self.panel.error.clear()
        except Exception as error:
            self.panel.error.setText(str(error))
            self.close()

    def _check_view(self):
        try:
            active = FreeCADGui.activeDocument()
            if active is None or active.Document.Name != self.document_name:
                self.close()
            elif int(active.activeView().getSceneGraph().this) != int(self.scene.this):
                self.close()
        except RuntimeError:
            self.close()

    def slotDeletedDocument(self, document):
        if document.Name == self.document_name:
            self.close()

    def close(self, *args):
        self.timer.stop()
        if self.observing:
            FreeCAD.removeDocumentObserver(self)
            self.observing = False
        try:
            if self.event_node is not None and self.callback is not None:
                self.event_node.removeEventCallback(coin.SoEvent.getClassTypeId(), self.callback)
            if self.event_scene is not None and self.event_node is not None:
                self.event_scene.removeChild(self.event_node)
            if self.scene is not None and self.overlay is not None:
                self.scene.removeChild(self.overlay)
        except RuntimeError:
            pass  # The document or view may already have been closed.
        if self.event_scene is not None:
            self.event_scene.unref()
        self.callback = self.event_node = self.overlay = self.scene = self.view = None
        self.event_scene = None
        ObjectSnapper.clear(self._snap_cache)
        self.index = self.candidate = None
        self.press_position = None
        self.dragged = False
        try:
            self.panel.move_in_view.setChecked(False)
            self.panel.view_status.clear()
        except RuntimeError:
            pass  # Qt is destroying the task panel.
