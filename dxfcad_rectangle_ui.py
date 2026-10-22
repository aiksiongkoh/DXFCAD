"""Two-corner native rectangular LWPolylines."""
from pivy import coin
from dxfcad_line_ui import LineTaskPanel, LineViewportTool, show_line_tool
from dxfcad_edit import create_rectangle, drawing_root, drawing_roots


class RectangleViewportTool(LineViewportTool):
    idle_message = "Click the first corner. Escape while idle stops picking."
    pending_message = "Click the opposite corner. Escape/right-click cancels the preview."
    reset_message = "Preview canceled. Click the first corner of another rectangle."

    def next_start(self, point):
        return None

    def validate(self, start, end):
        import DXFCAD
        DXFCAD.create_rectangle(start,end)

    def render(self, end=None):
        super().render()
        if end is None or self.overlay is None:
            return
        import DXFCAD
        raw = DXFCAD.create_rectangle(self.start_point,end)
        source = coin.SoInput()
        source.setBuffer(DXFCAD.entity_preview_shape(raw).writeInventor(1))
        color = coin.SoBaseColor()
        color.rgb = (1.0,0.45,0.05)
        color.setOverride(True)
        self.overlay.addChild(color)
        self.overlay.addChild(coin.SoDB.readAll(source))


class RectangleTaskPanel(LineTaskPanel):
    viewport_class = RectangleViewportTool
    title = "Draw Rectangles"
    note = "Pick two opposite corners, or enter their XY coordinates and elevation. Rectangles use layer 0 and drawing units. Each rectangle is a closed native LWPolyline and is undoable. Escape/right-click cancels the preview; Close exits."
    numeric_label = "Create rectangle from corners"
    success_message = "Rectangle created. Use Undo to remove it."

    def __init__(self, document, root=None, owns_document=False):
        super().__init__(document,root,owns_document)
        self.chain.setChecked(False)
        self.chain.hide()
        self.end_fields[1].setText("10")

    def commit(self, first, opposite):
        self.require_document()
        root = self.document.getObject(self.root_name) if self.root_name else None
        if self.root_name and root is None and drawing_roots(self.document):
            raise ValueError("The selected drawing changed. Close and reopen Rectangle.")
        obj = create_rectangle(self.document,first,opposite,root)
        self.root = drawing_root(obj)
        self.root_name = self.root.Name
        self.set_end(opposite)
        return obj


def show_rectangle_tool(root=None):
    return show_line_tool(root,panel_type=RectangleTaskPanel)
