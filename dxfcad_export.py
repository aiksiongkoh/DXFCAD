"""Export a saved drawing snapshot with current native entity records."""

import os
from pathlib import Path
import tempfile

from dxfcad_edit import drawing_roots


def export_dxf(filename, document, root=None):
    import DXFCAD

    if root is None:
        roots = drawing_roots(document)
        if len(roots) != 1:
            raise ValueError("Select one DXFCAD drawing to export.")
        root = roots[0]
    if root.Document != document or "DXFSourceData" not in root.PropertiesList:
        raise ValueError("Select a DXFCAD drawing in this document.")
    data = DXFCAD.current_drawing_bytes(root)
    # Write next to the destination and replace only after successful validation
    # and writing. Existing drawings survive validation and I/O failures.
    path = Path(filename).resolve()
    descriptor, temporary = tempfile.mkstemp(prefix=".dxfcad-", suffix=".dxf", dir=path.parent)
    os.close(descriptor)
    try:
        Path(temporary).write_bytes(data)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    return path


def export(objects, filename):
    """FreeCAD's File > Export entry point exports one whole DXF drawing."""
    from dxfcad_edit import drawing_root

    roots = {drawing_root(obj) for obj in objects}
    roots.discard(None)
    if len(roots) != 1:
        raise ValueError("Select one DXFCAD drawing or its entities for DXF export.")
    root = roots.pop()
    return export_dxf(filename, root.Document, root)
