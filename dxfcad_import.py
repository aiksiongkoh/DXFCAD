"""DXFCAD import workflow; DXF semantics and geometry live in C++."""

import os
from pathlib import Path

import FreeCAD

from dxfcad_objects import DXFObjectProxy, DXFViewProvider


def default_text_font():
    candidates = [
        Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts/arial.ttf",
        Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
        Path("/System/Library/Fonts/Supplemental/Arial.ttf"),
    ]
    return next((str(path) for path in candidates if path.is_file()), "")


def initialize_population(population):
    """Attach compatibility lifecycle and GUI presentation to populated objects."""
    source = population.source
    for obj, entity in zip(population.entities, source.entities):
        if obj.TypeId == "Part::FeaturePython":
            DXFObjectProxy(obj)
        if FreeCAD.GuiUp:
            layer = source.layers.get(entity.layer, source.layers["0"])
            DXFViewProvider(obj.ViewObject, entity.color(layer))
            obj.ViewObject.Visibility = entity.visible
    if FreeCAD.GuiUp:
        for name, group in population.layer_groups.items():
            layer = source.layers.get(name, source.layers["0"])
            if not layer.visible:
                group.ViewObject.Visibility = False


def import_dxf(filename, document=None, font_file=None):
    import DXFCAD

    parsed = DXFCAD.DXFDocument.read(str(filename))
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
        population = DXFCAD.populate_drawing(
            document, parsed, source_file=str(Path(filename).resolve()),
            label=Path(filename).stem, name="DXFImport", font_file=font)
        features = population.entities
        parsed = population.source
        initialize_population(population)
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
    for warning in parsed.warnings:
        FreeCAD.Console.PrintWarning(warning + "\n")
    FreeCAD.Console.PrintMessage(f"DXFCAD imported {len(features)} native entities.\n")
    if parsed.insertion_units == 0:
        FreeCAD.Console.PrintWarning("DXF units are unspecified; source coordinate values were preserved.\n")
    return document


def open(filename):
    return import_dxf(filename)


def insert(filename, document_name):
    return import_dxf(filename, FreeCAD.getDocument(document_name))
