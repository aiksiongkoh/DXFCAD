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

## Current scope

LWPolyline is the first implemented C++ domain entity. It preserves vertices,
bulges, constant and per-segment widths, closed state, elevation, and extrusion
direction. Python task panels provide vertex editing and native transforms.

The workbench includes a standalone C++ domain library and Python UI entry points.
`DXFCAD::LWPolyline` owns native OCS vertex data, bulges, constant and per-segment
widths, closed state, elevation, and extrusion direction. Construction rejects
nonfinite values, negative widths, and a zero extrusion direction, without
normalizing or deriving geometry from the stored values. Empty polylines are
allowed as an initial editing state.

The domain library also has optional native Python bindings and derives OCS and
world-coordinate centerline segments in C++. Native DXF import and FreeCAD
document/display adapters support LWPOLYLINE, LINE, ARC, CIRCLE, basic TEXT, and
plain MTEXT.
LWPolyline width regions are derived in C++ and displayed as filled faces.
Everyday drafting includes LINE/CIRCLE/rectangle creation, LWPolyline editing,
Move/Copy/Rotate/Mirror/Scale, LINE Trim/Extend, primitive/straight-polyline Offset,
polyline-corner Fillet/Chamfer, plain text creation/editing, layer/property editing,
and DXF export. Draw, Modify, and Drawing toolbars expose the frequent actions;
all commands remain available in the DXFCAD menu.
No FreeCAD or XynaCAD source changes are needed.

LWPolyline also has a native FreeCAD document object, enabled with
`DXFCAD_BUILD_NATIVE_OBJECTS=ON`. Its inheritance is
`App::DocumentObject -> Part::Feature -> DXFCAD::DXFEntityObject -> DXFCAD::LWPolylineObject`.
The shared base owns persistent DXF properties, recomputation, error handling,
and restoration in C++. LWPolyline specializes width/centerline rendering and
its `ShowWidths` property; Python provides its view provider and UI workflow.
The persisted type name remains `DXFCAD::LWPolylineObject`.

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
placement in C++. Standard left/center/right and baseline/bottom/middle/top
justification use the DXF alignment point and the substitute font's outline
bounds. This is approximate font layout: advances, whitespace, and original
style metrics are not reproduced. Its font sizing and shaping follow that API and can differ
from the older compatibility adapter's `Part.makeWireString` outlines. This
requires a matching FreeCAD SDK exposing `Part::makeTextWires` (verified with the
27.1 development runtime). The original DXF style remains in `DXFData`; the chosen
font is a display substitute. Empty text has an empty shape; invalid data or
font failures clear the old shape and report an error.

MTEXT uses `DXFCAD::MTextObject`, derived from `DXFCAD::TextObject`, with the same
native font property and outline renderer. C++ preserves all text chunks and
derives paragraph breaks (`\P`), escaped backslashes/braces, nonbreaking spaces,
width-based wrapping, line spacing, and the nine attachment positions. MTEXT
insertion and direction vectors use WCS; extrusion and rotation determine the
text plane. Layout uses measured substitute-font outline bounds, so spacing,
whitespace, wrapping, and attachment can differ from the original text style.
Style-inherited flow uses horizontal substitute-font layout. Rich inline
formatting, paragraph formatting, columns, background fills, and vertical flow
are reported as unsupported; their original records remain available for export.

Linear/rotated DIMENSION entities can display their saved picture block using
native `DXFCAD::DimensionObject`, derived from `TextObject`. C++ resolves and
persists the referenced block in hidden `DXFContext`; `DXFData` retains the
dimension record. This displays stored lines/arcs/circles, plain text, and filled
triangular SOLID arrowheads. Definition POINT markers are omitted. Children must
use BYBLOCK color, and the picture must have a zero block base with no dimension
insertion/extrusion transform. Nested INSERTs, other dimension types, complex
text, and unsupported picture contents remain warnings. Text uses a substitute
font; original picture records survive export unchanged. Dimensions are display
only: changing their native record is rejected at export because picture
regeneration and associative dimension editing are not implemented.

**DXFCAD > Saved DXF Views...** lists retained paper-space VIEWPORT records and
opens supported saved views of the model. C++ resolves layout names through
BLOCK_RECORD/LAYOUT ownership, validates view metadata, and derives camera center,
twist, and height. The first version supports enabled, unclipped top views.
C++ resolves viewport-specific frozen-layer handles against the layer table;
missing or ambiguous references keep the view unavailable. Layer-name matching
ignores ASCII letter case. Paper-space overview viewports, perspective,
tilted, and clipped views remain listed with an availability explanation.
This is model navigation: paper layouts are not rendered, and horizontal framing follows the FreeCAD window's
aspect ratio. The vertical model extent matches the saved view height. Navigation
does not change entity coordinates or source viewport records. Frozen layers are
previewed only on objects belonging to the selected drawing. Switching views or
closing the dialog restores visibility changes made by the preview; previously
hidden objects, globally hidden layers, and other drawings retain their visibility.
The camera remains at the chosen view after closing. Saved views are
available after FCStd restoration from the persisted drawing snapshot.

## Native drawing container

Native builds create `DXFCAD::DXFDocumentObject`, derived from
`App::DocumentObjectGroup`, as the drawing root. It declares persistent
`DXFSourceData`, `SourceFile`, `SourceUnitCode`, `ImportWarnings`, and
`DisplayNotes` properties in C++. Standard FreeCAD group membership retains
layer groups and their entity objects. Entities retain `DXFSourceRoot` links
and stable `DXFSourceIndex` slots for export.

`DXFCAD.create_drawing(document, drawing, source_file="", label="DXF Drawing",
name="DXFDrawing")` creates and initializes the container from an independent
C++ `DXFDocument`. `DXFCAD.drawing_document(root)` reconstructs the retained
source snapshot. `DXFCAD.set_drawing_source(root, drawing)` updates that snapshot
and its units/warnings while preserving source-file provenance. These APIs
handle existing generic drawing groups too. Import, new drawing creation,
layer editing, saved-view navigation and export use this shared C++ interface;
Python manages UI workflows and presentation.

The snapshot uses the existing Base64 property format. It retains original
source records; current entity edits remain in each child's `DXFData` and are
overlaid during export. No cached C++ model or source file is needed to restore
the drawing. Native recomputation validates the snapshot and refreshes its
unit code and warnings. Invalid snapshot data marks the root as invalid until
corrected. FreeCAD saves the native type, properties and group links in FCStd
and autoloads the registered DXFCAD types on restore.

Compatibility builds use a generic `App::DocumentObjectGroup` through the same
factory. Existing saved generic roots remain readable and editable without
automatic type conversion. `DXFCAD.FreeCADDocumentObject` covers both adapters,
group links, metadata undo/redo, failed-import rollback, legacy groups, native
snapshot validation, and cold restore/export after deleting the source DXF.

Native `DXFEntityObject` declares `DXFSourceRoot` and `DXFSourceIndex` in C++.
`DXFCAD.attach_entity(entity, root, source_index=-1)` validates ownership before
assigning them: entities must share the drawing's document, source slots must
exist and be unique, and an entity cannot silently switch to another drawing.
Legacy and compatibility objects receive the same properties through this API.
FreeCAD group membership continues to organize entities under layer groups.

`DXFCAD.current_drawing(root)` returns a C++ `DXFDocument` assembled from the
retained snapshot and current owned entities. `DXFCAD.current_drawing_bytes(root)`
returns its validated export bytes. The C++ domain assembler replaces original
slots, omits deleted entities and appends new entities (`source_index=-1`), while
retaining unsupported source records. It rejects duplicate or out-of-range slots;
the FreeCAD adapter also rejects nonidentity entity placements. These operations
do not mutate the retained snapshot or assign source slots to new entities.
Python export selects the drawing and writes these bytes through a temporary
file before replacing the destination.

