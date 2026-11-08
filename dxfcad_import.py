"""DXFCAD import workflow; DXF semantics and geometry live in C++."""

import os
from pathlib import Path
import shutil
import tempfile
from time import monotonic

import FreeCAD

from dxfcad_objects import EntityProxy, EntityViewProvider


class ImportProgress:
    """Present import phases without allowing edits to a partial document."""

    def __init__(self, filename):
        self.dialog = None
        self.phase = None
        self.started = monotonic()
        self.phase_started = self.started
        self.last_update = 0
        if FreeCAD.GuiUp:
            import FreeCADGui
            from PySide import QtCore, QtWidgets

            self.dialog = QtWidgets.QProgressDialog(FreeCADGui.getMainWindow())
            self.dialog.setWindowTitle(f"Import DXF/DWG - {Path(filename).name}")
            self.dialog.setWindowModality(QtCore.Qt.ApplicationModal)
            self.dialog.setCancelButton(None)
            self.dialog.setMinimumDuration(0)
            self.dialog.setAutoClose(False)
            self.dialog.setAutoReset(False)
            self.dialog.setRange(0, 0)

    def update(self, phase, completed=None, total=None):
        now = monotonic()
        changed = phase != self.phase
        if changed:
            if self.phase is not None:
                FreeCAD.Console.PrintMessage(
                    f"DXFCAD: {self.phase} finished in {now - self.phase_started:.1f}s.\n")
            self.phase = phase
            self.phase_started = now
            FreeCAD.Console.PrintMessage(f"DXFCAD: {phase}...\n")
        elif now - self.last_update < 0.1 and completed != total:
            return
        self.last_update = now
        if self.dialog is not None:
            from PySide import QtCore, QtWidgets

            label = phase
            if total:
                label += f" ({completed:,} / {total:,})"
            self.dialog.setLabelText(label)
            # Percentages apply only within a measured phase. Conversion,
            # parsing and recompute have no measurable total here.
            self.dialog.setRange(0, 100 if total else 0)
            if total:
                self.dialog.setValue(completed * 100 // total)
            self.dialog.show()
            QtWidgets.QApplication.processEvents(QtCore.QEventLoop.ExcludeUserInputEvents)

    def population(self, completed, total):
        self.update("Creating native entities", completed, total)

    def close(self):
        if self.dialog is not None:
            self.dialog.close()
            self.dialog.deleteLater()


def default_text_font():
    candidates = [
        Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts/arial.ttf",
        Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
        Path("/System/Library/Fonts/Supplemental/Arial.ttf"),
    ]
    return next((str(path) for path in candidates if path.is_file()), "")


def initialize_population(population, progress=None):
    """Attach compatibility lifecycle and GUI presentation to populated objects."""
    source = population.source
    # Binding properties return containers by value; fetch once, especially
    # the layer map which would otherwise be copied for every GUI entity.
    entities = population.entities
    layers = source.layers
    for index, (obj, entity) in enumerate(zip(entities, source.entities)):
        if obj.TypeId == "Part::FeaturePython":
            EntityProxy(obj)
        if FreeCAD.GuiUp:
            layer = layers.get(entity.layer, layers["0"])
            EntityViewProvider(obj.ViewObject, entity.color(layer))
            obj.ViewObject.Visibility = entity.visible
        if progress is not None:
            progress.update("Setting up display", index + 1, len(entities))
    if FreeCAD.GuiUp:
        for name, group in population.layer_groups.items():
            layer = layers.get(name, layers["0"])
            if not layer.visible:
                group.ViewObject.Visibility = False


def _read_drawing(filename, progress):
    """Delegate DWG conversion to FreeCAD; native DXF parsing stays in C++."""
    import DXFCAD

    source = Path(filename).resolve()
    if source.suffix.lower() != ".dwg":
        progress.update("Reading DXF records")
        return DXFCAD.Drawing.read(str(source))
    if not source.is_file():
        raise FileNotFoundError(f"Cannot open DWG file: {source}")
    try:
        import importDWG
    except ImportError as error:
        raise RuntimeError("DWG import requires FreeCAD's Draft DWG converter support.") from error

    # Reuse FreeCAD's configured converter, discovery and fallback policy.
    # Do not use importDWG.open(): that would create Draft objects instead.
    progress.update("Converting DWG to DXF")
    # Converters can interpret the basename as a wildcard filter. Isolate a
    # byte-identical copy with a plain name; the caller retains source as the
    # document's provenance. Never rename or convert inside the source folder.
    with tempfile.TemporaryDirectory(prefix="dxfcad-dwg-") as staging:
        staged = Path(staging) / "drawing.dwg"
        shutil.copyfile(source, staged)
        converted = importDWG.convertToDxf(str(staged))
        if not converted:
            raise RuntimeError(
                "DWG conversion failed. Configure a DWG converter under "
                "Edit > Preferences > Import/Export > DWG and check Report view.")
        converted = Path(converted).resolve()
        if converted == source or converted.suffix.lower() != ".dxf":
            raise RuntimeError("FreeCAD's DWG converter did not return a DXF file.")
        try:
            progress.update("Reading converted DXF records")
            return DXFCAD.Drawing.read(str(converted))
        finally:
            # FreeCAD normally creates a separate mkdtemp output folder. Remove
            # only its returned DXF and empty directory. The owned staging
            # directory is cleaned by its context manager on every exit path.
            directory = converted.parent
            if (directory.parent == Path(tempfile.gettempdir()).resolve()
                    and directory.name.startswith("tmp") and directory != source.parent):
                try:
                    converted.unlink(missing_ok=True)
                    directory.rmdir()
                except OSError as error:
                    FreeCAD.Console.PrintWarning(f"DWG conversion cleanup: {error}\n")


def import_dxf(filename, document=None, font_file=None):
    """Import DXF or converted DWG with the original filename as provenance."""
    progress = ImportProgress(filename)
    try:
        result = _import_dxf(filename, document, font_file, progress)
        progress.update("Import complete")
        FreeCAD.Console.PrintMessage(
            f"DXFCAD: total import time {monotonic() - progress.started:.1f}s.\n")
        return result
    finally:
        progress.close()


def _import_dxf(filename, document, font_file, progress):
    import DXFCAD

    parsed = _read_drawing(filename, progress)
    conversion_warnings = []
    if Path(filename).suffix.lower() == ".dwg":
        conversion_warnings.append(
            "DWG was converted to DXF by FreeCAD's external converter. "
            "Editing and DXF export use the converted records; DWG-only data may be lost.")
    created_document = document is None
    if created_document:
        document = FreeCAD.newDocument("DXFDrawing")
        document.Label = Path(filename).stem
    font = default_text_font() if font_file is None else str(font_file)
    # Console documents start with undo disabled. Enable it for this transaction
    # so an import failure also rolls back there, then restore the original mode.
    undo_was_disabled = document.UndoMode == 0
    if undo_was_disabled:
        document.UndoMode = 1
    document.openTransaction("Import native DXF entities")
    try:
        progress.update("Preparing drawing snapshot")
        population = DXFCAD.populate_drawing(
            document, parsed, source_file=str(Path(filename).resolve()),
            label=Path(filename).stem, name="DXFImport", font_file=font,
            progress=progress.population)
        features = population.entities
        parsed = population.source
        if conversion_warnings:
            # ImportWarnings is regenerated from the DXF by native recompute.
            # Keep conversion provenance in the persistent presentation notes.
            population.root.DisplayNotes += "; " + " ".join(conversion_warnings)
        progress.update("Setting up display", 0, len(features))
        initialize_population(population, progress)
        progress.update("Recomputing display geometry")
        document.recompute()
        failures = [f"{obj.Label}: {obj.DisplayStatus}" for obj in features
                    if obj.DisplayStatus.startswith("Error:")]
        if failures:
            raise RuntimeError("DXF display failed:\n" + "\n".join(failures[:10]))
        document.commitTransaction()
    except Exception:
        document.abortTransaction()
        if undo_was_disabled:
            document.UndoMode = 0
        if created_document:
            FreeCAD.closeDocument(document.Name)
        raise
    if undo_was_disabled:
        document.UndoMode = 0
    for warning in list(parsed.warnings) + conversion_warnings:
        FreeCAD.Console.PrintWarning(warning + "\n")
    FreeCAD.Console.PrintMessage(f"DXFCAD imported {len(features)} native entities.\n")
    if parsed.insertion_units == 0:
        FreeCAD.Console.PrintWarning("DXF units are unspecified; source coordinate values were preserved.\n")
    return document


def open(filename):
    return import_dxf(filename)


def insert(filename, document_name):
    return import_dxf(filename, FreeCAD.getDocument(document_name))
