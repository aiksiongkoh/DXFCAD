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
        root = document.addObject("App::DocumentObjectGroup", "DXFImport")
        root.Label = Path(filename).stem
        root.addProperty("App::PropertyFile", "SourceFile", "DXF")
        root.SourceFile = str(Path(filename).resolve())
        root.addProperty("App::PropertyInteger", "SourceUnitCode", "DXF")
        root.SourceUnitCode = parsed.insertion_units
        root.addProperty("App::PropertyStringList", "ImportWarnings", "DXF")
        root.ImportWarnings = list(parsed.warnings)
        root.addProperty("App::PropertyString", "DisplayNotes", "DXF")
        root.DisplayNotes = "Source coordinates; widths where present; substitute font for TEXT"
        for prop in ["SourceFile", "SourceUnitCode", "ImportWarnings", "DisplayNotes"]:
            root.setEditorMode(prop, 1)
        layers = {}
        features = []
        for entity in parsed.entities:
            if entity.layer not in layers:
                group = document.addObject("App::DocumentObjectGroup", "DXFLayer")
                group.Label = entity.layer
                root.addObject(group)
                layers[entity.layer] = group
            obj = DXFCAD.create_feature(document, entity, font)
            if obj.TypeId == "Part::FeaturePython":
                DXFObjectProxy(obj)
            layers[entity.layer].addObject(obj)
            features.append(obj)
            if FreeCAD.GuiUp:
                layer = parsed.layers.get(entity.layer, parsed.layers["0"])
                DXFViewProvider(obj.ViewObject, entity.color(layer))
                obj.ViewObject.Visibility = entity.visible
        document.recompute()
        failures = [f"{obj.Label}: {obj.DisplayStatus}" for obj in features
                    if obj.DisplayStatus.startswith("Error:")]
        if failures:
            raise RuntimeError("DXF display failed:\n" + "\n".join(failures[:10]))
        if FreeCAD.GuiUp:
            for name, group in layers.items():
                layer = parsed.layers.get(name, parsed.layers["0"])
                if not layer.visible:
                    group.ViewObject.Visibility = False
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