Layer editing also uses the C++ FreeCAD adapter:

- `DXFCAD.ensure_layer(root, name, color=7, visible=True)` returns the persistent
  layer group, creating a missing source layer while preserving existing layer
  properties.
- `DXFCAD.source_layer_group(root, name)` creates a group for a source layer
  without changing DXF records, including imported entities whose layer has no
  table entry.
- `DXFCAD.set_layer_properties(root, name, color, visible)` explicitly updates
  the source layer's ACI color and on/off state and ensures its group exists.
- `DXFCAD.assign_layer(entities, name)` updates native records and group
  membership while retaining each entity's color and visibility overrides.
- `prepare_entity_properties(...)` validates a complete property change and
  returns preview records without mutation; `apply_entity_properties(...)`
  applies the same rules to source metadata, native records and membership.

Mutating APIs should run inside a FreeCAD document transaction, followed by
recomputation. Python owns the transaction workflow, view colors, group/entity
visibility and previews. Import and the property task panel use these shared
APIs in both native and compatibility builds. Layer groups store `DXFLayerName`
independently of their display labels; legacy groups are recognized from
consistent native child records. Ambiguous duplicate/mixed legacy groups are
rejected before applying the change. `DXFCAD.FreeCADLayers` exercises the layer
APIs, undo/redo, validation, legacy groups and preservation of opaque layer data.

## Single-entity creation

`DXFCAD.add_entity(root, raw, font_file="")` validates and adds a new model-space
entity to an explicit drawing. `DXFCAD.create_entity(document, raw, root=None,
font_file="")` additionally selects the document's sole DXF drawing, or creates
an empty millimeter drawing when none exists. Multiple drawings require an
explicit root. Both APIs return the persistent entity object.

C++ resolves the record in its target drawing (including INSERT block context),
validates geometry/version rules and the proposed current drawing, ensures the
native layer definition/group, creates the correct entity type and assigns its
ownership link and source slot `-1`. New records must have no handle/owner and
must be supported model-space additions under the export contract. Missing
layers are validated and created without discarding opaque source records or
source-file provenance. Existing layer properties are preserved.

Call inside a document transaction. Python initializes compatibility proxies
and presentation, then recomputes and checks display errors before committing.
Drawing tools use this adapter automatically; creation or display failures roll
back the entire operation. Population, contour replacement, batch copies and
corner connectors share the C++ typed-object/ownership/group construction
helper, while retaining their own validation and original/new identity rules.
`DXFCAD.FreeCADEntityCreation` covers typed objects, target selection, layers,
legacy groups, INSERT context, validation, rollback and undo/redo.

## Entity updates

`DXFCAD.prepare_entity_update(entities, records)` returns an `EntityUpdatePlan`
without changing objects. Supply one replacement record per distinct object in
one document. The plan validates geometry, drawing context and each complete
proposed drawing, preserving source slots and opaque source records. Replacements
retain entity type, handle/owner, model-space identity, layer and display
properties; use the layer/property API for those changes.

The plan exposes `document`, `objects`, `preview_records` and `drawing_bytes`
(keyed by drawing-root name). Call `apply()` once inside a transaction, then
recompute and check display errors before committing. It rejects changes to
selected records, context, font, ownership, source indices, placement or drawing
snapshots before mutation. Python's `apply_records()` and
`apply_entity_update()` provide the transaction/display workflow; text editing,
trim/extend, polyline vertex edits and replacement offsets use this path.
Offset copies use the related-copy plan described below.

`DXFCAD.FreeCADEntityUpdate` checks preview/export consistency, native identity,
opaque data, stale plans, validation, rollback and undo/redo.

`DXFCAD.prepare_polyline_edit(entity, polyline, origins=[])` builds an
`EntityUpdatePlan` using the existing C++ native-record patcher. Original vertex
IDs follow the supplied provenance indices; `-1` denotes an inserted vertex.
The patcher preserves entity identity, non-geometric flags and XDATA.

Interactive editors use `DXFCAD.begin_polyline_edit(entity)` to capture a
`PolylineEditSession` when opening. Its `original_data` and `polyline` properties
provide the initial state, and `prepare(polyline, origins=[])` rejects stale
object/drawing state before building the update plan. Python manages the vertex
table, drag previews and the transaction/display workflow. The polyline lifecycle
and GUI tests cover provenance, stale sessions, rollback and undo/save/export.

## Text edit sessions

`DXFCAD.begin_text_edit(entity)` captures a `TextEditSession` for native TEXT or
MTEXT without changing the document. It exposes `object`, `original_data` and
`text_info` (content, height, MTEXT reference width). Its
`prepare(content, height, width=0)` returns a validated `EntityUpdatePlan` using
the existing C++ text encoder/patcher. `prepare_text_edit(entity, content, height,
width=0)` is the convenience API for current-state edits.

Text and polyline sessions share the C++ opening snapshot implementation. Changes
to native data, context, font, ownership, source slot, placement or drawing state
invalidate preparation. Opening is read-only; preparation additionally checks
the complete proposed drawing's export eligibility. Plans also check stale state
when applied inside the caller's transaction.

The text panel caches its preview plan while content/height/width stay unchanged,
commits through `apply_entity_update()` and refreshes the session after success.
Python retains text controls, origin picking, previews and undo/display workflow.
`DXFCAD.FreeCADTextEditSession` covers TEXT/MTEXT metadata, encoding, validation,
stale opening/cached state, rollback and undo/redo. Text GUI checks cover repeated
edits and drawing changes after preview.

## Related entity copies

`DXFCAD.prepare_entity_copy(entities, records)` returns an `EntityCopyPlan` for
derived records such as offset copies. Supply one record per distinct template
object, all in one document, retaining each template's entity type and layer.
C++ validates geometry/context, removes native handles and ownership, and
validates the complete set of proposed additions to each drawing. Originals and
retained source snapshots remain unchanged. Each copy gets its template's drawing
ownership, source slot `-1`, layer membership and text font.

The plan shares `document`, `objects`, `copies`, `preview_records`,
`drawing_bytes` and one-shot `apply()` with batch plans. Application rejects
stale records, context, font, ownership, source slots, placements and drawings;
layer-group ambiguity is rechecked before mutation. Use a transaction and finish
presentation/recompute before committing. Python's `create_related()` now uses
this API and the shared batch presentation adapter, preserving width display
preferences. Offset dialogs keep distance input, previews and the Keep originals
choice in Python. `DXFCAD.FreeCADEntityCopy` checks complete validation, native
identity, opaque data, multiple drawings, stale state, rollback and undo/redo.

## Offset plans

`DXFCAD.prepare_offset(entities, distance, keep_originals=True)` returns an
`OffsetPlan` for the complete selection. C++ validates the selection, calculates
native offsets and prepares either related copies or in-place replacements.
Duplicate objects are processed once in selection order. Geometry and signed
distance rules match `offset_entity`; one invalid member rejects the whole plan
without mutation. Multiple drawings in one document are supported.

The plan exposes `document`, `objects`, `copies`, `preview_records` and
`drawing_bytes` keyed by drawing name. Call its one-shot `apply()` inside a
transaction, initialize copied-object presentation and recompute before commit.
It retains the copy/update plans' identity, export validation and stale-state
checks. Python's offset panel keeps the previewed plan while distance/mode stay
unchanged and uses `apply_batch_plan()` for presentation and undo. The panel
clears the plan after a successful apply, allowing another offset.

`DXFCAD.FreeCADOffsetPlan` tests both modes, curved metadata, complete validation,
stale state, multiple drawings, rollback and undo/redo. Offset GUI checks also
verify that a drawing change after preview prevents applying that plan.

## Trim/extend plans

