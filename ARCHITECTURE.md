# OpenShape Architecture

This document describes the architecture **as it is implemented today**.
Technology choices and the alternatives considered are in
[docs/TECHNOLOGY_EVALUATION.md](docs/TECHNOLOGY_EVALUATION.md).

## Principles

1. The exact B-rep (OpenCASCADE) is the authoritative geometry. Meshes are
   derived, disposable display/pick/export artifacts.
2. The parametric document (features + parameters) is the source of truth.
   Geometry is regenerated from it; stored geometry is only a cache.
3. Everything below the UI is Qt-free and unit-tested headlessly. The Qt layer
   translates input and draws; it contains no modeling logic.
4. Every document edit is a `Command` on the undo stack. Previews never touch
   the document.
5. Kernel failures never escape as exceptions: they become `Result`s with a
   user message and a developer message.
6. Persistent identity is UUIDs. Kernel topology indices are transient.

## Layers

```
 Input devices (mouse, touch, pen, keyboard)
        │  Qt events
        ▼
 ui/        ViewportItem (QQuickRhiItem), AppController (QObject bridge), QML
        │  PointerEvent / Key / numeric text      ▲ properties, RenderScene
        ▼                                         │
 interaction/  InteractionController ── Operations (PushPull, Edge, OffsetFace,
        │         │   Shell, Extrude, Revolve, Move, Rotate, Align, Mirror,
        │         │   Pattern, Insert)
        │         │  camera, hover, selection, manipulators (arrows, rings), previews
        │         ├─ SketchSession (tools, snapping, inference, typed dimensions)
        │         ├─ TouchGestureRecognizer (touch frames → pointer, pan/pinch, undo/redo)
        │         ▼
        │   selection/  picking (CPU ray/segment/profile), SelectionSet (+signatures)
        ▼
 commands/  Command + UndoStack (CreateBody, AddFeature, SetParameter, EditSketch…)
        ▼
 document/  Document → Sketches + Bodies → Feature history (Box, PushPull,
        │            Fillet, Chamfer, Shell, Extrude, Revolve, Hole, Move,
        │            Combine, Mirror, Pattern, DeleteFaces, OffsetFace);
        │            SketchProfiles bridge
        ├──────────────────────────────┐
        ▼                              ▼
 geometry/  Shape, Modeling,       sketch/  Sketch model (points, lines, circles,
   Profiles, Tessellation,                  arcs, constraints) + SketchSolver
   TopoSignature, Exchange                        ▼
        ▼                             PlaneGCS (vendored, shared lib, Eigen)
 OpenCASCADE 7.9 (only included inside geometry/)

 render/    ViewportRenderer (QRhi) — consumes interaction::RenderScene
 io/        .openshape project container (ZIP + JSON)
 core/      Result, Log, Uuid, Units, Math, Camera, Timer
```

Library targets and their dependencies (`src/CMakeLists.txt`):

| Target | Depends on | Qt? | Notes |
|---|---|---|---|
| `openshape_core` | – | no | logging, UUID, units, math, camera |
| `openshape_geometry` | core, OCCT (private) | no | **only** place OCCT headers are included |
| `planegcs` (shared) | Eigen | no | vendored FreeCAD solver, C++23, unmodified |
| `openshape_sketch` | core, planegcs (private) | no | sketch model; only `SketchSolver.cpp` sees PlaneGCS |
| `openshape_document` | geometry, sketch, nlohmann_json | no | sketches, features, bodies, recompute |
| `openshape_commands` | document | no | undo/redo |
| `openshape_selection` | document | no | picking, selection sets |
| `openshape_interaction` | commands, selection | no | controller, manipulators, operations |
| `openshape_io` | document, libzip (private) | no | project files |
| `openshape_render` | interaction, Qt Gui/GuiPrivate/Quick | yes | QRhi renderer + shaders |
| `openshape_ui` | render, io, Qt Quick/Controls | yes | QML module `OpenShape` |
| `openshape` (exe) | ui | yes | `OpenShape.exe` |

## Core types

- `Result<T>` / `Status` (`core/Result.h`): success value or `ErrorCode` +
  `userMessage` ("Unable to create this fillet. Try a smaller radius.") +
  `developerMessage` (kernel detail, logged).
