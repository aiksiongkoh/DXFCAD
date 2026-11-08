"""Read-only paper-layout viewer; C++ supplies projected and clipped paths."""

import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets


class PaperLayoutDialog(QtWidgets.QDialog):
    def __init__(self, root):
        super().__init__(FreeCADGui.getMainWindow())
        import DXFCAD
        from dxfcad_import import default_text_font

        self.drawing = DXFCAD.current_drawing(root)
        self.font = default_text_font()
        self.setWindowTitle("Paper Layouts - " + root.Label)
        self.resize(1000, 720)
        layout = QtWidgets.QVBoxLayout(self)
        controls = QtWidgets.QHBoxLayout()
        controls.addWidget(QtWidgets.QLabel("Layout:"))
        self.sheets = QtWidgets.QComboBox()
        self.sheets.addItems([sheet.name for sheet in self.drawing.layouts])
        controls.addWidget(self.sheets, 1)
        fit = QtWidgets.QPushButton("Fit Sheet")
        fit.clicked.connect(self.fit_sheet)
        controls.addWidget(fit)
        layout.addLayout(controls)
        note = QtWidgets.QLabel(
            "Read-only preview in native paper coordinates. Rectangular top-view viewports are supported. "
            "Dashed lines mark viewport guides. Curves use a 0.02 paper-unit display tolerance. "
            "Plot styles and printer settings are not simulated.")
        note.setWordWrap(True)
        layout.addWidget(note)
        self.scene = QtWidgets.QGraphicsScene(self)
        self.view = QtWidgets.QGraphicsView(self.scene)
        self.view.setRenderHint(QtGui.QPainter.Antialiasing)
        self.view.setDragMode(QtWidgets.QGraphicsView.ScrollHandDrag)
        self.view.setBackgroundBrush(QtGui.QColor(190, 190, 190))
        layout.addWidget(self.view, 1)
        self.message = QtWidgets.QLabel()
        self.message.setWordWrap(True)
        layout.addWidget(self.message)
        self.details = QtWidgets.QPlainTextEdit()
        self.details.setReadOnly(True)
        self.details.setMaximumHeight(100)
        layout.addWidget(self.details)
        close = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Close)
        close.rejected.connect(self.reject)
        layout.addWidget(close)
        self.sheets.currentIndexChanged.connect(self.render_sheet)
        self.preview = None
        QtCore.QTimer.singleShot(0, self.render_sheet)

    def render_sheet(self, *_):
        import DXFCAD

        self.scene.clear()
        self.preview = None
        self.details.clear()
        if self.sheets.currentIndex() < 0:
            self.message.setText("This drawing has no paper layouts.")
            self.details.hide()
            return
        QtWidgets.QApplication.setOverrideCursor(QtCore.Qt.WaitCursor)
        try:
            self.message.setText("Rendering layout...")
            QtWidgets.QApplication.processEvents(QtCore.QEventLoop.ExcludeUserInputEvents)
            self.preview = DXFCAD.preview_layout(self.drawing, self.sheets.currentText(), self.font)
            for path in self.preview.paths:
                painter = QtGui.QPainterPath()
                painter.setFillRule(QtCore.Qt.OddEvenFill)
                for contour in path.contours:
                    if not contour:
                        continue
                    painter.moveTo(contour[0].x, -contour[0].y)
                    for point in contour[1:]:
                        painter.lineTo(point.x, -point.y)
                    if path.filled:
                        painter.closeSubpath()
                color = QtGui.QColor.fromRgbF(*path.color[:3])
                # ACI white/black follows the paper background.
                if all(component > 0.95 for component in path.color[:3]):
                    color = QtGui.QColor(0, 0, 0)
                pen = QtGui.QPen(color)
                pen.setCosmetic(True)
                self.scene.addPath(painter, QtGui.QPen(QtCore.Qt.NoPen) if path.filled else pen,
                                   QtGui.QBrush(color) if path.filled else QtGui.QBrush(QtCore.Qt.NoBrush))
            border = QtGui.QPen(QtGui.QColor(130, 130, 130))
            border.setCosmetic(True)
            border.setStyle(QtCore.Qt.DashLine)
            for bounds in self.preview.viewports:
                self.scene.addRect(bounds.min_x, -bounds.max_y,
                                   bounds.max_x - bounds.min_x, bounds.max_y - bounds.min_y, border)
            bounds = self.preview.limits
            if bounds is not None:
                paper = QtCore.QRectF(bounds.min_x, -bounds.max_y,
                                      bounds.max_x - bounds.min_x, bounds.max_y - bounds.min_y)
            else:
                paper = self.scene.itemsBoundingRect()
                if paper.isEmpty():
                    paper = QtCore.QRectF(0, -100, 100, 100)
                margin = max(paper.width(), paper.height()) * 0.05
                paper = paper.adjusted(-margin, -margin, margin, margin)
            background = self.scene.addRect(paper, QtGui.QPen(QtCore.Qt.NoPen), QtGui.QBrush(QtCore.Qt.white))
            background.setZValue(-1)
            self.scene.setSceneRect(self.scene.itemsBoundingRect())
            self.details.setPlainText("\n".join(self.preview.warnings))
            self.details.setVisible(bool(self.preview.warnings))
            self.message.setText(
                f"{len(self.preview.viewports)} viewport(s) rendered. "
                + (f"{len(self.preview.warnings)} notice(s); see details below." if self.preview.warnings else ""))
            self.fit_sheet()
            QtCore.QTimer.singleShot(0, self.fit_sheet)
        except Exception as error:
            self.scene.clear()
            self.message.setText(f"Cannot preview this layout: {error}")
        finally:
            QtWidgets.QApplication.restoreOverrideCursor()

    def fit_sheet(self):
        self.view.fitInView(self.scene.sceneRect(), QtCore.Qt.KeepAspectRatio)

    def showEvent(self, event):
        super().showEvent(event)
        self.fit_sheet()


def show_paper_layouts():
    from dxfcad_layout_ui import selected_drawing

    dialog = PaperLayoutDialog(selected_drawing())
    dialog.exec()