`DXFCAD.prepare_trim(target, boundary, start=False, extend=False)` returns a
`TrimPlan` without mutation. `start` selects the endpoint to change; `extend`
selects extension instead of trimming. C++ uses the existing finite-boundary,
coplanar LINE/ARC/open-LWPOLYLINE geometry rules and validates the proposed
target drawing. The boundary remains unchanged.

The plan exposes `document`, `target`, `boundary`, `objects` (only the target),
`preview_records` and `drawing_bytes` (only the target drawing). Both objects
must belong to the same FreeCAD document; separate DXF drawings and detached
boundary objects are supported. Both objects and their owning drawings are
snapshotted, including native data, context, font, ownership, source slot and
placement. A changed or deleted boundary invalidates the plan even when it
belongs to another drawing or has no drawing root.

Call one-shot `apply()` inside a transaction and recompute/check display before
committing. Python's trim/extend panels cache the previewed plan until the
target/endpoint/mode changes and commit through `apply_entity_update()`.
`DXFCAD.FreeCADTrimPlan` covers both endpoints/modes, curved results, native
identity, boundary dependencies, validation, rollback and undo/redo. The drafting
GUI test also checks that changing the boundary drawing after preview rejects Apply.

## Contour operation plans

C++ combines geometry calculation with contour replacement rules through:

```python
plan = DXFCAD.prepare_contour_join(entities, tolerance=1e-7)
plan = DXFCAD.prepare_contour_close(target, tolerance=1e-7)
plan = DXFCAD.prepare_contour_split(target, distances)
plan = DXFCAD.prepare_contour_break(target, first, second)
plan = DXFCAD.prepare_interior_trim(target, boundaries, pick_distance)
```

Each returns a `ContourOperationPlan` with the contour edit plan's `document`,
`root`, `template_object`, `preserves_first`, `created_objects`, `preview_records`,
`drawing_bytes` and one-shot `apply()`. C++ chooses replacement targets, preserves
the first object's identity when its type permits, creates identity-free pieces
and validates the complete target drawing. Join replaces inputs with a new
polyline and requires them in one drawing.

Interior-trim boundaries are read-only dependencies and may belong to separate
drawings in the same FreeCAD document or have no drawing root. Changes to any
selected object or its drawing invalidate Apply, including boundary deletion,
context, font, ownership, source slot or placement changes. Construction never
mutates objects. Use `apply_contour_plan()` for transactions and presentation.

The UI uses `prepare_contour_operation(entities, operation, target_index=0,
distances=[], tolerance=1e-7)`. Operation is `join`, `close`, `split`, `break` or
`trim`; break takes two distances and trim takes one pick distance. Python parses
controls and retains the previewed plan while inputs stay unchanged. Geometry,
identity/replacement rules and dependency tracking stay in C++.
`DXFCAD.FreeCADContourOperations` and the contour GUI test cover all operations,
metadata, export, stale boundaries, topology rollback and undo/redo.

## Polyline corner plans

`DXFCAD.prepare_polyline_fillet(target, vertex, radius)` and
`DXFCAD.prepare_polyline_chamfer(target, vertex, incoming, outgoing)` return an
`EntityUpdatePlan` without mutation. C++ combines the existing corner algorithms
with native-record preservation, complete drawing validation and stale-state
checks. Vertex indices start at zero. Open endpoints, adjacent curved segments,
collinear segments and distances/radii that do not fit are rejected. Closed
polylines also permit editing the corner at index zero.

Corner edits update the existing object in place, retaining its handle, owner,
source slot, layer, flags, OCS/elevation and XDATA. Existing vertex IDs follow
their provenance; the inserted vertex has no inherited ID. Variable widths are
interpolated by the domain algorithm. The plan's `preview_records` and
`drawing_bytes` predict the applied result. Use `apply_entity_update()` for the
undo transaction and display checks.

The fillet/chamfer panels retain the preview plan while inputs stay unchanged,
refresh their vertex selector after success and reject stale drawing state on
Apply. Python retains controls and presentation. `DXFCAD.FreeCADPolylineCornerPlan`
and both GUI checks cover metadata, width interpolation, invalid corners,
preview/export consistency, stale state, rollback and undo/redo.

## Drawing population

`DXFCAD.populate_drawing(document, drawing, source_file="", label="DXF Drawing",
name="DXFDrawing", font_file="")` constructs the persistent FreeCAD graph in
C++. It creates the root, referenced layer groups and entity objects, assigning
unique original source slots and ownership links. Its `DrawingPopulation` result
exposes `root`, `entities` in source-slot order, `layer_groups` keyed by native
layer name, the retained `source` model and `warnings`. Unused layer definitions,
unsupported entities and other opaque records remain in the snapshot. Population
does not add layer-table entries for undeclared imported layers.

The API participates in the caller's document transaction. It does not initialize
Python compatibility proxies, change view appearance, recompute, commit, or
report messages. A typical Python caller uses:

```python
from dxfcad_edit import transaction
from dxfcad_import import initialize_population, default_text_font

with transaction(document, "Populate DXF drawing"):
    result = DXFCAD.populate_drawing(
        document, parsed, source_file=filename,
        font_file=default_text_font())
    initialize_population(result)
```

The transaction helper recomputes before committing. `import_dxf()` additionally
checks display failures and reports warnings; creation or presentation failures
abort the import transaction and close a newly created document. Native builds
create native entity types, and compatibility builds use the same construction
rules with Python lifecycle adapters. `DXFCAD.FreeCADPopulation` covers source
retention, ownership, grouping, multiple drawings, undo/redo and rollback.

## BLOCK / INSERT instances

Model-space INSERT records can display referenced BLOCK definitions as one
selectable native `DXFCAD::InsertObject`. C++ resolves case-insensitive ASCII
block names and nested INSERT dependencies, subtracts each block base point,
and derives affine transforms from insertion OCS, extrusion, rotation, and
signed X/Y/Z scale factors. Nonuniform scaling is applied to derived FreeCAD
geometry; original circles/arcs and block records remain authoritative. Row and
column arrays use rotated, unscaled spacing. Each reference stores only its
reachable block definitions in hidden `DXFContext` for source-free FCStd restore.

Supported block contents are LINE, ARC, CIRCLE, LWPOLYLINE (including displayed
widths), basic TEXT, plain MTEXT, and nested INSERTs. Existing Move, Copy, Rotate,
Mirror, Scale, and DXF Properties workflows operate on the whole instance and
preserve block definitions. Copies export as INSERT references with new handles;
export never explodes the block. C++ APIs `DXFEntity::blockParts()` and
`DXFDocument::resolveEntity()` are exposed as Python `block_parts()` and
`resolve_entity()`. Snapping includes insertion points and transformed child
endpoints, vertices, centers, and midpoints; block-curve intersection snapping
is deferred.

This first version supports one inherited display style per instance: child
layers must be layer 0 or the instance layer, and child colors must be BYBLOCK
or BYLAYER. BYLAYER children require a BYLAYER instance color. Other child layer
or color combinations produce an import warning instead of an inaccurate
display. Attributes/ATTDEF, external references, unsupported block contents,
and cycles are also retained as opaque source records with warnings. Nesting
is limited to 32 levels and expanded content/arrays are bounded to avoid
unbounded recomputation. Block definition creation/editing, attribute editing,
and explode commands are deferred.

`DXFCAD.Insert` tests block resolution, nested/nonuniform/tilted transforms,
arrays, snapping, native editing and export preservation. `DXFCAD.FreeCADInsert`
checks derived shapes, preview, Move/Copy undo/redo, widths/text, export and cold
restore in native and compatibility builds. Run `tests/test_insert_gui.py`
through the isolated GUI runner for selection, mouse preview/commit, transform
panels, layer properties, copy and export in a complete FreeCAD session.

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
the native classes. The native configuration has twenty-two CTest entries, including
LWPolyline, LINE, ARC, CIRCLE, TEXT, MTEXT, and DIMENSION inheritance, C++ recomputation,
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
`DXFCAD_FREECAD_PYTHON_EXECUTABLE` adds FreeCAD CTest entries, for seventeen entries in
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