- `Uuid` (`core/Uuid.h`): RFC 4122 v4, the identity of bodies and features.
- Units (`core/Units.h`): **millimeters are the only internal unit**. Input is
  parsed with `parseLength(text, displayUnit)`, which accepts units (`1in`,
  `2.5cm`) and arithmetic (`20+5`, `(10+2)*3`). A bare number uses the
  document display unit. Saved files state `"lengthUnit": "mm"`.
- `Camera` (`core/Camera.h`): Z-up turntable, orthographic by default,
  perspective optional. Pure math shared by picking, interaction and rendering.
- Logging (`core/Log.h`): categories APP, DOCUMENT, COMMAND, GEOMETRY, KERNEL,
  SKETCH, CONSTRAINT, SELECTION, INTERACTION, RENDER, FILE, PERFORMANCE.
  `ScopedTimer` logs kernel/tessellation/recompute/file timings at debug level.

## Geometry adapter (`geometry/`)

- `Shape` is an immutable, shared, opaque handle. Its topology index maps
  (faces/edges/vertices) are built once. Indices are **0-based and only valid
  for that Shape instance**.
- `Modeling.h`: box, cylinder, push/pull of a planar face (prism + fuse/cut +
  `ShapeUpgrade_UnifySameDomain`), fillet, chamfer, shell, booleans,
  transforms, direct face edits, measurements (volume, area, optimal bounding
  box, `measure`: distance / parallel gap / angle), face/edge info, BRep
  (de)serialization. Every call is wrapped in `guarded()` (catches
  `Standard_Failure`) and results pass `finishSolid()` (unwraps single solids,
  rejects empty results, runs `BRepCheck_Analyzer`, and adds a `Result`
  warning when the result is in several pieces).
- **Kernel "success" is verified.** OCCT sometimes reports success with an
  unchanged or wrong result, so operations check a cheap invariant of their
  intent: `shell` must remove volume; `deleteFaces` (`BRepAlgoAPI_Defeaturing`)
  must change the shape; `offsetFace` (`BRepOffset_MakeOffset` with one face
  offset; the skin-mode result is a shell, closed into a solid) must change
  the volume by about area × distance (within 25 %), otherwise the
  neighbours could not follow (e.g. tangent fillets) and the edit is refused.
- Rigid motions: `RigidMotion` (`Shape.h`: rotate about the axis through
  `center`, then translate) and `transformed`; `mirrored`; `mirrorJoined`
  and `repeatJoined` fuse the original and all copies in one General Fuse
  pass (`fuseInOnePass`; pairwise fusing is quadratic).
- Align: `alignFrame(shape, kind, index)` reduces a face or edge to a point
  and a direction (flat faces: centroid + outward normal, *sided*; straight
  edges: midpoint + direction; circles, cylinders, cones: center + axis).
  `alignMotion(source, target, flip, offset)` is the motion that brings one
  onto the other: sided pairs end up touching, facing each other; others
  become parallel with the smaller rotation.
