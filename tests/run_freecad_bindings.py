"""Run the binding suite inside a headless FreeCAD Python runtime.

Usage: <FreeCAD Python> run_freecad_bindings.py <native-module-directory>
The SDK used to build the module must match this interpreter's major/minor ABI.
"""

import FreeCAD

import runpy
from pathlib import Path


if __name__ == "__main__":
    print("FreeCAD:", ".".join(FreeCAD.Version()[:3]), flush=True)
    # The binding suite consumes the module-directory argument and runs all
    # native-data and error/lifetime tests after FreeCAD has been imported.
    runpy.run_path(str(Path(__file__).with_name("test_lwpolyline.py")), run_name="__main__")
