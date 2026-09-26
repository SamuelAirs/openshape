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
 io/        .openshape project container (ZIP + JSON), recovery copies, recent-files list
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
| `openshape_io` | document, libzip (private) | no | project files, recovery store, recent-files list |
| `openshape_render` | interaction, Qt Gui/GuiPrivate/Quick | yes | QRhi renderer + shaders |
| `openshape_uistate` | io, Qt Core | yes | settings (`AppSettings`), recovery sessions (`RecoverySession`) |
| `openshape_ui` | render, io, uistate, Qt Quick/Controls | yes | QML module `OpenShape` |
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
- `Exchange.h`: STEP AP214 import/export, binary/ASCII STL export. Both STEP
  directions go through an XCAF document (`STEPCAFControl_*`): export writes
  one product per body named after it (each body wrapped in a compound of
  its own, so bodies sharing a kernel shape stay separate and a moved body
  is no assembly), in mm, inches or meters (`StepWriteOptions`); import
  flattens assemblies with their placements, names each solid after its
  product (a part without a name takes its assembly's; OCCT's placeholder
  "Open CASCADE STEP translator ..." is no name), converts any unit to mm,
  closes closed shells into solids, repairs damaged solids with ShapeFix
  (or skips them) and reports open surfaces and curves as warnings. Curves
  on round faces (cylinders, cones, spheres, tori) are rebuilt from the 3D
  edges: the file's are rounded (OCCT writes 13 digits) and made the first
  push/pull on an imported fillet produce unorientable faces.
- **Booleans never modify their arguments** (`runNonDestructive`,
  `SetNonDestructive`): OCCT otherwise updates argument shapes in place, and
  Shapes are shared and immutable (cached step outputs, imported geometry).

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
  body), Mirror and Pattern (copies joined into the body), DeleteFaces,
  OffsetFace, Split and SplitPiece (below), Copy (a base feature: another
  body's current shape mirrored or moved — Mirror / Pattern with "Separate
  bodies") and Imported (a base feature holding a STEP-imported solid's
  exact geometry; projects store it in `imports/`, see Files). Planes, axes and directions are stored as geometry, not as
  references; only faces/edges (`FaceRef` / `EdgeRef`), sketches and tool
  bodies are references. So an Align or Mirror step does not follow the face
  it was aimed at when that face moves later.
- A successful step can carry a `FeatureState::note` (from `Result`
  warnings, e.g. "The body is now in 2 separate pieces."); the Model panel
  shows it in amber (unless a later Split step dealt with the pieces).
