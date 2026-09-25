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
 interaction/  InteractionController ── Operation (PushPull, Fillet/Chamfer, Extrude)
        │         │  camera, hover, selection, manipulators, previews
        │         ├─ SketchSession (tools, snapping, inference, typed dimensions)
        │         ▼
        │   selection/  picking (CPU ray/segment/profile), SelectionSet (+signatures)
        ▼
 commands/  Command + UndoStack (CreateBody, AddFeature, SetParameter, EditSketch…)
        ▼
 document/  Document → Sketches + Bodies → Feature history (Box, PushPull,
        │            Fillet, Chamfer, Extrude); SketchProfiles bridge
        ├──────────────────────────────┐
        ▼                              ▼
 geometry/  Shape, Modeling,       sketch/  Sketch model (points, lines, circles,
   Profiles, Tessellation,                  constraints) + SketchSolver
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
  `ShapeUpgrade_UnifySameDomain`), fillet, chamfer, booleans, transforms,
  measurements (volume, area, optimal bounding box), face/edge info,
  BRep (de)serialization. Every call is wrapped in `guarded()` (catches
  `Standard_Failure`) and results pass `finishSolid()` (unwraps single solids,
  rejects empty results, runs `BRepCheck_Analyzer`).
- `Tessellation.h`: `BRepMesh_IncrementalMesh` → `Mesh` with per-triangle face
  ids, contiguous per-face triangle ranges (`faceTriangleOffset`) and per-edge
  polylines taken from the triangulation (so edges sit exactly on mesh vertices).
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
  distance, fillet radius) — the basis for history editing (Milestone 4).
- `Document::preview(body, feature)` evaluates a feature without mutating
  anything; interactive previews use it.
- `shapeRevision()` changes whenever a body's shape changes; views use it to
  know when to re-tessellate and when topology indices are stale.

## Sketches (`sketch/`, `document/SketchProfiles`)

- A `Sketch` (UUID) lives on a `Plane` (origin + orthonormal x/y axes) and holds
  points, lines, circles and constraints under per-sketch integer ids (id 1 is
  the fixed origin). Positions are always the last solved state.
- Constraints: coincident, horizontal, vertical, distance, horizontal/vertical
  distance (signed), diameter. `solve()` / `solveDragging()` build a PlaneGCS
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
  Select, Line, Rectangle, Circle. Snapping order: existing points (incl.
  origin) → line midpoints → horizontal/vertical inference relative to the
  shape start → zoom-dependent grid. Inferred H/V becomes a constraint only
  when shown during drawing. Typed values (width/height, diameter, length) lock
  the shape and become dimension constraints. Every completed action commits
  one `EditSketchCommand` (full before/after snapshots). Dragging a point runs
  the solver live. Undo that removes the sketch exits sketch mode.
- **Operations with several handles:** an `Operation` may expose several
  arrows (`handleCount()`); the grabbed one becomes active and receives drags
  and typed values. `MoveOperation` uses this for X/Y/Z (axis-colored).
- **Face/body actions:** a single flat face arms Push/Pull and offers Shell
  and Sketch; several faces arm Shell. A body (double-click) arms Move; two
  bodies (Shift+double-click) offer Union/Subtract/Intersect, applied as one
  `CompositeCommand` (add `Combine` step + hide the tool body).
- **Profiles in model mode:** sketch regions are pickable (a region lying on a
  face wins over the face); selecting profiles arms `ExtrudeOperation`, whose
  arrow follows the plane normal. For sketches on a body, pulling out joins
  and pushing in cuts, unless overridden (New body / Join / Cut).

## Rendering (`render/`, `ui/ViewportItem`)

`ViewportItem` is a `QQuickRhiItem`, so the viewport is part of the Qt Quick
scene (QML overlays compose naturally) and runs on whatever backend Qt Quick
uses (Direct3D 11 on Windows by default, Vulkan/Metal/OpenGL elsewhere).
`ViewportRenderer` copies a `RenderScene` in `synchronize()` (GUI thread
blocked) and draws with 4× MSAA:

1. bodies (lit, two-sided shading), keyed by mesh key so unchanged bodies are
   never re-uploaded;
2. adaptive grid + X/Y axes (depth-tested, no depth write);
3. face highlights as **index sub-ranges of the body mesh** (no extra buffers);
4. edges as screen-space expanded quads (constant pixel width, depth bias);
5. sketches: profile fills (cached meshes), curves batched per style, point
   markers as zero-length line quads; the sketch being edited draws on top;
6. manipulator arrows on top (no depth test), sized in screen pixels.

Sketch labels (dimensions, live inputs, inference hints) are QML items
positioned from `SketchSession::labels()` screen coordinates.

Shaders are GLSL 440 compiled by `qt_add_shaders` into `.qsb` packages.
All draws share one dynamic uniform buffer with per-draw offsets.

## History panel

`InteractionController::historyRows()` flattens sketches, bodies and each
body's features into rows (name, detail, status, explanation, editable
length parameters). The QML `HistoryPanel` edits values through
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
  saving screenshots. It covers the Milestone 0 script and the Milestone 1
  bracket end to end (52 checks).

## Known architectural limits (tracked in docs/TECHNICAL_DEBT.md)

- Tessellation and previews run synchronously on the GUI thread.
- Picking is brute force (no BVH).
- Only linear per-body history. Features may depend on sketches and (Combine)
  on other bodies; `Document::recomputeDependents` propagates changes and
  `dependsOn` prevents cycles.
- QRhi comes from `Qt6::GuiPrivate`: binaries are tied to the Qt version.
