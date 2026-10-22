"""FreeCAD lifecycle and presentation adapters for C++ DXF domain objects."""

import weakref


class _CircleSelectionObserver:
    """One selection observer; weak references do not retain closed documents."""

    def __init__(self):
        self.providers = weakref.WeakSet()
        self.selected = set()

    def refresh(self, *args):
        import FreeCADGui

        self.selected = {(obj.Document.Name, obj.Name)
                         for obj in FreeCADGui.Selection.getSelection("*")}
        for provider in list(self.providers):
            provider._update_circle_center(self.selected)

    addSelection = refresh
    removeSelection = refresh
    clearSelection = refresh
    setSelection = refresh


_circle_selection_observer = None


def _observe_circle(provider):
    global _circle_selection_observer
    if _circle_selection_observer is None:
        import FreeCADGui

        _circle_selection_observer = _CircleSelectionObserver()
        FreeCADGui.Selection.addObserver(_circle_selection_observer)
    _circle_selection_observer.providers.add(provider)
    _circle_selection_observer.refresh()


class DXFObjectProxy:
    """Persist native records on the object; delegate all domain work to C++."""

    def __init__(self, obj):
        obj.Proxy = self

    def execute(self, obj):
        import DXFCAD

        DXFCAD.recompute_feature(obj)

    def onDocumentRestored(self, obj):
        import DXFCAD

        DXFCAD.ensure_display_properties(obj)
        obj.touch()

    def dumps(self):
        return None

    def loads(self, state):
        pass


def display_color(color):
    """Choose a visible foreground for white native strokes on a light theme."""
    if tuple(color) == (1.0,1.0,1.0):
        import FreeCAD
        packed = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").GetUnsigned("DefaultShapeLineColor",0x191919FF)
        color = tuple(((packed >> shift) & 255)/255 for shift in (24,16,8))
    return tuple(color)


class DXFViewProvider:
    """Use FreeCAD's standard Part view provider for derived shapes."""

    def __init__(self, view_object, color):
        color = display_color(color)
        view_object.Proxy = self
        self._update_width_mode(view_object)
        view_object.LineColor = tuple(color)
        view_object.ShapeColor = tuple(color)
        view_object.PointColor = tuple(color)
        view_object.LineWidth = 1.5

    def doubleClicked(self, view_object):
        if getattr(view_object.Object, "DXFType", "") == "LWPOLYLINE":
            from dxfcad_polyline_ui import show_editor
            try:
                show_editor(view_object.Object)
            except Exception as error:
                import FreeCAD
                FreeCAD.Console.PrintError(f"DXFCAD: {error}\n")
            return True
        return False

    def attach(self, view_object):
        self.ViewObject = view_object
        if getattr(view_object.Object, "DXFType", "") == "CIRCLE":
            from pivy import coin

            self._center_switch = coin.SoSwitch()
            self._center_switch.whichChild = coin.SO_SWITCH_NONE
            annotation = coin.SoAnnotation()
            pick = coin.SoPickStyle()
            pick.style = coin.SoPickStyle.UNPICKABLE
            color = coin.SoBaseColor()
            style = coin.SoDrawStyle()
            style.pointSize = 8
            self._center_coordinates = coin.SoCoordinate3()
            for node in (pick, color, style, self._center_coordinates, coin.SoPointSet()):
                annotation.addChild(node)
            self._center_color = color
            self._center_switch.addChild(annotation)
            view_object.RootNode.addChild(self._center_switch)
            _observe_circle(self)

    def _update_circle_center(self, selected):
        import FreeCAD
        from pivy import coin

        switch = getattr(self, "_center_switch", None)
        if switch is None:
            return
        switch.whichChild = coin.SO_SWITCH_NONE
        try:
            view = self.ViewObject
            obj = view.Object
            if not view.Visibility or (obj.Document.Name, obj.Name) not in selected:
                return
            if obj.Shape.isNull() or len(obj.Shape.Edges) != 1:
                return
            # The view's root already applies object Placement. Obtain the
            # center from Part's local analytic circle to avoid applying it twice.
            shape = obj.Shape.copy()
            shape.Placement = FreeCAD.Placement()
            center = shape.Edges[0].Curve.Center
            self._center_coordinates.point.setValue(center.x, center.y, center.z)
            packed = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").GetUnsigned(
                "SelectionColor", 0x00ABFFFF)
            self._center_color.rgb = tuple(((packed >> shift) & 255) / 255
                                           for shift in (24, 16, 8))
            switch.whichChild = coin.SO_SWITCH_ALL
        except (ReferenceError, RuntimeError, AttributeError):
            # Deleted/closed objects and shapes awaiting recomputation have no marker.
            return

    def onDelete(self, view_object, subelements):
        if _circle_selection_observer is not None:
            _circle_selection_observer.providers.discard(self)
        switch = getattr(self, "_center_switch", None)
        if switch is not None:
            view_object.RootNode.removeChild(switch)
            self._center_switch = None
        return True

    @staticmethod
    def _update_width_mode(view_object):
        show_widths = getattr(view_object.Object, "ShowWidths", False)
        view_object.DisplayMode = "Flat Lines" if show_widths else "Wireframe"

    def updateData(self, obj, prop):
        if prop == "ShowWidths":
            self._update_width_mode(self.ViewObject)
        if prop in ("Shape", "Placement", "DXFData") and _circle_selection_observer is not None:
            self._update_circle_center(_circle_selection_observer.selected)

    def onChanged(self, view_object, prop):
        if prop == "Visibility" and _circle_selection_observer is not None:
            self._update_circle_center(_circle_selection_observer.selected)

    def getDisplayModes(self, view_object):
        return []

    def getDefaultDisplayMode(self):
        return "Flat Lines" if getattr(self.ViewObject.Object, "ShowWidths", False) else "Wireframe"

    def setDisplayMode(self, mode):
        return mode

    def dumps(self):
        return None

    def loads(self, state):
        pass