Open ends use flat caps. Vertex corners use intersections of offset tangents,
including taper, with bevels when a miter exceeds ten times the larger half-width
or has no suitable intersection. This is a bounded display approximation, not a
claim of identical rendering in every DXF application. Curved offsets retain the
sampling tolerance described above. Kernel refinement may retain internal face
seams when removing them would invalidate a coplanar union. Native widths and
other DXF group codes remain authoritative.

## Draw lines

Use **DXFCAD > Draw Lines...**. Click the start point, then click each endpoint
to create connected native LINE objects. Pointer movement previews the next
segment without adding a document object. Each completed segment is a separate
undoable transaction. Escape or right-click ends the current chain and keeps
completed lines; Escape while idle stops viewport picking. **Close** stops the
tool. Numeric start/end coordinates and **Create line from coordinates** use
the same domain validation. Uncheck **Continue from the last endpoint** for
separate segments.

The first line tool works in the world XY plane at the chosen **Elevation Z**.
New lines use layer `0`, respecting its existing group visibility and source
color. Select the destination drawing or one of its entities first when a
document contains multiple drawings. An empty new document is removed if the
tool closes without creating geometry. New drawings use millimeters; imported
drawings retain source coordinates/units. Native creation/export additions
require R2000 or later when a source version is specified.

Snapping shares the polyline editor's visible-object targets and twelve-pixel
radius. Off-plane snaps and zero-length lines are rejected in C++; Ctrl/Alt/Shift
gestures remain available for FreeCAD navigation. Picking callbacks and preview
nodes are detached on exit, view/document switch, and document closure. This
step does not add arbitrary working planes, Ortho constraints, or grid snapping.

`DXFCAD.create_line(start, end, layer="0")` creates a validated native record;
`dxfcad_edit.create_line(document, start, end, root=None)` adds it with drawing
ownership and undo/redo. The domain API accepts 3D WCS endpoints. The interactive
tool delegates ray/plane intersection and snap-plane validation to C++.

Use `--test tests/test_line_creation_gui.py` with the isolated GUI runner to test
click chains, endpoint/center snaps, invalid input, Escape/right-click, numeric
entry, cleanup, undo/redo, source-free FCStd restore, DXF export, and reading a
clean line drawing with FreeCAD's independent DXF reader.

## Draw circles

Use **DXFCAD > Draw Circles...**. Click a center, then a point on the
circumference. Pointer movement previews the native circle without adding a
document object. Alternatively enter **Center X**, **Center Y**, **Radius**, and
**Elevation Z**, then choose **Create circle from center and radius**. Each
completed circle is undoable; the tool stays ready for another center.
Escape/right-click cancels the pending preview and keeps completed circles;
Escape while idle stops picking. **Close** exits.

Circles use world XY at the chosen elevation and layer `0`, with the same units,
drawing selection, visible-object snapping, and cleanup rules as Draw Lines.
Zero, negative, and nonfinite radii and off-plane snap targets are rejected.
C++ owns circle records, radius calculation, validation, and preview geometry;
Python owns the task panel and picking workflow.

`DXFCAD.create_circle(center, radius, layer="0")` creates a native record;
`DXFCAD.circle_radius(center, circumference)` validates two coplanar points and
calculates their radius. `dxfcad_edit.create_circle(document, center, radius,
root=None)` adds the circle with drawing ownership and undo/redo. Save/reopen
and DXF export retain the native CIRCLE data.

Use `--test tests/test_circle_creation_gui.py` with the isolated GUI runner to
check snapping, previews, cancellation, numeric radius entry, invalid input,
undo/redo, FCStd persistence, callback cleanup, and independent DXF reading.

## Draw arcs

Use **DXFCAD > Draw Arcs...**. Pick **Start**, **Through**, then **End**;
the through point determines the direction and whether the arc is minor or
major. Pointer movement previews the native arc before the third click creates
it. The task panel also accepts three numeric points or a center, radius,
start/end angles, and a **Clockwise** option. Angles are degrees from world +X;
negative and multi-turn values are normalized. Use Draw Circles for a full turn.

The task panel uses world XY at **Elevation Z**, layer `0`, and the shared
visible-object snapping. Each completed arc is undoable. Escape/right-click
cancels a pending arc; Escape while idle stops picking. Switching to center/angle
entry stops picking. Close, document closure, and view/document switches release
callbacks and preview geometry. Duplicate or collinear points, nonfinite values,
nonpositive radii, and off-plane snaps are rejected before creating an object.

`DXFCAD.create_arc(center, radius, start_degrees, end_degrees, clockwise=False,
layer="0")` creates a native ARC in world XY. The C++ domain API
`createArcThroughPoints` and its Python binding
`DXFCAD.create_arc_through_points(start, through, end, layer="0")` accept three
WCS points in any plane. C++ calculates and validates the circle and directed
sweep; clockwise arcs use the appropriate OCS normal. The corresponding
`dxfcad_edit` helpers add drawing ownership and undo/redo. Save/reopen and DXF
export retain the native ARC data.

`tests/ArcCreationTests.cpp` covers numeric and three-point construction,
direction, major arcs, wraparound, tilted planes, invalid inputs, and serialization.
Use `--test tests/test_arc_creation_gui.py` with the isolated GUI runner to check
snapping, previews, cancellation, numeric entry, cleanup, undo/redo, FCStd
persistence, and independent DXF reading.

## Move objects

Batch move/copy/rotate/scale/mirror now use the independent C++
`BatchTransformPlan` record model and its FreeCAD adapter. For example:

```python
from dxfcad_edit import apply_batch_plan

plan = DXFCAD.prepare_batch_transform(
    selected_objects, "move", offset=DXFCAD.Vector3D(10, 5, 0))
# plan.preview_records contains (source_object, transformed_DXF_record) pairs.
result = apply_batch_plan(plan, "Move DXF selection")
```

Use `"copy"` with an offset, or `"rotate"`, `"scale"`, `"mirror"` with `value`
and an optional `pivot`. Rotation/mirror values are degrees; scale takes a
positive factor. Missing offsets/pivots default to zero vectors. Zero move is
rejected unless `allow_identity=True` is supplied for validation/previews;
copying with zero offset is supported. Duplicate selected objects are processed
once, in selection order. Objects must belong to one FreeCAD document; multiple
DXF drawing roots in that document remain independent. Unowned native objects
can be transformed in place, while copying requires drawing ownership.

Planning mutates nothing. It validates transformed records and the complete
result for every affected drawing, exposing predicted export bytes in
`plan.drawing_bytes` keyed by root name. Updates retain object/handle/source-slot
identity; copies remove native identity and receive source slot `-1`. The
adapter checks object identities, native records, contexts, fonts, ownership,
source slots, placements and retained/current drawing snapshots before Apply.
Each mutation attempt consumes the plan. Rebuild after Apply, Undo or failure.
`apply_batch_plan()` provides the undo transaction, compatibility proxies,
presentation settings and recomputation. Move/Copy and Rotate/Scale/Mirror task
panels use the same planning API for preview and commit. Generic raw-record
editing helpers used by other tools retain their existing APIs.

Select native objects, then use **DXFCAD > Move Objects...**. Click a base point
and a destination to move the entire selection together. The orange preview
shows the proposed positions without changing document objects. Picking uses
world XY at **Pick plane Z**, with the shared visible endpoint, vertex, and
center snaps. Off-plane snap targets are rejected. Escape/right-click cancels
the preview; Escape while idle stops picking. **Close** exits.