- `FaceInfo` has `point` (a point on the face where `normal` is taken; a full
  cylinder's centroid lies on its axis, off the face) and, for cylinders and
  cones, `axisOrigin` / `axisDirection` / `radius`.
- Bounding boxes: `boundingBox()` is the tight (optimal) box, computed once
  per Shape and cached in `ShapeData` (it costs tens of ms on curved parts);
  `approximateBoundingBox()` is a conservative microsecond box for camera
  fitting, mesh resolution and scale normalization.
- `facesChangedBy` / `facesCreatedBy(before, after, current)`: which faces of
  the current shape a step created or changed, by kernel identity (and, for
  "created", new surface objects). Drives the Model panel highlight.
- `Tessellation.h`: `BRepMesh_IncrementalMesh` (faces meshed in parallel) →
  `Mesh` with per-triangle face ids, contiguous per-face triangle ranges (`faceTriangleOffset`) and per-edge
  polylines taken from the triangulation (so edges sit exactly on mesh vertices).
- `pushPullFaceKeepingEdges`: push/pull that takes fillets and chamfers
  along: split the part on a plane just below the face's non-wall
  neighbours, move the top piece, fill the gap with the extruded
  cross-section (or take out a slab), fuse; everything the moving band
  passes through must be a wall and the volume must change by exactly
  cross-section x distance, else `ErrorCode::Unsupported` and the caller
  uses `pushPullFace`.
- `offsetCurves` (Profiles.h): offsets one connected chain of planar curves
  (`BRepOffsetAPI_MakeOffset`, sharp corners) for the sketch Offset action.
- `pointOnFace` (a point inside a flat face, away from holes) and
  `faceThickness` (distance to the parallel face straight behind, by a line
  intersection with `IntCurvesFace_ShapeIntersector`) back push/pull's size.
- `Profiles.h`: planar curves (lines, circles, counter-clockwise arcs) →
  regions (see Sketches).
- `TopoSignature.h`: interim topological naming (see below).
- `Exchange.h`: STEP AP214 import/export, binary/ASCII STL export.

## Document model (`document/`)

```
Document (UUID, display unit)
 └─ Body (UUID, name, visible)
     └─ Feature history: Box → PushPull → Fillet → Chamfer → …
```

- A `Feature` is a pure function `compute(inputShape) → Result<Shape>` of its
  parameters and the body's shape before it. Base features (Box) ignore input.
- `Body::recompute(fromIndex)` re-evaluates from the first changed feature,
  reusing cached outputs before it. Evaluation **stops at the first failure**:
  the failed feature is `Failed` (with user/developer messages), later ones are
  `NotComputed`, and the body shows the last good shape. Nothing is deleted.
- Features expose editable scalar `parameters()` (e.g. box width, push/pull
  distance, fillet radius, pattern count) — the basis for history editing.
- Feature kinds (`FeatureKind`, stored by name): Box, and Extrude / Revolve
  (base features when they make a new body); PushPull, Fillet, Chamfer,
  Shell, Hole (drilled at a circular rim), Move (a translation plus an
  optional rotation: Rotate and Align steps are Moves), Combine (with a tool
  body), Mirror and Pattern (copies joined into the body), DeleteFaces and
  OffsetFace. Planes, axes and directions are stored as geometry, not as
  references; only faces/edges (`FaceRef` / `EdgeRef`), sketches and tool
  bodies are references. So an Align or Mirror step does not follow the face
  it was aimed at when that face moves later.
- A successful step can carry a `FeatureState::note` (from `Result`
  warnings, e.g. "The body is now in 2 separate pieces."); the Model panel
  shows it in amber.
- `Document::preview(body, feature)` evaluates a feature without mutating
  anything; interactive previews use it.
- `shapeRevision()` changes whenever a body's shape changes; views use it to
  know when to re-tessellate and when topology indices are stale.

## Sketches (`sketch/`, `document/SketchProfiles`)

- A `Sketch` (UUID) lives on a `Plane` (origin + orthonormal x/y axes) and holds
  points, lines, circles, arcs (counter-clockwise from start to end around
  the center; PlaneGCS `Arc` plus its arc rules) and constraints under
  per-sketch integer ids (id 1 is the fixed origin). Positions are always the
  last solved state. Curves flagged `construction` never become profiles.
- Constraints: coincident, horizontal, vertical, distance, horizontal/vertical
  distance (signed), diameter, radius (arcs), parallel, perpendicular, equal
  (lengths or radii), tangent (line or round to round), concentric, point on
  line, point on circle, midpoint. A line and an arc tangent at a shared end
  are solved as a direction (angle constraint), not "line touches circle".
  `sketch/SketchEdit` holds the edits behind tools that change geometry:
  `addSlot`, `filletCorner` (keeps the corner as a reference point on both
  lines), `trimAt` / `trimPreview` (pieces between crossings; new ends kept
  on the curves they meet). `solve()` / `solveDragging()` build a PlaneGCS
  system per call (DogLeg), write positions back, and report DOF plus
  conflicting/redundant constraints. A failed solve never changes the sketch.
- Profiles: `geom::findRegions` splits a large face on the sketch plane with
  all sketch curves via OCCT's General Fuse (`BRepAlgoAPI_Splitter`) and keeps
  the bounded pieces — this handles nesting, crossings and dangling lines
  without our own arrangement code. `ProfileRef` = an interior point (sketch
  coordinates) plus area; it resolves to the region containing that point.
- A sketch created on a face stores an `Attachment` (body, feature whose
  output holds the face, face signature). `effectivePlane()` re-resolves the
  face during recompute (only from features *before* the one being
  evaluated), and `Document::syncSketchAttachments()` writes the resolved
  plane back after every change, so sketches ride along with their faces.
- `ExtrudeFeature` references a sketch UUID and profile refs; mode NewBody
  (base feature), Join or Cut; cuts can be "through all". `Feature::compute` receives an `EvalContext`
  for such lookups, `Feature::dependencies()` declares them, and
  `Document::replaceSketch` recomputes dependent bodies. Deleting a sketch that
  a feature uses is refused.

## Topological naming (interim strategy)

See [docs/TOPOLOGICAL_NAMING.md](docs/TOPOLOGICAL_NAMING.md). In short:
feature references to faces/edges store a **topology index hint plus a
geometric signature** (surface/curve kind, normal/tangent, centroid/midpoint,
area/length). On recompute the hint is accepted only if it still matches the
signature exactly; otherwise the best-scoring candidate is chosen, and
resolution fails loudly (feature marked failed) rather than guessing wildly.
Selections use the same mechanism so a pushed face stays selected.

## Commands (`commands/`)

`Command::execute()` (also used for redo) and `undo()`. Commands hold UUIDs,
never pointers, so they survive objects being destroyed and recreated.
`execute()` must leave the document untouched on failure — e.g.
`AddFeatureCommand` removes the feature again if it fails to compute, so a
too-large fillet never enters history. `UndoStack` discards the redo branch on
a new command, tracks the clean state for "unsaved changes", and caps depth.

## Interaction (`interaction/`)

`InteractionController` owns the camera, the tessellation cache
(`SceneCache`), hover state, the `SelectionSet` and the active `Operation`.
It consumes framework-neutral events (`PointerEvent` with device kind
Mouse/Touch/Pen, `Key`, value text) and produces a `RenderScene` plus UI state.

- **Pointer state machine:** press → *pending*; moving past a device-dependent
  threshold turns it into orbit (left/right drag, 1-finger), pan (middle or
  Shift+left, 2-finger), or a manipulator drag (press on the arrow).
  Release without moving is a click (select).
- **Selection:** faces and edges (edges win within tolerance; hidden edges are
  rejected by a depth test against the visible face). Mouse click replaces,
  Shift/Ctrl adds; touch and pen taps are additive by default (no modifier
  keys on tablets); tapping empty space clears. Double-click selects the body.
  `InputProfile` gives touch 3× larger pick/grab tolerances.
- **Push/pull shows the size:** `PushPullOperation` places its arrow on the
  face (`geom::pointOnFace`: a washer's centroid is in its hole) and measures
  the part behind it (`geom::faceThickness`: a line into the material must
  leave through a parallel flat face). Then its value is that thickness
  (label Height/Width/Depth by the face's axis, else Thickness): typed and
  dragged values set it, `makeFeature` stores the difference as the PushPull
  distance, and the render scene gets a `Measure`-style line from the
  opposite face to the face. Otherwise the value is the distance moved.
- **Operations:** selecting one planar face arms `PushPullOperation`; selecting
  edges of one body arms `EdgeOperation` (fillet, switchable to chamfer). An
  operation owns a `LinearManipulator` (arrow), a value, a live preview mesh
  and an error. Dragging maps the pointer ray to the arrow axis (closest-point
  between lines, with a screen-space fallback when the axis faces the viewer)
  and snaps to zoom-dependent increments (1/2/5×10ⁿ mm; Alt disables).
  Typing sets exact values. Enter or clicking elsewhere commits a command;
  Esc clears the value, a second Esc clears the selection.
- **Camera:** orbit about the point under the cursor, pan and zoom keep the
  point under the cursor fixed, animated standard views and fit.
- **Sketch mode:** `startSketch()` creates a sketch on the selected planar face
  (host body recorded) or the XY plane, animates the view to face it, and hands
  input to a `SketchSession`. The session edits a working copy; tools are
  Select, Line, Rectangle, Circle, Arc (3-point: start, end, then bend; a
  typed radius locks it), Slot (two centers, then the width) and Trim (click
  a piece, previewed red). Selected curves offer Offset (a mode: the pointer
  picks the side, a typed distance fixes it, click/Enter applies); selected
  corner points offer Fillet. Starting a sketch on a plane where a visible sketch
  already lies (exactly coplanar), or with one of its profiles selected,
  reopens that sketch instead, so new curves split its shapes. Selecting
  sketch items offers constraint and Construction actions (`contextActions`).
  Snapping order: existing points (incl.
  origin) → line midpoints → horizontal/vertical inference relative to the
  shape start → zoom-dependent grid. Inferred H/V becomes a constraint only
  when shown during drawing. Typed values (width/height, diameter, length) lock
  the shape and become dimension constraints. Every completed action commits
  one `EditSketchCommand` (full before/after snapshots). Dragging a point runs
  the solver live. Undo that removes the sketch exits sketch mode.
- **Operations with several handles:** an `Operation` may expose several
  arrows (`handleCount()`); the grabbed one becomes active and receives drags
  and typed values. `MoveOperation` uses this for X/Y/Z (axis-colored).
  `RotateOperation` exposes three `RingManipulator`s instead (`ringCount()`:
  constant screen size, the angle unwrapped past ±180°, a screen-space
  fallback when a ring is seen edge-on; 15° snaps, Alt for 1°).
- **Operation hooks** (`Operation.h`): `prompt()` while a further pick is
  needed (Align's target, Mirror's plane); `labelAnchor()` for a value editor
  without an arrow; `neutralValue()` (what Esc returns to, e.g. a hole's
  current diameter, a push/pull's thickness); `neutralIsIdentity()` (the
  neutral value means no preview; Align previews at 0); `relativeBase()`
  (typed `+5` / `-5` are relative to it); `checkValue()` (refuse a value
  before any kernel call, e.g. a thickness of 0);
  `resetAutomaticChoices()` / `reconsider()` (revise an automatic choice once
  the preview is known); `canCommit()`; `clearPreview()`.
- **Face/body actions:** a single flat face arms Push/Pull and offers Shell,
  Sketch, Align and Delete face; a single cylindrical face (hole, shaft) arms
  Offset, typed as a diameter; several faces arm Shell. The Delete key on
  selected faces adds a DeleteFaces step. Edges arm Fillet (switchable to
  Chamfer; a hole rim also offers the heat-set insert; one edge offers Align).
  One body (double-click, or its Model-panel row) arms Move and offers Rotate,
  Mirror and Pattern (`BodyTool`). Two or more bodies offer Union / Subtract /
  Intersect, applied as one `CompositeCommand` (add `Combine` steps + hide the
  tool bodies); the first selected body is kept and Swap exchanges the two.
- **Align:** Align on a face or edge creates an `AlignOperation` that waits
  for a target on another body (`prompt()`), then previews at offset 0; the
  arrow adds an offset along the target, Flip reverses, "Onto ground" (flat
  faces) lays the face on the XY plane. It commits as a Move step named
  "Align": a one-time placement, not linked to the target.
- **Extrude options:** Symmetric makes the value the total thickness
  (`displayOffset` = value / 2); "Up to face" makes the next face click set
  the distance to a parallel flat face (`ExtrudeOperation::extendToFace`,
  stored as a plain distance). Push/pull steps from the UI set `keepEdges`.
- **Mirror / Pattern:** Mirror waits for a flat face (or an origin plane from
  the action bar) and has no value; Apply or Enter commits. Pattern previews
  right away (spacing = the body's extent plus 5 mm); the arrow sets the
  spacing (angle when circular), ± copy changes the count, and clicking an
  edge or a hole/shaft sets the direction or axis. Both commit one step with
  the copies joined into the body.
- **Profiles in model mode:** sketch regions are pickable (a region lying on a
  face wins over the face; a consumed sketch's region only when it is
  coplanar with the body face hit, so used sketches do not steal clicks);
  selecting profiles arms `ExtrudeOperation`, whose
  arrow follows the plane normal. For sketches on a body, pulling out joins
  and pushing in cuts, unless overridden (New body / Join / Cut). An
  automatic join whose preview would add separate pieces becomes a new body
  (`Operation::reconsider` revises automatic choices after a preview).
- **Touch:** `TouchGestureRecognizer` (Qt-free) turns touch frames into
  intents — one-finger pointer press/move/release and double-tap, two-finger
  pan/pinch once they move past a threshold, quick two/three-finger taps as
  undo/redo; `ViewportItem` only converts `QTouchEvent`s. Pen mode (turned on
  by the first pen press, or the Pen switch) makes finger presses
  navigation-only. `AppController::touchMode` (on after a touch, off after a
  real mouse click, on from the start on iOS/Android, `--touch` on the
  command line) makes `Theme.controlHeight` 44 and shows the Pen switch.
- **Buttons:** only a left click (or tap) selects and applies a pending value;
  right/middle drags orbit/pan and their clicks do nothing in 3D. In sketch
  mode a right click acts like Esc (ends the line chain, then leaves the tool).
- **Tools and actions:** `contextActions()` lists what the selection offers;
  the UI shows them in the value chip while a manipulator is active and in the
  selection action bar otherwise (`barAction_<id>` object names, used by the
  acceptance run). `runTool(id)` backs the Modify/Combine palette (ids:
  pushpull, fillet, chamfer, shell, offset, move, rotate, mirror, pattern,
  align, union, subtract, intersect, measure): it runs the tool when the
  selection fits and otherwise explains what to select.

## Rendering (`render/`, `ui/ViewportItem`)

`ViewportItem` is a `QQuickRhiItem`, so the viewport is part of the Qt Quick
scene (QML overlays compose naturally) and runs on whatever backend Qt Quick
uses (Direct3D 11 on Windows by default, Vulkan/Metal/OpenGL elsewhere).
`ViewportRenderer` copies a `RenderScene` in `synchronize()` (GUI thread
blocked) and draws with 4× MSAA:

1. bodies (lit, two-sided shading), keyed by mesh key so unchanged bodies are
   never re-uploaded;
2. adaptive grid + X/Y/Z axes through the origin (depth-tested, no depth
   write; the orientation marker is QML: `AxisTriad` from
   `InteractionController::axisTriad()`);
3. face highlights (hover, selection, and the orange Model-panel highlight
   with adjacent ranges merged) as **index sub-ranges of the body mesh** (no
   extra buffers);
4. edges as screen-space expanded quads (constant pixel width, depth bias);
5. sketches: profile fills (cached meshes), curves batched per style, point
   markers as zero-length line quads; the sketch being edited draws on top;
6. manipulator arrows and rotation rings on top (no depth test), sized in
   screen pixels.

Sketch labels (dimensions, live inputs, inference hints) are QML items
positioned from `SketchSession::labels()` screen coordinates.

Shaders are GLSL 440 compiled by `qt_add_shaders` into `.qsb` packages.
All draws share one dynamic uniform buffer with per-draw offsets.

## History panel

`InteractionController::historyRows()` flattens sketches, bodies and each
body's features into rows (name, detail, status — ok, warning, failed,
blocked, suppressed — explanation, editable length parameters). Hovering a row
calls `setHistoryHighlight(id)`: bodies and base features highlight the whole
body, other steps their new faces (`facesCreatedBy`, falling back to
`facesChangedBy`), sketches draw highlighted even when hidden. Clicking a
body row calls `selectBody(id, additive)`. The QML `HistoryPanel` edits values through
`setFeatureParameter`, which pushes a `SetParameterCommand` in *keep-failed*
mode: an edit that breaks a later step is kept, the step is marked failed
with its user message, and undo restores the value. Base features cannot be
deleted or suppressed; sketches used by features cannot be deleted.

## Files (`io/`)

`.openshape` = ZIP: `document.json` (source of truth), `metadata.json`,
`geometry/<body>.brep` (cache), optional `thumbnail.png`. Versioned with a
migration table; newer versions are refused with a clear message. Readers
treat files as untrusted: size limits, entry-name validation (no traversal),
strict JSON schema checks, duplicate-UUID rejection; nothing is extracted to
disk. Saves are atomic (temp file + rename). See
[docs/FILE_FORMAT.md](docs/FILE_FORMAT.md).

## Testing

- GTest suites (`tests/`): core (units, UUID, math), geometry (measurable
  invariants: volumes, bounding boxes, face counts), document/commands/files,
  camera/picking/interaction including a **headless Milestone 0 script**.
- `OpenShape --acceptance <dir>` (CTest `acceptance_gui`, label `gui`) drives
  the real application through Qt's platform input path — including clicking
  QML buttons found by `objectName` — and checks geometry after each step,
  saving screenshots. 147 checks, ~30 s (it moves the real mouse cursor):
  help card, the Milestone 0 script, save/open, exports, the Milestone 1
  bracket, a history edit, booleans through the Model panel and the action
  bar, Align, Rotate rings, Pattern, Mirror, two-/three-finger taps and the
  touch layout, the About box, trim/slot/fillet/offset in a sketch,
  symmetric and up-to-face extrusions, a fillet carried by a push, a hole
  resized by its diameter and deleted. `clickItem` lays out freshly created
  buttons before clicking (a click once landed on the Delete button that
  still sat where Fillet was about to go).
- `tools/bench/bench_session.cpp` (`-DOPENSHAPE_BUILD_TOOLS=ON`) times drag
  previews, tessellation, recompute and bounding boxes on a filleted part;
  `scripts/dev/` has a Win32 input driver and a live log watcher (see
  BUILDING.md, "Developer tools").

## Builds and releases

- **Version:** one number, `project(OpenShape VERSION x.y.z)` in the top-level
  `CMakeLists.txt`. `core/Version.h` is generated from it (`os::kAppVersion`,
  used by the app's `--version`, the About card, the log and project files'
  `metadata.json`); the macOS/iPad bundle and the Windows version resource
  take it from CMake too.
- **Windows resources:** `src/app/openshape.rc.in` (icon
  `resources/icons/openshape.ico`, made from the SVG by
  `scripts/windows/make-icon.py`, and VERSIONINFO) is configured and compiled
  by windres on Windows only (`enable_language(RC)` there). Warning flags
  apply to C++ sources only (`cmake/CompilerWarnings.cmake`).
- **Two OpenCASCADE builds on Windows:** the dev preset `msys2-ucrt64` and CI
  use MSYS2's package; the release preset `msys2-ucrt64-release`
  (`OPENSHAPE_OWN_OCCT=ON`) uses OpenShape's own build of the same version
  (`scripts/windows/build-occt.sh`: same source and MSYS2 patches, only the
  toolkits OpenShape links, no FFmpeg/FreeImage/TBB/VTK/Tcl/OpenGL), found in
  `$OPENSHAPE_OCCT_PREFIX` (default `%USERPROFILE%\opt\occt-7.9.3-openshape`).
  MSYS2's package links a GPL FFmpeg into the STEP translator's
  dependencies, so only the release build can be distributed.
- **Package → release files:** `scripts/package-windows.sh` (windeployqt plus
  one recursive `ntldd` scan of the entry points; generates
  THIRD_PARTY_LICENSES.txt with each library's license text and source
  location) → `scripts/windows/license-gate.sh` (traces every packaged file to
  OpenShape's build or texts, the own OCCT build or an MSYS2 package, and
  fails on GPL-licensed ones; mandatory for release builds) →
  `scripts/windows/make-installer.sh` (NSIS installer from
  `packaging/windows/openshape.nsi`, portable zip, SHA256SUMS.txt) →
  `scripts/windows/test-installer.ps1` (silent install, upgrade, uninstall,
  checks) and `scripts/windows/test-installer-dialogs.ps1` (the same through
  the dialogs, clicked by UI Automation; both only accept a test build of
  the setup, whose desktop shortcut goes to a test folder). `.github/workflows/release.yml` runs the whole chain and
  publishes tags `v*` as GitHub Releases.

## Known architectural limits (tracked in docs/TECHNICAL_DEBT.md)

- Tessellation and previews run synchronously on the GUI thread.
- Picking is brute force (no BVH).
- Only linear per-body history. Features may depend on sketches and (Combine)
  on other bodies; `Document::recomputeDependents` propagates changes and
  `dependsOn` prevents cycles.
- QRhi comes from `Qt6::GuiPrivate`: binaries are tied to the Qt version.
