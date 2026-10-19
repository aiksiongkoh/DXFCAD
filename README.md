# DXFCAD

A standalone FreeCAD workbench for creating and manipulating native DXF entities.

DXFCAD is maintained independently of FreeCAD and XynaCAD. XynaCAD is a
specialized downstream workbench that uses DXFCAD's APIs.

## Architecture

- C++ implements domain objects and algorithms, including geometry,
  transformations, validation, and DXF import/export semantics.
- Python implements UI objects and algorithms, including workbenches, commands,
  dialogs, task panels, and interactive editing.
- Native DXF entity data is authoritative; display geometry is derived from it.
- Dependency direction: XynaCAD -> DXFCAD -> FreeCAD.

## Initial scope

LWPolyline is the first implemented C++ domain entity. It preserves vertices,
bulges, constant and per-segment widths, closed state, elevation, and extrusion
direction. Interactive Python editing is planned.

The Python workbench entry point and the initial C++ domain build are implemented.
`DXFCAD::LWPolyline` owns native OCS vertex data, bulges, constant and per-segment
widths, closed state, elevation, and extrusion direction. Construction rejects
nonfinite values, negative widths, and a zero extrusion direction, without
normalizing or deriving geometry from the stored values. Empty polylines are
allowed as an initial editing state.

The domain library also has optional native Python bindings and derives OCS and
world-coordinate centerline segments in C++. Native DXF import and FreeCAD
document/display adapters support LWPOLYLINE, LINE, ARC, CIRCLE, and basic TEXT.
LWPolyline width regions are derived in C++ and displayed as filled faces.
DXF export and interactive entity editing are not implemented yet.
No FreeCAD or XynaCAD source changes are needed.

LWPolyline also has a native FreeCAD document object, enabled with
`DXFCAD_BUILD_NATIVE_OBJECTS=ON`. Its inheritance is
`App::DocumentObject -> Part::Feature -> DXFCAD::LWPolylineObject`. The native
class owns persistent properties and performs recomputation in C++; Python
provides its view provider and UI workflow.

LINE and ARC also have native `DXFCAD::LineObject` and `DXFCAD::ArcObject`
classes. They derive from `Part::Feature` through the shared abstract
`DXFCAD::DXFEntityObject` base, which owns native properties, recomputation, and
restoration. LINE preserves world-coordinate endpoints; ARC uses the existing C++
OCS transform and counterclockwise sweep semantics. Their shapes are built directly
with OpenCASCADE, with Python used only for presentation and UI.

CIRCLE and TEXT use native `DXFCAD::CircleObject` and `DXFCAD::TextObject`
classes through the same base. CIRCLE builds a full analytic circle in its OCS
plane. TEXT has a native `TextFont` property and uses FreeCAD's C++
`Part::makeTextWires` API, then applies height, width factor, rotation, and OCS
placement in C++. Its font sizing and shaping follow that API and can differ
from the older compatibility adapter's `Part.makeWireString` outlines. This
requires a matching FreeCAD SDK exposing `Part::makeTextWires` (verified with the
27.1 development runtime). The original DXF style remains in `DXFData`; the chosen
font is a display substitute. Empty text has an empty shape; invalid data or
font failures clear the old shape and report an error.

## Build and test the C++ domain library

Requirements: CMake 3.20 or newer and a C++17 compiler. The domain library and its
tests require no external packages or FreeCAD installation.

The following commands were verified on Windows with MSVC 19.51 and Ninja.
Run them from a Visual Studio developer shell, at the repository root:

```powershell
cmake -S . -B build/ninja -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/ninja
ctest --test-dir build/ninja --output-on-failure
```

With another installed compiler, select its appropriate generator. Visual Studio
generators use `--config Release` when building and `-C Release` with CTest;
single-configuration generators use `-DCMAKE_BUILD_TYPE=Release` at configure time.
The library target is `DXFCADDomain` (alias `DXFCAD::Domain`); its public headers
are in `include/DXFCAD`. Configure with `-DBUILD_TESTING=OFF` for a library-only
build. Building the library does not install a FreeCAD extension or alter the
Python workbench.

CTest checks default state, exact native-field preservation (including signed
bulges and non-unit extrusion), ownership of vertex data, and invalid inputs.
The geometry tests cover open/closed connectivity, straight segments, clockwise
and counterclockwise minor/semicircular/major arcs, and degenerate inputs.
World-coordinate tests cover elevation, tilted and negative extrusion, the DXF
arbitrary-axis threshold, non-unit and extreme-magnitude normals, and overflow.
Checks remain active in Release builds.