For an exact 3D move, enter **Delta X**, **Delta Y**, and **Delta Z**, then choose
**Move by displacement**. Coordinates use drawing units. Zero and nonfinite
displacements are rejected. Each move is one undoable transaction, including
all selected objects. A validation or display failure rolls back the whole move.
If another action changes or deletes a selected object, close and reopen the
tool to select the current objects.

Move supports LINE, ARC, CIRCLE, LWPOLYLINE, TEXT, and MTEXT. Cached DIMENSION
pictures remain read-only. C++ translates native coordinates using each entity's
WCS/OCS rules, including tilted polylines/circles and TEXT alignment points;
MTEXT direction vectors retain their original values. Handles, owners, layer,
widths, bulges, vertex IDs, application groups, and XDATA are retained. FreeCAD
Placement stays at identity; FCStd save/reopen and DXF export use the moved data.

`DXFCAD.parse_entity(raw).translated(offset)` returns the translated record;
`DXFCAD.displacement(base, destination)` validates a nonzero displacement.
`dxfcad_edit.move_entities(objects, offset)` applies a selection atomically.
Use `--test tests/test_move_gui.py` with the isolated GUI runner to check all six
entity types, snapped picking, preview cancellation, numeric input, undo/redo,
stale-selection handling, persistence, export, and viewport cleanup.

## Copy and rotate selections

**Copy Objects...** uses the same snapped base/destination and numeric
XYZ displacement controls as Move. Originals are retained; each copied selection
is one undoable operation. Copies retain their layers, display fonts, widths,
and native data. New export handles are allocated; source ownership, reactors,
and extension-dictionary links are removed from the new records. LINE, ARC,
CIRCLE, LWPOLYLINE, TEXT, and MTEXT copies are supported.

**Rotate Objects...** rotates a selection about world Z, using a numeric center
and angle in degrees, or a picked center and direction measured from world +X.
Tilted entities are re-expressed in the rotated OCS; text directions and both
TEXT insertion/alignment points rotate with the geometry. Each rotation is one
undoable operation. The shared tools reject stale selections and detach picking
callbacks on closure or document/view switches.

**Scale Objects...** uniformly scales a selection about a center. Enter a positive
factor, or pick a center and a point: the factor is their distance divided by
**Reference length**. Coordinates, radii, widths, thickness, and text dimensions
scale together; source units and dimensionless text spacing remain unchanged.

**Mirror Objects...** reflects the selection across the vertical plane through
a picked line, or a point and an angle from world +X. Text outlines are reflected
too. OCS handedness and thickness signs are adjusted to retain native curves and
extrusion semantics. Mirroring twice across the same line restores the geometry.

Run `tests/test_copy_gui.py` and `tests/test_rotate_gui.py` with the isolated GUI
runner for selection previews, cancellation, repeated copies, snapping, undo,
FCStd persistence, and export checks.

## Draw rectangles

**Draw Rectangles...** creates a closed four-vertex native LWPolyline from two
opposite corners in world XY at the chosen elevation. Pick snapped corners or
enter Start/End XY coordinates. Zero width/height and off-plane corners are
rejected. The preview adds no document object; each finished rectangle has one
undo step and exports as a native LWPOLYLINE. New rectangles use layer `0` and
drawing units. Escape/right-click cancels a pending rectangle.

## Trim and extend objects

Select two native objects, then use **Trim Objects...** or **Extend Objects...**.
Choose a LINE, ARC, or open LWPOLYLINE target and its Start/End endpoint. The
other object is a finite LINE, ARC, CIRCLE, or open/closed LWPOLYLINE boundary.
Both entities must share a plane; tilted OCS planes are supported.

Trim uses the first intersection encountered from the selected endpoint and
retains the opposite endpoint. For polylines it removes segments beyond the cut,
shortens the intersected straight or bulged segment, and interpolates variable
widths. Extend lengthens only the selected terminal segment to the first boundary
intersection ahead. Arcs retain their center/radius and cannot extend through a
complete revolution. Width interpolation also applies to extensions; results
with negative widths are rejected.

**Preview** changes no objects; **Apply** commits one undo step; **Close** cancels
the preview. Handles, ownership, surviving vertex IDs, elevation/extrusion,
constant widths, and opaque data are retained. Tangent contacts can be used as
cut points. Parallel or coincident supports have no unique cut and are ignored;
intersections outside a finite boundary are ignored. A missing valid intersection
is reported without modifying objects. Closed polylines and circles can be
boundaries but cannot be endpoint-edit targets; cutting out an interior portion
or splitting closed entities uses the contour cleanup tools below.

C++ `Drafting::trim` is exposed as Python `DXFCAD.trim_entity(target, boundary,
start, extend=False)`. The earlier `trim_line` API remains as a compatibility alias.
`tests/TrimExtendTests.cpp` covers analytic intersections, nearest-hit selection,
curved/straight polyline trimming, extensions, widths, vertex IDs, tilted planes,
export, and invalid operations. `tests/test_drafting_gui.py` exercises previews,
undo/redo, persistence, export and task-panel cleanup.

## Contour cleanup

Contour replacement uses an independent C++ `ContourEditPlan` domain object.
It validates the complete proposed drawing, decides whether the first result
retains the original object/handle/source slot, removes identity from new
pieces, and accounts for deleted originals while retaining unselected and
unsupported source records. Selection indices address live records, so multiple
new objects with source slot `-1` remain distinct.

`DXFCAD.prepare_contour_edit(entities, records, preserve_first=True)` creates
the FreeCAD adapter plan without changing the document. Its `preview_records`
drive the task panel preview; `drawing_bytes` contains the proposed export.
`dxfcad_edit.apply_contour_plan(plan, label)` applies that plan inside an undo
transaction, initializes presentation/compatibility proxies, and recomputes.
The C++ adapter checks selected identities/records/source slots, the retained
snapshot, current drawing contents, placements and layer-group ambiguity before
mutation. A mutation attempt consumes the plan; rebuild it after Apply, Undo,
or a rolled-back failure. Python retains mouse interaction, task panels,
display settings and transaction boundaries. Join, split, break, close and
interior trim all use the same planning API in native and compatibility builds.

The **DXFCAD Contour** toolbar and DXFCAD menu provide **Join Contours...**,
**Split Contour...**, **Break Contour...**, **Close Polyline...**, and
**Trim Interior Section...**. C++ `DXFCAD::Contour`, exposed as Python
`DXFCAD.Contour`, owns topology, distances, exact line/arc intersections, width
interpolation, and native record generation. Python handles selection, picking,
preview and document transactions.

- **Join:** select connected coplanar LINE, ARC, and open LWPOLYLINE objects in
  one drawing. Selection order and direction are resolved automatically. The
  result is one new LWPOLYLINE, closed when the endpoint graph forms a loop.
  An endpoint tolerance (default `1e-7` drawing units) allows small gaps; shared
  endpoints are snapped within that tolerance. Disconnected selections,
  branches, duplicate lines, collapsed endpoints and ambiguous tolerance
  clusters are rejected. Inputs must share layer/display properties.
  Bulges and effective segment widths survive reversal. The first selection's
  application metadata survives; differing nonempty metadata on another input
  is rejected. This is endpoint joining, not overlap removal or self-intersection
  repair.
- **Split:** select one LINE, ARC, CIRCLE or LWPOLYLINE. Enter comma-separated
  distances along its native traversal, or use **Pick on contour**. Distances
  use drawing units, and the panel shows the target length. Open contours split
  into consecutive pieces. A single cut opens a closed contour at that seam;
  multiple cuts split it cyclically. A circle opened at one seam becomes an
  open LWPOLYLINE with exact circular bulges; partial circles become ARCs.
