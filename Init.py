"""Initialize DXFCAD in FreeCAD console and GUI sessions.

Domain objects are supplied by a C++ extension. Keep GUI imports out of here.
"""

import FreeCAD

FreeCAD.addImportType("DXFCAD native DXF (*.dxf)", "dxfcad_import")
FreeCAD.addExportType("DXFCAD native DXF (*.dxf)", "dxfcad_export")

# Native FreeCAD types must be registered before restoring their FCStd objects.
try:
    import DXFCAD
except ImportError as error:
    FreeCAD.Console.PrintWarning(f"DXFCAD extension unavailable: {error}\n")