## Build and test the Python bindings

Enable `DXFCAD_BUILD_PYTHON` to build the `DXFCAD` extension module. This requires
pybind11 3.0 or newer and a Python 3.9 or newer development SDK (interpreter,
headers, and libraries). Use the same Python major/minor version and architecture
as the FreeCAD installation that will load the module. The SDK and pybind11 are
external build dependencies; CMake does not download them or depend on a sibling
source checkout.

From a Visual Studio developer shell, substitute your SDK and pybind11 paths:

```powershell
cmake -S . -B build/python -G Ninja -DCMAKE_BUILD_TYPE=Release -DDXFCAD_BUILD_PYTHON=ON "-DPython_ROOT_DIR=C:/path/to/python-sdk" "-Dpybind11_DIR=C:/path/to/pybind11/share/cmake/pybind11"
cmake --build build/python
ctest --test-dir build/python --output-on-failure
```

`Python_ROOT_DIR` should identify the SDK containing the interpreter, headers,
and libraries. If multiple interpreters are installed, also set
`-DPython_EXECUTABLE=C:/path/to/python.exe` to select the intended one explicitly.
Use a fresh build directory when changing Python versions.

The binding build was verified with MSVC 19.51 and pybind11 3.0.4, using both
Python 3.14.7 and the installed FreeCAD 1.1.4's Python 3.11.14. pybind11 was supplied
by a local FreeCAD LibPack, used read-only; its location is selected explicitly
and is not a repository dependency. A Python 3.14 binary cannot be loaded by
Python 3.11; use a separate build directory for each runtime.

### Native FreeCAD document object build

Native document objects require a matching compiled FreeCAD source/build pair
and dependency SDK. These are explicit, read-only inputs; DXFCAD never adds the
FreeCAD tree as a subproject or changes its source. The current native build was
verified against the available FreeCAD 27.1 development runtime with Python
3.14.7. Its headers require C++23; the independent domain library remains C++17.

From a Visual Studio developer shell, substitute the three SDK locations:

```powershell
$freecadSource = "C:/path/to/FreeCAD"
$freecadBuild = "C:/path/to/FreeCAD/build/release"
$freecadSdk = "C:/path/to/matching/LibPack"
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release -DDXFCAD_BUILD_PYTHON=ON -DDXFCAD_BUILD_NATIVE_OBJECTS=ON "-DDXFCAD_FREECAD_SOURCE_DIR=$freecadSource" "-DDXFCAD_FREECAD_BUILD_DIR=$freecadBuild" "-DDXFCAD_FREECAD_LIBPACK_DIR=$freecadSdk" "-DCMAKE_PREFIX_PATH=$freecadSdk" "-DPython_EXECUTABLE=$freecadBuild/bin/python.exe" "-DPython_ROOT_DIR=$freecadBuild/bin" "-Dpybind11_DIR=$freecadSdk/share/cmake/pybind11" "-DDXFCAD_FREECAD_PYTHON_EXECUTABLE=$freecadBuild/bin/python.exe"
cmake --build build/native
ctest --test-dir build/native --output-on-failure
cmake --install build/native --prefix "$PWD/build/native/package/DXFCAD"
```

The packaged folder contains both `DXFCAD` and `_DXFCADObjects` extensions.
`DXFCAD` first loads FreeCAD's Part module, then the companion library registers
the native classes. This ordering also supports loading types during FCStd
restoration. `DXFCAD.NATIVE_DOCUMENT_OBJECTS` reports whether this build supports
the native classes. The native configuration has fourteen CTest entries, including
LWPolyline, LINE, ARC, CIRCLE, and TEXT inheritance, C++ recomputation,
invalid-data recovery, undo/redo, and cold restore from saved type names without
Python document proxies.

Install this package only into the matching FreeCAD runtime's user Mod folder.
The tested 27.1/Python 3.14 package is incompatible with the installed FreeCAD
1.1.4/Python 3.11 installation. That installation retains the compatibility
adapter until a matching C++ SDK is available. Native FCStd objects require the
native build; the compatibility adapter cannot restore their C++ type.

To load the staged workbench for development, open the matching FreeCAD executable
and run in its Python console (substitute the staged folder):