- **Break:** select one contour and specify two distances, numerically or by
  picking. The interval between them is removed. On closed contours removal
  follows the native direction from First to Second, wrapping across the seam
  when First is greater than Second. The complement remains as an open piece.
  On open contours the distances are sorted, retaining either or both ends.
  Removing the entire contour is rejected.
- **Close:** select one open LWPOLYLINE. A duplicate terminal vertex within
  tolerance is merged with the start; otherwise a straight closing segment is
  added. A formerly unused terminal bulge is cleared for the new straight edge.
- **Trim Interior Section:** select a target and one or more finite boundaries,
  then choose the target in the panel. Pick inside the portion to remove, or
  enter a distance along the target. The nearest distinct crossings on either
  side bound removal; open endpoints can bound end sections. Closed polylines
  and circles can be targets. Duplicate crossings are merged. Tangential
  contacts and touching polyline corners do not cut; overlapping boundaries,
  picks on intersections, noncoplanar geometry and ambiguous centerline picks
  are rejected. Boundaries remain unchanged.

**Preview** changes no document objects; **Close** cancels it. **Apply** commits
one undo step and closes the panel. Undo/redo includes created and deleted
objects. Retained pieces preserve native data, OCS/elevation, widths, bulges,
application data and surviving vertex IDs. The first piece retains its entity
handle/owner when its type is unchanged; added pieces receive new export
handles. Join and type-changing edits replace source entities, and references
to those deleted entities are not repaired. Source-free FCStd restore and DXF
export/reimport are tested in both native and compatibility runtimes.

`DXFCAD.Contour` and `DXFCAD.FreeCADContour` cover domain operations, curved and
tilted cases, tapered/constant widths, metadata/identity, rollback, undo/redo and
cold restore. Run `tests/test_contour_gui.py` with the isolated GUI runner for
the commands, real viewport picking, preview/cancel, Apply, undo/redo and export.

## Offset and fillet

Separate-LINE fillet/chamfer application uses an independent C++
`CornerEditPlan` and a FreeCAD adapter:

```python
from dxfcad_edit import apply_corner_plan

plan = DXFCAD.prepare_line_fillet(
    [first_line, second_line], radius=2,
    keep_first_start=True, keep_second_start=False)
# plan.preview_records: two LINE updates followed by the ARC connector.
arc = apply_corner_plan(plan, "Fillet DXF lines")
```

`prepare_line_chamfer(entities, first_distance, second_distance, ...)` provides
the corresponding LINE connector. Both APIs compute geometry and validate the
complete proposed drawing without mutation. `plan.drawing_bytes` predicts the
export result. C++ preserves both original LINE objects, handles, source slots
and layers; the connector has new identity and source slot `-1`. It checks that
the connector joins endpoints of both updated lines and retains unsupported
source records. Application payload stays with the original lines, while the
new connector receives the first line's visual properties.

Apply rejects stale selections, ownership/source-slot changes, placements,
contexts, source snapshots and other native drawing edits. Mutation consumes
the plan; rebuild it after Apply, Undo or a rolled-back failure. Python owns
the undo transaction, compatibility lifecycle, view providers and recomputation.
The task panels use plan records for preview and apply that plan on commit.
Existing `apply_line_fillet()`/`apply_line_chamfer()` record callers use
`prepare_corner_edit()` to obtain the same validation and application behavior.
`DXFCAD.CornerEdit` and `DXFCAD.FreeCADCornerEdit` exercise identity, connector
validation, stale state, rollback, undo/redo and export consistency.

**Offset Objects...** supports LINE in world XY, ARC/CIRCLE in their OCS planes,
and straight or curved open/closed LWPolylines in their OCS planes. A positive
signed distance means left of a line/polyline: counterclockwise polyline arcs
shrink and clockwise arcs grow. For a standalone ARC/CIRCLE, positive distance
continues to enlarge its radius. Offset operates on native centerlines.

Straight segments stay parallel and curved segments remain concentric circular
arcs. Adjacent supports meet at the nearest analytic intersection, retaining the
existing bounded-miter behavior. Disjoint outer supports receive a tangent
circular connector around the original corner. Smooth joins need no new vertex.
Closed two-arc circles and mixed line/arc profiles are supported, including tilted
and reversed OCS planes and major arcs.

Original segment widths, constant width, elevation/extrusion, thickness, surviving
vertex IDs and opaque data are retained. A new connector transitions from the
incoming segment's end width to the outgoing segment's start width; its inserted
vertex has no inherited source ID. Collapsed/reversed segments, nonpositive arc
radii, arcs wrapping through a complete revolution, overlapping/self-crossing
paths, and miters longer than 100 times the offset distance are rejected. Results
that require removing collapsed segments or returning multiple contours are
rejected rather than silently dropping geometry.

**Keep originals** creates new native records with fresh export handles;
uncheck it to update the originals. Both modes preview before applying and use
one undo step per selection. Validation of the entire selection happens before
any document changes. C++ `Drafting::offset` and Python `DXFCAD.offset_entity`
retain their existing signatures. `tests/PolylineOffsetTests.cpp` checks analytic
joins, connectors, planes, native data, invalid results and export;
`tests/test_curved_offset_gui.py` checks preview/cancel, copy/in-place modes,
atomic failure, undo/redo, export and FCStd restore.

Select one LWPolyline and use **Fillet Polyline Corner...** to replace a corner
between two straight segments with a tangent native bulged arc. Choose its
zero-based vertex index and a positive radius that fits both segments. Vertex
IDs and opaque data are retained, and variable widths interpolate through the
new arc. **Preview**, **Apply**, and **Close** follow the drafting-panel workflow.

**Chamfer Polyline Corner...** uses the same corner selection with separate
incoming/outgoing distances. It inserts a straight bevel, retains native vertex
IDs and data, and interpolates widths at the cut points. Both distances must be
positive and fit within the adjacent straight segments. LINE/ARC and ARC/ARC
fillets remain deferred.

**Fillet Lines...** joins two distinct native LINE objects in the same DXFCAD
drawing with a new native ARC. Select both lines, enter a positive radius, and
choose the Start/End endpoint to keep on each line. Those endpoints determine
the side of the corner, including all four solutions for crossing lines. The
other endpoints are trimmed or extended to tangent points. Lines may lie in an
arbitrary common plane; they need not meet before the operation.

**Preview** shows the two adjusted lines and arc without creating objects.
**Apply** updates both lines and adds the arc in one undo step, then closes the
panel; **Close** cancels the preview. Original handles, owners, styles and opaque
data remain on the lines. The new arc inherits the first line's native layer,
linetype, color, lineweight, visibility, linetype scale and transparency, but no
identity, ownership or application payload. It uses its own OCS and a fresh
export handle. Its thickness defaults to zero. A new ARC requires an R2000+
source under the existing exporter contract; preflight occurs before any edits.
Parallel, collinear, skew or zero-length lines, a kept endpoint at the corner,
and radii that would consume a kept endpoint are rejected. Nonidentity FreeCAD
Placements must first be reset or applied through native DXFCAD transforms.

C++ `Drafting::filletLines(first, second, radius, keepFirstStart=true,
keepSecondStart=false)` returns `LineFilletResult` with updated `first`/`second`
records and an identity-free `arc`. Python exposes the same operation as
`DXFCAD.fillet_lines`. Geometry and DXF semantics stay in C++; Python owns the
preview and atomic document transaction. `tests/LineFilletTests.cpp` covers
sides, tangency, extension, tilted planes, metadata and serialization.
`tests/test_line_fillet_gui.py` covers preview/cancel, rollback after arc creation,
one-step undo/redo, ownership/version preflight, independent DXF reading, and
FCStd restore without the source file.