- **Split into bodies** (`cmd::makeSplitBodyCommand`, one undo step): a
  `Split` step keeps the body's largest piece and records every piece's
  `geom::SolidSignature` (volume, centroid, box); each other piece becomes a
  new body whose base `SplitPiece` step takes piece *k* from the parent's
  shape just before that Split step. Both use `SplitFeature::assign`
  (`geom::matchSolids`: the kept piece picks first, then the closest pairs,
  rejecting pieces that moved more than their size), so they always agree.
  Pieces that appear upstream later stay in the parent; a piece that is gone
  (the body is whole again) fails with a message. `SplitPiece` depends on the
  parent body, so `recomputeDependents` (transitive) carries upstream edits
  — a sketch dimension, a tool body — through the parent to every piece.
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
  line, point on circle, midpoint, symmetric (two points across a line: the
  only kind with a third entity, `c`), angle (between two lines: stored as
  the signed angle between their directions, shown and edited as the angle
  at their corner). A line and an arc tangent at a shared end
  are solved as a direction (angle constraint), not "line touches circle";
  two arcs tangent at a shared end likewise tie their end angles (equal, or
  half a turn apart for an S-bend), never circle-to-circle tangency.
  `sketch/SketchEdit` holds the edits behind tools that change geometry:
  `addSlot`, `filletCorner` (keeps the corner as a reference point on both
  lines), `trimAt` / `trimPreview` (pieces between crossings; new ends kept
  on the curves they meet; a construction curve that only touches — a
  polygon's inner circle — is no crossing), `addCenterRectangle`, `addPolygon`,
  `mirrorCurves` (copies held by `Symmetric` constraints across a line;
  points on the axis are shared and kept on it) and `patternCurves` (linear
  or circular copies with their shape constraints; circles and arcs keep an
  Equal radius to the original). `solve()` / `solveDragging()` build a PlaneGCS
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

`DuplicateBodyCommand` makes an independent copy ("<name> copy"): the
history is cloned with fresh ids (`Feature::cloneWithNewId`), and what
belongs to that history alone is copied too — the sketches its steps use
(copied hidden, so they do not sit on the source's) and the tool bodies its
Combine steps consumed (copied hidden, recursively) — then every reference is
re-pointed at the copies (`Feature::remapReferences`, sketch attachments and
host bodies). Editing the copy (a step, its sketch, its tool) never changes
the source, nor the other way round. The other bodies a history builds on
(the parent of a split-off piece, the source of a mirror copy) stay shared.
What is copied depends on the kind of reference, never on visibility (a tool
shown again is still consumed; a hidden source is still shared). The copy's
id is fixed at construction so the UI can select it and redo recreates it.

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
  Select, Line, Rectangle, Center rectangle (the center is the midpoint of a
  construction diagonal, so it stays centered), Polygon (regular: corners on
  a construction circle and equal sides; an inner construction circle
  touching one side carries the size across flats; a `SketchCounter` shows
  the side count with -/+ buttons), Circle, Arc (3-point: start,
  end, then bend; a typed radius locks it), Tangent arc (starts on the end
  of a line or arc, tangent to it, and chains on from its own end), Slot
  (two centers, then the width) and Trim (click a piece, previewed red);
  the tools sit in a palette on the left (`tool_<id>` object names). Selected curves offer Offset (a mode: the pointer
  picks the side, a typed distance fixes it, click/Enter applies); selected
  corner points offer Fillet; selected curves also offer Mirror (the next
  click on a line mirrors them, previewed while hovering) and Pattern (a
  mode: Linear / Circular, clicks set the step or the center, typed
  spacing/angle and count, a -/+ counter, Enter or Apply adds the copies). Starting a sketch on a plane where a visible sketch
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
  fallback when a ring is seen edge-on; 15° snaps, Alt for 1°). Clicking a
  straight edge (of any body) or a hole/shaft makes it the axis — one ring around it, its
  direction's largest component positive; clicking an edge near its end (a
  corner) or a circular edge moves the X/Y/Z rings' pivot there; "Center
  pivot" goes back. The typed or dragged angle is kept. It is stored as the
  usual Move step with a rotation center and axis (no file format change).
  A ring is grabbed only once the pointer moves: a click on a ring activates
  it, except over an edge in Rotate, where it picks the edge (the rings
  cover edges near the body's center).
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
  Mirror, Pattern and Duplicate (`BodyTool`; Duplicate is also Ctrl+D and a
  button in the body's expanded Model-panel row; the copy comes out selected
  with the Move arrows, ready to drag away), and "Split into bodies" when it
  is in several pieces (also under the body's warning in the Model panel and
  on the expanded step that left the pieces; a committed step, or a Subtract
  or Intersect, that leaves new pieces says so in a message:
  `suggestSplit`). The Delete key deletes the selected bodies in one undo
  step (`deleteBodies`), except a body that others are built from
  (`Document::bodiesUsing`: split-off pieces, separate copies, bodies that
  consumed it as a tool): that one is hidden instead, with a message, and a
  hidden one cannot be deleted (its Model-panel row says why). Two or more bodies offer Union / Subtract /
  Intersect, applied as one `CompositeCommand` (add `Combine` steps + hide the
  tool bodies); the first selected body is kept and Swap exchanges the two.
  A body built from the other (a Copy or SplitPiece of it) cannot be its
  tool: Union and Intersect then keep the result in the copy instead.
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
  the copies joined into the body — or, with the "Separate bodies" option,
  one new body per copy (one undo step) whose base `Copy` step is the source
  body's current shape mirrored or moved, so the copies follow every later
  change of the source. Their preview shows the source and the copies side
  by side, unfused (`Operation::computePreview`); at most 100 copies.
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
  command line) makes `Theme.controlHeight` 44 and shows the Pen switch. The
  flag itself lives in `InteractionController::touchLayout()` (AppController
  only reads and sets it), so the on-canvas targets of the sketch session and
  the QML controls can never disagree.
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

Sketch labels (dimensions, live inputs, inference hints, constraint glyphs)
are QML items positioned from `SketchSession::labels()` screen coordinates.
Constraint glyphs (H, V, ∥, ⊥, =, T, …; not for dimensions, and hidden while
a shape is being drawn) sit beside their geometry, on the outer side of a
line, and slide along it to stay clear of each other, the dimension labels
and the points; a glyph with no clear spot nearby is left out until the view
is zoomed in. Dimension labels count as pills (about 7.5 px per character
plus padding, 24 px high), not points. The glyph items take no input: with
the Select tool `SketchSession` resolves a click or tap itself — a point or
curve within pick reach wins, and only then a glyph whose square target
(24 px, 40 px in the touch layout) holds the pointer — so a glyph never
steals a tap meant for the geometry beside it, and drawing tools ignore
glyphs. A glyph's center stays out of pick reach of points and curves, so
tapping the glyph itself always reaches it. Clicking one selects the
constraint (`constraintIcon_<id>`), never mixed with geometry, and its
geometry is highlighted; Delete removes it.

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
deleted or suppressed; sketches used by features cannot be deleted, and
neither can hidden bodies other bodies are built from (Delete on a shown one
hides it).