```python
import sys
sys.path.insert(0, r"C:\path\to\DXFCAD\build\native\package\DXFCAD")
import DXFCAD
assert DXFCAD.NATIVE_DOCUMENT_OBJECTS
from dxfcad_workbench import DXFCADWorkbench
Gui.addWorkbench(DXFCADWorkbench())
Gui.activateWorkbench("DXFCADWorkbench")
```

This staged loading method is for development previews. To reopen saved documents
with Python view providers, install the complete package in the matching runtime's
user `Mod/DXFCAD` directory and restart FreeCAD. FreeCAD restricts restoration of
Python proxies to modules supplied by FreeCAD or installed addons.

### Installed FreeCAD on Windows: compatibility build

The installed FreeCAD includes an interpreter but not Python development files.
For the verified Python 3.11 build, headers and the import library came from the
official [Python 3.11.9 NuGet package](https://www.nuget.org/packages/python/3.11.9).
These work with the installed Python 3.11.14 runtime. Download and extract the SDK
under the ignored build directory (no system Python installation is needed):

```powershell
New-Item -ItemType Directory -Force build/dependencies | Out-Null
Invoke-WebRequest https://www.nuget.org/api/v2/package/python/3.11.9 -OutFile build/dependencies/python.3.11.9.zip
Expand-Archive build/dependencies/python.3.11.9.zip build/dependencies/python311
$pythonSdk = (Resolve-Path build/dependencies/python311/tools).Path
cmake -S . -B build/python311 -G Ninja -DCMAKE_BUILD_TYPE=Release -DDXFCAD_BUILD_PYTHON=ON "-DPython_EXECUTABLE=C:/Program Files/FreeCAD 1.1/bin/python.exe" "-DPython_INCLUDE_DIR=$pythonSdk/include" "-DPython_LIBRARY=$pythonSdk/libs/python311.lib" "-Dpybind11_DIR=C:/path/to/pybind11/share/cmake/pybind11" "-DDXFCAD_FREECAD_PYTHON_EXECUTABLE=C:/Program Files/FreeCAD 1.1/bin/python.exe"
cmake --build build/python311
ctest --test-dir build/python311 --output-on-failure
cmake --install build/python311 --prefix "$PWD/build/stage/DXFCAD"
```

Substitute the installed pybind11 configuration directory. Setting
`DXFCAD_FREECAD_PYTHON_EXECUTABLE` adds FreeCAD CTest entries, for nine entries in
total. One imports FreeCAD before running the twelve binding tests, including
comparisons of OCS and world-coordinate geometry with FreeCAD Placement and Part.
The two FreeCAD comparisons are skipped when running without FreeCAD. The same suite was
also verified against the staged package:

```powershell
& "C:/Program Files/FreeCAD 1.1/bin/python.exe" tests/run_freecad_bindings.py build/stage/DXFCAD
```

The import integration tests also verify valid shapes for the five supported
entity types, width/centerline toggling, wide and tapered major arcs, legacy-object
migration, transaction rollback, and save/reopen/recompute from saved native
records after the synthetic source file has been removed. Customer drawings are
not test fixtures and must remain outside version control.

Using the interpreter selected for the build, add `build/python` to `sys.path`
(or the configuration subdirectory for a multi-configuration generator):

```python
import sys
sys.path.insert(0, "C:/path/to/DXFCAD/build/python")
import DXFCAD

polyline = DXFCAD.LWPolyline(
    vertices=[
        DXFCAD.LWPolylineVertex(x=0, y=0, bulge=0.5),
        DXFCAD.LWPolylineVertex(x=10, y=0, start_width=1, end_width=2),
    ],
    closed=False,
    constant_width=0,
    elevation=3,
    extrusion=DXFCAD.ExtrusionDirection(0, 0, 1),
)
print(polyline.vertices[0].bulge)
```

Properties are read-only. `vertices` returns a list of copied vertex values;
`extrusion` returns a copied direction. Changing the list cannot alter native
state, and snapshots remain valid after the polyline is released. Vertex and
direction values are validated by the C++ LWPolyline constructor when assembled
into an entity. Domain validation failures become Python `ValueError`; invalid
argument types become `TypeError`. No geometry or validation is implemented in
Python. The binding tests cover these behaviors through the compiled module.

## Derived OCS centerlines

`polyline.segments()` returns ordered `LWPolylineSegment` values with `start` and
`end` points. Straight segments have `arc is None`. Curved segments have an
`LWPolylineArc` with `center`, `radius`, signed `sweep` in radians, and a `midpoint`
halfway along the directed arc. Positive sweep is counterclockwise in OCS. Bulges
greater than one in magnitude produce major arcs; small nonzero bulges are not
silently flattened.

An open entity has one fewer segment than vertices; its final bulge is unused.
A closed entity also uses its final vertex's bulge for the closing segment.
Entities with fewer than two vertices return no segments. Coincident straight
endpoints are retained; coincident bulged endpoints raise `ValueError` because
they do not define a unique arc. Numerically unrepresentable arcs raise
`OverflowError`. Geometry derivation never changes native data, even on failure.
Returned geometry properties are read-only and remain valid independently of the
polyline's lifetime.

These are two-dimensional OCS centerlines. Elevation, extrusion transforms, and
width outlines are not applied by this API.

## Derived world-coordinate centerlines

`polyline.world_segments()` (C++ `worldSegments()`) applies elevation and extrusion
to the OCS centerlines and returns `LWPolylineSegment3D` snapshots. Each has 3D
`start` and `end` vectors. An arc also has 3D `center`, `midpoint`, and unit `normal`,
plus the original radius and signed sweep. Sweep is measured about that normal;
a negative sweep does not reverse the returned normal.

The frame follows DXF's arbitrary-axis algorithm. After normalizing a derived
copy of the extrusion, it chooses world Y as the reference when both absolute
X and Y components are strictly below 1/64; otherwise it chooses world Z.
Cross products give a right-handed OCS frame. Elevation is distance along its
normal, so a non-unit native extrusion does not scale the entity.

For default extrusion `(0, 0, 1)`, vertex `(10, 20)` at elevation `5` becomes
`(10, 20, 5)`. With extrusion `(0, 2, 0)`, the same vertex becomes `(-10, 5, 20)`.
Stored vertices, elevation, widths, and extrusion are never modified.

Topology and OCS geometry errors follow `segments()`. A nonfinite transformed
coordinate raises `OverflowError`. Results are read-only and remain valid after
the polyline is released. This API derives centerlines; use `width_regions()` for
the width geometry.

## LWPolyline width display

Imported LWPolylines have a **Show Widths** property, enabled by default. It shows
filled regions using their native constant or start/end widths. Disable it to
display the original centerline. Existing DXFCAD objects acquire this property
when restored or recomputed with the updated module. The setting is saved in the
FreeCAD document; it never changes `DXFData`.

`polyline.width_regions(tolerance=0.0001)` derives world-coordinate polygons in
C++. Each `LWPolylineWidthRegion` identifies its `segment_index` and ordered
`points`. Straight tapered segments are exact trapezoids. Curved boundaries are
sampled with a chord-error bound of 0.0001 source units by default. Tolerance must
be finite and positive; requests requiring over 65,536 subdivisions per arc raise
`OverflowError`. Nearly straight bulges use a numerically stable construction.

Explicit variable-width codes 40/41 override constant width code 43, including
explicit zero widths. `LWPolylineVertex.has_widths` preserves that presence when
constructing native data; nonzero start/end widths also select variable width.
Widths interpolate linearly along each segment. Wide arcs that cross their own
centers are split into simple radial regions; FreeCAD's Part kernel unites
overlapping faces. Zero-width segments retain their centerlines, and zero-length
straight segments generate no width area.

This increment uses butt caps on each segment and unites overlapping regions;
it does not add miter or round joins at vertices. Kernel refinement may retain
internal face seams when removing them would invalidate a coplanar union.
Single-segment LWPolylines are unaffected by vertex-join limitations. Native
widths and other DXF group codes remain authoritative.

## Import and display a drawing

After installing the native build, select **DXFCAD > Import Native DXF...**.
Choose a drawing to create a new FreeCAD document, grouped by DXF layer. Source
layer off/frozen state, entity visibility, and ACI/true colors are used for
presentation; white strokes use FreeCAD's foreground color for contrast. Toggle
layer groups in the tree with Space to inspect hidden layers. The same importer
is registered as **DXFCAD native DXF** in FreeCAD's file dialog.

Selecting a CIRCLE or its edge also highlights its center with a screen-sized
point in the selection color. The marker disappears on deselection or hiding the
circle. It is a transient Python view-provider overlay; native DXF data and the
Part shape are unchanged. This works with native and compatibility objects,
including reopened FCStd documents. `tests/test_circle_selection_gui.py` exercises
the behavior in a GUI runtime with DXFCAD installed in an isolated test profile.

Run GUI tests through the FreeCAD executable, rather than hosting its GUI inside
`python.exe`. The runner checks both test assertions and the application's exit
code, uses an isolated profile inside this workspace, and logs failures instead
of opening a Windows crash dialog for automated child processes:

```powershell
cmake --install build/native --prefix "$PWD/build/native/selection-profile/Mod/DXFCAD"
python tests/run_gui_test.py --freecad "C:/path/to/matching/FreeCAD.exe" --profile build/native/selection-profile
```

Use `build/python311` and the installed FreeCAD 1.1.4 executable to test the
compatibility package. Do not point the runner at your normal user profile.

With the native build, imported LWPolylines, lines, arcs, circles, and texts are
`DXFCAD::LWPolylineObject`, `DXFCAD::LineObject`, `DXFCAD::ArcObject`,
`DXFCAD::CircleObject`, and `DXFCAD::TextObject`
instances derived from `Part::Feature`. Their `DXFData` and metadata
are native C++ FreeCAD properties. Their C++ `execute()` reconstructs the existing
domain model and builds the derived `Shape` directly with Part/OpenCASCADE.
There is no application `Proxy` property or Python recompute callback. LWPolyline
also has a native `ShowWidths` property.

All entities in a compatibility build use a
`Part::FeaturePython` adapter backed by the same C++ domain model. Its Python
lifecycle proxy delegates geometry work to the extension, which calls FreeCAD's
Part Python APIs. This mode requires no FreeCAD C++ SDK. Existing proxy-backed
FCStd files remain readable; they are not automatically replaced with native
objects. Reimporting a drawing with the native build creates native LWPolyline,
LINE, ARC, CIRCLE, and TEXT
objects while preserving the existing documents.

In both modes, hidden `DXFData` stores the authoritative native entity group
codes, including widths, bulges, unknown fields, and XDATA. Read-only `DXFType`,
`DXFHandle`, and `DXFLayer` identify the entity. Python provides view providers,
grouping, dialogs, and import transactions.

Saving as `.FCStd` preserves native records and derived shapes. Recomputing after
reopening uses saved records; it does not reread the original DXF. Keep DXFCAD
installed to restore the adapters and recompute. `SourceFile` is provenance only.
`SourceUnitCode` records `$INSUNITS`; numeric coordinates are retained without
automatic unit conversion, including when units are unspecified.

The current importer supports ASCII/UTF-8 DXF model-space LWPOLYLINE, LINE, ARC,
CIRCLE, and simple left/baseline TEXT. Unsupported entities and text alignment,
oblique, or mirrored styles are reported in the import group's `ImportWarnings`
and Report view. BLOCK/INSERT expansion, paper space, binary DXF, legacy text
codepage conversion, DXF text formatting escapes, line-type patterns, thickness,
are not implemented. LWPolylines display width regions or centerlines using
**Show Widths**; widths remain authoritative in the stored native record. Zero-length straight segments
do not create Part edges. TEXT uses outline wires in a substitute TrueType font
(Arial on Windows), rather than reproducing the DXF font style. The `TextFont`
property selects the display font and can be changed before recomputing.

For scripting in FreeCAD:

```python
from dxfcad_import import import_dxf
doc = import_dxf(r"C:\path\to\drawing.dxf")
Gui.activeDocument().activeView().viewTop()
Gui.activeDocument().activeView().fitAll()
```

## Install and check the workbench

1. In FreeCAD's Python console, run `print(App.getUserAppDataDir())`.
2. Close FreeCAD. For a native build, copy the contents of the staged
   `build/stage/DXFCAD` folder into that directory's `Mod/DXFCAD` folder.
   `Init.py`, `InitGui.py`, and the compiled extension must be directly inside
   `Mod/DXFCAD`. Copying the repository alone installs only the Python UI skeleton.
3. Restart FreeCAD and select **DXFCAD** in the workbench selector.
4. Open **View > Panels > Report view** and select
   **DXFCAD > Check DXFCAD Workbench**.

The Report view should show `DXFCAD workbench loaded successfully.`
The command works without an open document and does not change geometry.
XynaCAD is not required. This check verifies UI loading only.

Python entry points must be UTF-8 without a byte-order mark. FreeCAD compiles
their decoded text, which rejects a leading U+FEFF even though ordinary Python
imports may accept it. `DXFCAD.WorkbenchStartup` guards this startup path.