**Chamfer Lines...** bevels two distinct native LINE objects in the same drawing.
Choose one endpoint to keep on each line and enter two positive distances,
measured from the extended intersection toward those kept endpoints. Each other
endpoint is trimmed or extended to its distance point, and a new native LINE
connects those points. Equal or unequal distances and arbitrary common planes
are supported; the source lines do not need to meet before chamfering.

**Preview** shows all three segments without modifying the document. **Apply**
updates both originals and adds the connector as one undo step, then closes the
panel. **Close** cancels the preview. Source identities and opaque data are
retained. The connector inherits the first line's native layer and visual
attributes, with no inherited identity, owner or application payload, zero
thickness, and a fresh export handle. Invalid/consuming distances, parallel,
collinear, skew or degenerate lines, and collapsed connectors are rejected.
Ownership and the existing R2000+ new-entity export requirement are checked
before any edits. Nonidentity FreeCAD Placements must be reset or transformed
through native DXFCAD controls before corner editing.

C++ `Drafting::chamferLines(first, second, firstDistance, secondDistance,
keepFirstStart=true, keepSecondStart=false)` returns `LineChamferResult` with
updated `first`/`second` records and an identity-free `connector` LINE. Python
exposes `DXFCAD.chamfer_lines`. Fillet and Chamfer share the C++ `LineCorner`
validation and Python atomic transaction workflow. `tests/LineChamferTests.cpp`
covers distances, sides, extension, tilted planes, metadata and serialization;
`tests/test_line_chamfer_gui.py` covers preview/cancel, rollback, undo/redo,
preflight, independent DXF reading and FCStd restore. The combined
`tests/test_line_corners_gui.py` runs both separate-line workflows consecutively.

## Create and edit text

**Create Text...** creates TEXT or plain MTEXT with an origin, height, and
optional multiline wrapping width. **Pick text origin** shares endpoint/vertex/
center snapping in world XY at Origin Z. **Edit Text...** edits a selected text
object's content and height; MTEXT also exposes width (`0` means no wrap).
Use Move/Rotate to change an existing text object's placement in native data.

The panel previews outlines without adding objects, applies each change in one
undo step, and preserves existing fonts and justification while editing. New
text uses the same substitute-font selection as import. MTEXT escapes literal
backslashes/braces and splits long content into UTF-8-safe DXF chunks. Single-line
TEXT content is limited to 250 bytes; use MTEXT for longer text or paragraphs.
Empty text, unsupported control characters, invalid heights, and invalid MTEXT
widths are rejected. Rich inline formatting, columns, and text-style authoring
remain deferred.

## Layers and properties

Select native objects and use **DXF Properties...**. Assign an existing layer or
enter a new layer name, and set entity visibility. Color stays unchanged unless
**Set ACI color** is enabled; choosing it replaces any true-color override.
ACI `0` is BYBLOCK and `256` is BYLAYER. New layers use the entered layer ACI
color/on state; enable **Set existing layer's ACI color and on/off state** to
change an existing layer. Turning a layer on also unfreezes it globally; locked
flags and saved viewport freeze lists remain intact.

Apply updates native entity records, layer-table metadata, tree groups, and
presentation together in one undo transaction. Layer changes affect other
entities on that layer too. Entity-hidden flags remain effective when a layer is
turned back on. Save/reopen and export retain layer tables and native styles.
New layers require R2000 or later when a source version is specified. New names
allow letters, digits, spaces, underscore, hyphen, dot, dollar sign, and Unicode;
case-only duplicates are rejected. Renaming/deleting layers and authoring custom
linetypes are deferred.

Run `tests/test_common_features_gui.py` through the isolated GUI runner with
`--timeout 600` to exercise the common tools sequentially in one application,
including cleanup between tools, undo/redo, FCStd persistence, and DXF export.
Each tool also has its own `tests/test_*_gui.py` script for focused verification.

## Create, edit, and export LWPolylines

Use **DXFCAD > Create LWPolyline...** to enter vertices, bulges, constant or
variable widths, closure, elevation, and extrusion. New drawings use millimeters
and an R2000 (`AC1015`) ASCII DXF header. A new entity is placed on layer `0`.
In a document containing several imported drawings, select the destination
drawing or one of its entities first.

Select an LWPolyline and use **Edit LWPolyline...**, or double-click it. Insert
and remove rows in the task panel. **Use widths** preserves explicit variable
widths even when both values are zero. Other nonzero variable widths also take
precedence over constant width, following the domain API. The transform controls
move in world coordinates, rotate or uniformly scale about an OCS pivot, and
mirror about an OCS line. Mirroring reverses bulges; scaling scales widths by the
absolute factor. The extrusion vector and source units remain unchanged.
These controls update the table; **OK** validates and commits one undoable edit.
**Cancel** leaves the document unchanged. This first editor uses numeric table
entry and a viewport move tool. With this document active, choose **Move vertices
in view**, press an orange vertex handle, move the pointer to preview, and release
to place it in the table. Movement of four screen pixels starts a drag. The
previous two-click workflow also works: click a handle, then its new position.
**OK** commits all placed changes as one edit. Escape
or right-click cancels a pending move; Escape with no pending move exits the tool.
Optional snapping targets this polyline's vertices and visible DXFCAD objects in
the same document within twelve screen pixels. Cross-object targets include
polyline vertices, LINE/ARC endpoints, CIRCLE/ARC centers, TEXT/MTEXT insertion
points, LINE/ARC/polyline segment midpoints, and intersections between visible
native centerlines. Arc midpoints follow half the directed sweep, including
clockwise and major bulges; circles have no unique midpoint. Intersection snaps
support finite coplanar LINE, ARC, CIRCLE and LWPOLYLINE geometry, including tilted
planes and rigid object placements. They do not extend edges or create apparent
intersections between geometry at different elevations. Tangent contacts produce
one snap point. Coincident supports are ignored; their existing endpoints and
vertices remain available. The nearest screen target wins. Exact cross-object
ties prefer endpoints/vertices, then intersections, then midpoints, then centers
or insertion points; this polyline's own vertices retain priority in its editor.
Hidden objects and hidden parent groups/layers are excluded. Target FreeCAD
Placements are applied to their native snap points. Off-plane targets are rejected
using a C++ tolerance of `1e-7` source units; they are not projected onto the plane.
An invalid drag release restores the last placed table value. Snapping copies a
position without adding a constraint or changing the target object. Modified
mouse gestures remain available
for FreeCAD navigation. Projection and validation run in C++, including tilted
OCS planes and perspective views. An edge-on view reports an error instead of
choosing an unstable position. The preview is a transient centerline and handles;
native data, source shape, and visibility remain unchanged until **OK**. Preview
nodes and event callbacks are removed on exit, document/view switch, or document
closure. Cross-object snapping currently supports native DXFCAD objects; arbitrary
Part shapes, perpendicular/tangent construction snaps, and grid snapping remain
unimplemented. Midpoints and intersections also use the shared snapper in the
line, circle, rectangle, move/copy, transform, and text-origin tools. Native
snapshots and pair results invalidate on data, context or placement changes;
document and view observers invalidate object snapshots on edits and visibility
changes, and labels are read when a target is selected. Viewport bounds filter intersection
candidates, and pair results are retained only near the current pointer. Screen
projections are cached until the native snapshot, camera or viewport size changes.
Observers detach when the tool or document closes.

C++ `SnapGeometry` holds an immutable placed centerline snapshot, exposes
conservative bounds, and computes finite intersections using the same analytic
primitives as Trim/Extend. Python `DXFCAD.SnapGeometry(entity)` uses identity
placement; its five-argument constructor accepts origin and three orthonormal
right-handed axes. `entity.snap_points()` now appends midpoint targets after the
existing snap points. `tests/SnapGeometryTests.cpp` covers geometry and placement;
`tests/test_object_snapping_gui.py` and `tests/test_polyline_drag_snap_gui.py`
exercise real mouse picking/dragging, cache invalidation, undo/redo and export.