## Files (`io/`)

`.openshape` = ZIP: `document.json` (source of truth), `metadata.json`,
`imports/<step>.brep` (the geometry of Imported steps: source of truth,
also in recovery copies), `geometry/<body>.brep` (cache), optional
`thumbnail.png` (written on Save: `InteractionController::renderThumbnail`
draws the visible bodies' display meshes on the CPU — `interaction/Thumbnail`,
a z-buffered rasterizer with the viewport's lighting and edges, 2 x 2
samples per pixel, isometric and framed, independent of the current view,
the GPU and any window, so automated runs and iPadOS behave the same; the
UI encodes it as PNG. Measured: 18 ms for the 21-body, 1528-face model whose
full save takes 134 ms; `io::readProjectThumbnail` opens the ZIP directory
and reads only that entry, for the start screen). `documentFromJson` takes an `EntryReader` for the imports;
the loader checks each against the hash, validity and volume its step
recorded before any modeling sees it (the BRep text is read with stream
exceptions on: OCCT's reader looped forever on a cut-off text). Versioned with a
migration table; newer versions are refused with a clear message. Readers
treat files as untrusted: size limits, entry-name validation (no traversal),
strict JSON schema checks, duplicate-UUID rejection; nothing is extracted to
disk. Saves are atomic (temp file + rename). See
[docs/FILE_FORMAT.md](docs/FILE_FORMAT.md). `saveProject` is three steps
(`serializeProject` reads the document on the GUI thread;
`buildProjectArchive` and `writeFileAtomically` only use strings), so the
slow parts could move to a worker thread.

**Recovery copies** (the owner's choice: the user's file changes only on
Save). `io/Recovery` (Qt-free) stores, per running app instance (a
*session*), `<session>.openshape` (a project file without the geometry
cache) and a JSON sidecar (original path, title, time, app version) in
`<AppLocalData>/recovery/`; it lists copies, finds orphans through a
caller-supplied "is this session alive?" check, adopts and removes them.
`ui/RecoverySession` answers that check with `QLockFile`s: each instance
holds `<session>.lock`; a lock whose process is gone is stale, and taking it
also stops a second instance from offering the same copy. `AppController`
watches `UndoStack::revision()` (bumped by push/undo/redo/clear) on every
`stateChanged`: while `dirty()`, a copy is written 3 s after the last edit,
at least every `recoveryInterval` (Preferences; 0 = off), and at once when
the app stops being the active one (`applicationStateChanged`: another
window, or the iPad home screen, after which iPadOS may end the app). Save, New,
Open, undo back to the saved state and "Don't Save" (closing the window
calls `discardUnsavedWork()`) remove it. **The rule at exit:** the copy is
deleted only when the user let go of the work. `endRecovery()` (on
`aboutToQuit`, and from the destructor for exits that skip it, such as
iPadOS unwinding out of `exec()`) keeps it when the document still has
unsaved changes that were not discarded: it brings the copy up to date and
sets `RecoverySession::setKeepCopy`, so the session's destructor releases
the lock but leaves the files, and the next start offers them like a
crashed run's. Any exit that does not go through the window's close
question (iPadOS ending the app, Windows logging off, `QCoreApplication::exit`)
therefore keeps the work. At startup (not in automated runs)
`checkForRecovery()` fills `recoveryItems`, which `RecoveryOverlay.qml`
shows; Restore opens the copy as an unsaved document (`UndoStack::setModified`)
with its original path and adopts the file as this session's copy (the copy
is moved; its sidecar is written anew from what the prompt showed, so a
sidecar another program holds cannot lose the original path).
Measured (bench_session): a copy of a 21-body, 1528-face model takes about
8-10 ms on the GUI thread (a full save with the geometry cache: 234 ms), so
no worker thread is used.

**Settings** (`ui/AppSettings`, QSettings): preferences (default unit for
new documents, sketch grid snapping, recovery interval), recent files
(`io/RecentFiles`: most recent first; the menu shows the 10 newest that
exist, and a file that is gone never pushes an existing one out; the File
menu rereads the list as it opens, `refreshRecentFiles()`) and the
window's place (frame + client rectangle + maximized; restored by client
area and clamped to today's screens by `fitToScreens`). `main.cpp` points
QSettings at a temporary INI file (and recovery copies at a temporary
folder) for `--acceptance`, `--demo` and `--screenshot`, or at
`--data-dir`. `app/CrashLog` writes one log line on an unhandled exception
(Windows, with module + offset) or `std::terminate`.