**Export Native DXF...** exports one whole drawing, including supported source
entities and opaque unsupported entities, tables, blocks, headers, paper space,
and other sections. The source snapshot is stored inside the FCStd document;
export does not reread `SourceFile`. It replaces supported source entity slots
with their current records, omits deleted supported entities, and appends newly
created LWPolylines, LINEs, and CIRCLEs with unique handles. It retains source version and units;
adding entities to a drawing explicitly older than R2000 is rejected instead of
silently upgrading it. Export through **File > Export > DXFCAD native DXF** also
exports the whole drawing containing the selected objects.

Untouched group values and record order are retained, including unsupported or
invalid records reported during import. Group-code spelling and line endings
are normalized; output is not byte-identical to the source file. Editing preserves
entity handles, ownership, unrelated fields, XDATA, non-closure flag bits, and
vertex IDs through explicit vertex-origin mappings. External references to a
deleted entity are retained as source data and are not repaired. Export does not
expand blocks, interpret unsupported entities, or convert DXF versions/codepages.
An existing destination is replaced only after validation and successful writing.

Objects with an additional FreeCAD `Placement` must have that placement reset
before DXFCAD editing/export; use the native transform controls instead. FCStd
files imported before source snapshots were introduced must be reimported to
export a complete source drawing with this workflow.

The C++ editing APIs return validated LWPolyline values without changing the
original. `read_lwpolyline(raw)`, `update_lwpolyline(raw, model, origins)`, and
`create_lwpolyline(model, layer="0")` handle native record semantics. Use `-1` for
new vertices in `origins`; existing vertices use their original indices. The
default mapping is positional when the count is unchanged. `DXFDocument.empty`,
`serialize`, and `write` provide drawing-level export with replacement indices,
deleted indices, and added records. Python `dxfcad_edit.apply_polyline` and
`create_polyline` add FreeCAD transactions; `dxfcad_export.export_dxf` writes a
persisted drawing.

CTest covers native editing, transforms, metadata preservation, width joins,
undo/redo, source-free FCStd restore, export/reimport, separate drawings, deleted
entities, and failed export preservation. Run the task-panel test in an isolated
profile using the GUI runner described below, with
`--test tests/test_polyline_editing_gui.py`.
Use `--test tests/test_polyline_view_gui.py` for real Coin event dispatch covering
tilted-plane picking, perspective projection, snapping, invalid collapsed arcs,
preview cancellation, cleanup, native-data preservation, undo/redo, and export.
Use `--test tests/test_polyline_drag_snap_gui.py` for drag/release, fast drags,
cross-object targets, hidden layers, target Placements, off-plane rejection,
Escape/deactivation during a held drag, metadata preservation, and export.
This suite also checks cache invalidation after target data, visibility, label,
and Placement changes, and deletion. Native snap points are cached by unchanged
DXF record; group membership and effective visibility are shared within each
lookup. Camera projection, Placements, visibility, and labels are read afresh.

Use `--test tests/test_saved_views_gui.py` for the saved-view dialog, camera
navigation, frozen-layer preview/restoration, renamed groups, multiple-drawing
isolation, unsupported-view rejection, source-free
restore, and unchanged export. Optional `DXFCAD_VALIDATION_FILES` supplies local
DXF paths; viewport/navigation counts are written to `saved-views-validation.json`
in the isolated profile. `DXFCAD.Viewport` tests metadata validation, layout
ownership resolution, view-twist/target math, and native record preservation.

Use `--test tests/test_polyline_stress_gui.py` for a generated 1,104-entity drawing
with a 120-vertex bulged polyline and wide polylines in default, tilted, and
reversed OCS planes. It checks import, shape validity, unchanged export,
viewport mouse drag/release, preview/cancel preservation, edit undo/redo,
source-free FCStd restore, and
export/reimport. Counts and timings are saved to `drawing-validation.json` in
the isolated profile. Timing measurements describe that machine/run and are not
fixed pass/fail thresholds.

To include real drawings, set `DXFCAD_VALIDATION_FILES` to a JSON array of local
DXF paths before running the stress test, for example in PowerShell:

```powershell
$env:DXFCAD_VALIDATION_FILES = ConvertTo-Json -InputObject @("C:\path\to\drawing.dxf") -Compress
```

Set `DXFCAD_VALIDATION_SKIP_SYNTHETIC=1` to run only the supplied drawings.
Set `DXFCAD_VALIDATION_CONTINUE=1` to collect failures across all supplied
drawings; the suite still fails if any drawing fails. For larger batches, pass
`--timeout 900` to `tests/run_gui_test.py` (the default is 120 seconds).
Each drawing with a usable LWPolyline exercises a viewport vertex drag with
snapping disabled; the dedicated drag/snap suite covers snap behavior.

Original drawings are read only. Derived exports and FCStd files use temporary
directories under the ignored test profile and are removed afterward. Reports
contain counts/timings rather than drawing geometry. With no paths supplied,
the report explicitly records zero real drawings tested. Real drawings must
remain outside version control. A real drawing's unit/version and unsupported
records are retained, subject to the import limitations below.

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
LINE, ARC, CIRCLE, TEXT, MTEXT, and supported DIMENSION
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
CIRCLE, TEXT with standard horizontal and vertical justification, plain MTEXT,
saved pictures of supported linear/rotated DIMENSION entities, and supported
BLOCK/INSERT instances.
Unsupported entities and TEXT aligned/fit/horizontal-middle modes,
oblique, or mirrored styles are reported in the import group's `ImportWarnings`
and Report view. Paper space, binary DXF, legacy text
codepage conversion, rich DXF text formatting, line-type patterns, thickness,
are not implemented. LWPolylines display width regions or centerlines using
**Show Widths**; widths remain authoritative in the stored native record. Zero-length straight segments
do not create Part edges. TEXT uses outline wires in a substitute TrueType font
(Arial on Windows), rather than reproducing the DXF font style. The `TextFont`
property selects the display font and can be changed before recomputing.
MTEXT shares that font property. `DXFCAD.MText` and `DXFCAD.FreeCADMText` test
parsing/layout, wrapping, attachment, tilted WCS placement, font recovery,
undo/redo, source-free cold restore, and preservation through DXF export.

For scripting in FreeCAD:

```python
from dxfcad_import import import_dxf
doc = import_dxf(r"C:\path\to\drawing.dxf")
Gui.activeDocument().activeView().viewTop()
Gui.activeDocument().activeView().fitAll()
```

## Install and check the workbench

### Launch from VS Code on Windows

Open this repository in VS Code, then select **Run and Debug** (`Ctrl+Shift+D`):

- **DXFCAD: FreeCAD 1.1 (manual testing)**: press `F5` to build and stage DXFCAD,
  open FreeCAD, and activate the workbench automatically.
- **DXFCAD: contour GUI tests (FreeCAD 1.1)**: runs the contour workflow checks
  and closes the test application, reporting success/failure in the terminal.
- **DXFCAD: current GUI test (FreeCAD 1.1)**: first open a `tests/test_*_gui.py`
  file, then launch it through the isolated GUI runner.

These configurations use the installed `C:/Program Files/FreeCAD 1.1` runtime
and the existing matching `build/python311` CMake configuration. The pre-launch
task loads the MSVC environment selected in that build's cache, builds current
sources, and installs into `build/python311/vscode-profile/Mod/DXFCAD`.
Preferences and temporary files stay in that workspace profile. Close that
FreeCAD instance before rebuilding its loaded extension. The **DXFCAD: contour
domain and integration tests** task runs the non-GUI contour checks as well.

The Python launch configurations debug the test runner; they do not attach
Python debugging inside the FreeCAD subprocess. The C++/Python debugger
extensions are required for these launch configurations.

### Install manually

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