**Import STEP** (File menu, Ctrl+I): `AppController::importStep` reads the
file (`geom::importStep`), `InteractionController::importBodies` adds one
body per solid (an `Imported` base step, unique names, "Imported 1" when
the file has none) as one `CompositeCommand` and fits the view; the message
says how many bodies came in and what was skipped. `importStepAsProject`
does the same into a new document (the current one stays if the file
cannot be read). Native file dialogs cannot be clicked by the acceptance
run: `AppController::setNextFileChoice` hands it the file, and
`window.chooseFile(dialog, accept)` runs the dialog's accept code with it.

**Home** (the start screen, `HomeScreen.qml`, z 90: over the model and
its panels, under dialogs and the restore prompt): `AppController::homeVisible`
is set by `main.cpp` at launch without a file (never in automated runs; the
`home` demo scene shows it) and by File → Home; New, Open, opening a
recent project, importing as a project and restoring a recovery copy clear
it (Esc and Back return to the open document). `homeProjects` lists the
recent files with name, folder, date and a preview source, and on iPadOS
also the projects in the app's Documents folder (`io::homeProjects`).
Previews come from `ui/ThumbnailProvider` (`image://thumbnail/<mtime>/<path>`,
loaded off the GUI thread with `io::readProjectThumbnail`; the time stamp
makes a re-saved project show its new preview). Cards are the tap
targets; ⋯, a long press or a right click open "Remove from list"
(`removeRecentFile`, `io::withoutRecentFile`). The grid takes as many
columns as fit (two on a phone in portrait) and gets denser in short
windows (a phone in landscape).

**Dialogs are overlays** in the window, not native message boxes (touch-sized,
clickable by the acceptance run): `UnsavedOverlay` (Save / Don't Save /
Cancel before New, Open, Open Recent, Restore and closing),
`RecoveryOverlay`, `PreferencesOverlay`, `AboutOverlay`, `HelpOverlay`.
While `UnsavedOverlay` or `RecoveryOverlay` is shown (`window.modalOpen`)
the window's shortcuts are disabled, as behind a native modal dialog, and
`UnsavedOverlay.ask()` ignores a second request: the pending action is the
one the user is being asked about.
Only file choosers stay native (`FileDialog`). After a menu or overlay
closes, `focusViewUnlessPanel()` gives the keys back to the view (Qt left
them on a hidden menu separator after the Open Recent sub-menu).

## Testing

- GTest suites (`tests/`): core (units, UUID, math), geometry (measurable
  invariants: volumes, bounding boxes, face counts), document/commands/files,
  camera/picking/interaction including a **headless Milestone 0 script**;
  `test_uistate` (Qt Core, no window): settings, window placement and
  recovery sessions with real lock files.
- `OpenShape --acceptance <dir>` (CTest `acceptance_gui`, label `gui`) drives
  the real application through Qt's platform input path — including clicking
  QML buttons found by `objectName` — and checks geometry after each step,
  saving screenshots. 147 checks, ~30 s (it moves the real mouse cursor):
  help card, the Milestone 0 script, save/open, exports, the Milestone 1
  bracket, a history edit, booleans through the Model panel and the action
  bar, Align, Rotate rings, Pattern, Mirror, two-/three-finger taps and the
  touch layout, the About box, trim/slot/fillet/offset in a sketch,
  symmetric and up-to-face extrusions, a fillet carried by a push, a hole
  resized by its diameter and deleted; scenarios `recovery` (a real crash
  of a second OpenShape via `--simulate-crash`, the restore prompt, and a
  second OpenShape ended with unsaved work via `--simulate-quit`),
  `recent`, `preferences` and `files` (Import STEP from the File menu, Ctrl+I
  and Home, the saved thumbnail, Home's cards, menu, long press and
  buttons). `clickItem` lays out freshly created
  buttons before clicking (a click once landed on the Delete button that
  still sat where Fillet was about to go).
- `tools/bench/bench_session.cpp` (`-DOPENSHAPE_BUILD_TOOLS=ON`) times drag
  previews, tessellation, recompute and bounding boxes on a filleted part;
  `scripts/dev/` has a Win32 input driver and a live log watcher (see
  BUILDING.md, "Developer tools").

## Known architectural limits (tracked in docs/TECHNICAL_DEBT.md)

- Tessellation and previews run synchronously on the GUI thread.
- Picking is brute force (no BVH).
- Only linear per-body history. Features may depend on sketches and (Combine)
  on other bodies; `Document::recomputeDependents` propagates changes
  transitively (a body that changed updates the bodies built on it in turn)
  and `dependsOn` prevents cycles.
- QRhi comes from `Qt6::GuiPrivate`: binaries are tied to the Qt version.
