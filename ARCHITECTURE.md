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
        │         │   Pattern, Insert, Head, Hole)
        │         │  camera, hover, selection, manipulators (arrows, rings), previews
        │         ├─ SketchSession (tools, snapping, inference, typed dimensions)
        │         ├─ TouchGestureRecognizer (touch frames → pointer, pan/pinch, undo/redo)
        │         ▼
        │   selection/  picking (CPU ray/segment/profile, BVH per mesh), SelectionSet (+signatures)
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
 core/      Result, Log, Uuid, Units, Math, Camera, Lighting, Timer
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
- `Camera` (`core/Camera.h`): Z-up turntable, perspective by default (35
  degree vertical field of view: depth cues, so a box cannot flip in the
  viewer's mind), orthographic optional (the View buttons' toggle, remembered
  in QSettings `view/perspective`). Pure math shared by picking, interaction
  and rendering.
- `StudioLighting` (`core/Lighting.h`): the viewport's lighting model, one
  formula for the GPU (the renderer passes its numbers to `mesh.frag`) and
  the CPU (project thumbnails, `tests/test_lighting.cpp`). See Rendering.
- Logging (`core/Log.h`): categories APP, DOCUMENT, COMMAND, GEOMETRY, KERNEL,
  SKETCH, CONSTRAINT, SELECTION, INTERACTION, RENDER, FILE, PERFORMANCE.
  `ScopedTimer` logs kernel/tessellation/recompute/file timings at debug level.

## Geometry adapter (`geometry/`)

- `Shape` is an immutable, shared, opaque handle. Its topology index maps
  (faces/edges/vertices) are built once. Indices are **0-based and only valid
  for that Shape instance**. Booleans run non-destructive (`runBoolean`):
  by default OCCT widens tolerances of its inputs' sub-shapes in place, which
  changed cached step outputs under later steps (found by the undo/redo
  stress test). Meshing still writes triangulations into shapes, which
  other shapes share (a preview result shares most faces with its body):
  hence the kernel lock (see Threads).
- `Modeling.h`: box, cylinder, push/pull of a planar face (prism + fuse/cut +
  `ShapeUpgrade_UnifySameDomain`), fillet, chamfer, shell, booleans,
  transforms, direct face edits, measurements (volume, area, optimal bounding
  box, `measure`: distance / parallel gap / angle), face/edge info, BRep
  (de)serialization. Every call is wrapped in `guarded()` (catches
  `Standard_Failure`) and results pass `finishSolid()` (unwraps single solids,
  rejects empty results, runs `BRepCheck_Analyzer`, and adds a `Result`
  warning when the result is in several pieces).
- **Kernel crashes become failures.** OCCT can dereference null pointers on
  valid input (the stress test found a General Fuse that crashed in its solid
  classifier on blend corners with degenerate edges). The first kernel call
  installs OCCT's signal handlers (`OSD::SetSignal`, `SetUnhandled` mode, no
  floating-point traps); OCCT is built with `OCC_CONVERT_SIGNALS` (MSYS2,
  Homebrew, Linux), so an access violation jumps to the nearest
  `OCC_CATCH_SIGNALS`: OCCT's own algorithms then report a failed build, and
  our try blocks (`guarded()`, `OS_KERNEL_SIGNALS_TO_EXCEPTIONS` in
  `internal/KernelUtil.h`) rethrow it as a `Standard_Failure`. Locks stay
  outside those try blocks (a jump skips destructors).
- **Kernel "success" is verified.** OCCT sometimes reports success with an
  unchanged or wrong result, so operations check a cheap invariant of their
  intent: `shell` must remove volume; `deleteFaces` (`BRepAlgoAPI_Defeaturing`)
  must change the shape; `offsetFace` (`BRepOffset_MakeOffset` with one face
  offset; the skin-mode result is a shell, closed into a solid) must change
  the volume by about area × distance (within 25 %), otherwise the
  neighbours could not follow (e.g. tangent fillets) and the edit is refused.
- **Nothing fails silently or does nothing silently.** A subtraction must
  remove volume, a mirror or pattern must add some, an intersection must
  leave some: otherwise the operation fails with `ErrorCode::NoEffect` (or
  `EmptyResult`) and a message saying why ("This cut does not reach the
  body…", "The copies land on top of the original…"); features reword the
  kernel layer's generic text for their context (`reworded()` in
  `Feature.cpp`). A **new** step that would change nothing is refused
  (previews, `AddFeatureCommand`, a direct-manipulation `SetParameterCommand`);
  during history recompute such a step passes its input on as an `Ok` step
  with a warning note, so an upstream edit that moves a cut off the body, or
  a file from an older version, does not block the steps after it.
- **Refused sizes name one that works** (`geom::SizeAdvice`). Only
  interactive previews ask for it (`EvalContext::interactive`, set by
  `Document::preview`): a fillet, chamfer or shell the kernel refuses is then
  retried to find the largest size that works, and the message names it in
  the document's display unit ("The radius is too large for this edge. Try
  2.9 mm or less."). The search (`geom::detail::largestWorkingSize`) tries
  0.1 mm first (fails: "at any size"), then bisects geometrically while the
  bounds are far apart (a value typed 100x too large) and arithmetically
  after; at most 10 attempts, none that would end past 600 ms judged by the
  slowest attempt so far; if it stops before the bounds are within 1.5x it
  keeps the general wording. The last answer is cached for drags. History
  recompute and file loading use the general wording ("Try a smaller
  radius."), so a failing step costs no extra kernel attempts per rebuild.
  An edge between tangent faces is reported as having no corner to round.
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
  Meshing stores triangulations in the faces and polygons in the edges, and
  a preview result shares most faces and edges with its body: meshed in
  place, every preview left its polygons on the body's edges (a filleted
  cube's BRep text grew from 93 KB to 1.3 MB over 40 previews, and the
  project file's geometry cache with it). Previews are therefore meshed
  `isolated`: on a copy of the topology (`BRepBuilderAPI_Copy` without
  geometry, with the existing meshes, which the mesher reuses): 22 ms
  instead of 18 ms for the enclosure's rim preview.
- `pushPullFaceKeepingEdges`: push/pull that takes fillets and chamfers
  along: split the part on a plane just below the face's non-wall
  neighbours, move the top piece, fill the gap with the extruded
  cross-section (or take out a slab), fuse; everything the moving band
  passes through must be a wall and the volume must change by exactly
  cross-section x distance, else `ErrorCode::Unsupported` and the caller
  uses `pushPullFace`.
- `extrudeFacesDrafted` (Profiles.h, `DraftExtrude.cpp`): a straight prism
  whose side faces `BRepOffsetAPI_DraftAngle` tilts about the profile's
  plane (planes stay planes, cylinders become cones, corners stay sharp).
  Before any kernel work the far end is checked analytically: lines
  shorten by inset x tan(turn / 2) at each corner, circles and arcs around
  the material shrink by the inset, around holes they grow; an edge that
  would vanish refuses the draft with a plain message
  (`BRepOffsetAPI_MakeOffset` could answer this, but crashed in its medial
  axis on a square with a small round hole). After it, a positive draft
  must remove volume and a negative one add some.
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
- **Booleans never modify their arguments** (`runBoolean`,
  `SetNonDestructive`): OCCT otherwise updates argument shapes in place, and
  Shapes are shared and immutable (cached step outputs, imported geometry).
- `Holes.h`: `drillHoles` cuts any number of round holes in one boolean
  (`HoleCut`: entry point, direction, diameter, depth or through all, and a
  counterbore or countersink head; `drillShaft = false` cuts only the head
  on an existing hole, whose depth or through-all is then given). Tools are
  analytic cylinders and cones (the shaft starts inside the head, a
  countersink cone runs on past the hole's wall, halfway to a blind hole's
  bottom at most, so no faces coincide). Checked: sizes before any kernel call, then the
  cut must remove something and no more than the tools hold. `headVolume`
  is the exact ring or frustum a head takes from solid material;
  `materialDepth` measures along a line how much material follows a
  surface point, `emptyDepth` how much empty space follows a point (a
  hole's depth from its opening; 0 inside material). The side is read from
  the normal of the first face hit: `BRepClass3d_SolidClassifier` crashed
  inside Extrema on a plain holed plate.

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
  A recompute that starts after a failed step (editing a blocked step) keeps
  that last good shape too (it once left the body empty). A step that
  changes nothing (`ErrorCode::NoEffect`) is not a failure here: it is `Ok`
  with its input as output, `error` set and a `note` shown as a warning.
- Features expose editable scalar `parameters()` (e.g. box width, push/pull
  distance, fillet radius, pattern count) — the basis for history editing.
- Feature kinds (`FeatureKind`, stored by name): Box, and Extrude / Revolve
  (base features when they make a new body); PushPull, Fillet, Chamfer,
  Shell, Hole (at a circular rim: a plain cylinder such as a heat-set
  insert's pilot hole, or a counterbore / countersink for a screw head,
  whose exact ring or frustum volume is verified, and which measures the
  existing hole's depth first (`geom::emptyDepth`); sizes from
  `document/Fasteners`, the one place for screw and insert tables with
  their sources), Holes (the Hole tool: holes at points on a flat face,
  stored in the face's frame like a sketch on it, so they follow the face;
  diameter, depth or through all, optional counterbore / countersink), Move (a translation plus an
  optional rotation: Rotate and Align steps are Moves), Combine (with a tool
  body), Mirror (the image joined into the body; with `keepOriginal` off the
  body becomes its image: the last step of a mirror copy) and Pattern
  (copies joined into the body), DeleteFaces, OffsetFace, Split (below),
  SplitPiece and Copy (base features that follow another body: split-off
  pieces and "Separate bodies" copies in files from before independent
  copies; they still load and compute, the UI no longer makes them) and
  Imported (a base feature holding a STEP-imported solid's exact geometry;
  projects store it in `imports/`, see Files). Planes, axes and directions are stored as geometry, not as
  references; only faces/edges (`FaceRef` / `EdgeRef`), sketches and tool
  bodies are references. So an Align or Mirror step does not follow the face
  it was aimed at when that face moves later.
- A successful step can carry a `FeatureState::note` (from `Result`
  warnings, e.g. "The body is now in 2 separate pieces."); the Model panel
  shows it in amber (unless a later Split step dealt with the pieces).
- **Split into bodies** (`cmd::makeSplitBodyCommand`, one undo step): a
  `Split` step keeps the body's largest piece and records every piece's
  `geom::SolidSignature` (volume, centroid, box); each other piece becomes a
  new, independent body: a copy of the body's history
  (`DuplicateBodyCommand`, with its own hidden copies of the sketches and
  consumed tools it uses) ending in a `Split` step that lists that piece
  first, so it keeps that piece. Pieces are found by `SplitFeature::assign`
  (`geom::matchSolids`: the kept piece picks first, then the closest pairs,
  rejecting pieces that moved more than their size). Pieces that appear
  upstream later stay in the body whose history grew them; editing one
  piece (or the body it came from) never changes another. Files from
  before independent pieces hold `SplitPiece` bodies (piece *k* of the
  parent's shape just before its Split step, following every upstream edit
  through `recomputeDependents`); they still load and compute as before.
  Each piece of an imported body stores its geometry again, so a split that
  would take the project beyond `Document::importedGeometryLimit` is
  refused before anything changes (see `DuplicateBodyCommand`).
- `Document::preview(body, feature)` evaluates a feature without mutating
  anything; interactive previews use it. `Document::snapshot()` copies the
  bodies (histories, cached step results, shapes shared: they are
  immutable) and sketches for the preview worker (0.04 ms for a 21-body
  model).
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
  a feature uses is refused. `recomputeDependents` follows dependencies
  transitively (A combines with B, B with C: editing C updates B, then A),
  so the order of bodies in a file does not matter; a cycle from a hostile
  file stops after a bounded number of recomputes per body.

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
the source, nor the other way round. A body from before independent copies
whose base step follows another body (a `Copy`, a `SplitPiece`) is copied
without that link: the base step is replaced by the other body's history
(resolved the same way; up to its Split step for a piece) and the step that
made it (a Mirror keeping the image, a Move, a Split keeping the piece), so
the copy follows nothing; only when that body cannot be built up to there
does the copy keep the old step (shared, like before).
What is copied depends on the kind of reference, never on visibility (a tool
shown again is still consumed; a hidden source is still shared). The copy's
id is fixed at construction so the UI can select it and redo recreates it.
The same command makes Mirror and Pattern copies and split-off pieces (a
name and one more step after the cloned history: a Mirror step keeping only
the image, a Move step, a Split step; `makeCopyBodiesCommand` groups several
into one undo step) and refuses when that step fails or changes nothing.
The cloned steps **take over the source's results** (`Body::adoptResults`,
`Document::addBody`'s `computeFrom`: the shapes are shared, only the added
step is computed): 10 pattern copies of a 14-step body took 18 ms instead of
about 3.3 s (333 ms per recompute of that history). A resolved legacy
history differs from the source's, so it is computed in full.
Each copied Imported step is one more `imports/` entry in the project: the
command refuses (plainly, changing nothing) a copy that would take the
document beyond `Document::importedGeometryLimit` (512 MiB, what a project
can save; lower only in tests), and `cmd::checkImportedCopiesFit` /
`DuplicateBodyCommand::importedBytesOfCopy` let Mirror, Pattern and Split
check before they start.

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
- **Picking acceleration:** `SceneCache` builds a `sel::PickAccelerator`
  with every body mesh (two bounding-volume hierarchies: triangles and edge
  segments, median splits, 4 items per leaf; ~9 ms for 25k triangles). Face
  picking walks the triangle tree for the nearest hit (ties go to the lowest
  triangle index, like the linear scan); edge picking collects the segments
  whose boxes come within the pixel tolerance of the pick line (the
  tolerance times the pixel size at the box's far side, so the cull is
  conservative) and runs the unchanged per-segment logic on them in mesh
  order. Results are identical to the linear scan, which remains the path
  for targets without an accelerator (tests compare the two). The point of
  an edge under the cursor is interpolated perspective-correctly (1/depth
  is linear on screen): taken linearly, a visible long edge failed the
  occlusion test in perspective. The ray-triangle test has a 1e-9
  barycentric tolerance, so a ray through the diagonal two triangles share
  (a face's center) hits one of them. An unused sketch profile clearly in
  front of a picked edge wins over the edge.
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
  point under the cursor fixed, animated standard views and fit. In
  perspective the wheel and pinch zoom head for the surface under the
  pointer (`InteractionController::zoomAt`: the target first moves along the
  view axis to the picked depth, which leaves the image unchanged), so
  zooming in approaches that surface and never passes through it. Sketches
  keep the projection: the view faces the sketch plane head-on, which
  perspective shows undistorted.
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
- **Previews off the GUI thread** (`Operation::setValue`, `PreviewWorker`):
  `setValue` checks the value at once (`checkValue`, the neutral value),
  then - when the operation has a `PreviewScheduler` (the app; headless
  tests stay synchronous) - hands the worker a self-contained job: a
  `clone()` of the operation and a `Document::snapshot()` (cached per
  document and undo revision), and returns. The job runs on the single
  preview worker thread exactly what the synchronous path runs:
  `resetAutomaticChoices`, `computePreview` (`makeFeature` +
  `Document::preview`), `reconsider`/`reconsiderRefusal`, `tessellate`.
  The latest request wins: a newer value replaces a job that has not
  started; a running one finishes. Its result comes back as a queued call
  (`InteractionController::deliverPreviews`, which the UI calls when the
  worker's notify arrives) and `Operation::acceptPreview` decides: another
  operation's, or older than the shown one or than a reset (Esc, a sync
  refusal) or a parameter change - dropped; the newest request's - shown
  with its error and automatic choices (`adoptAutomaticChoices`); a
  superseded value's - shown if it worked (the preview keeps up during a
  drag), its error dropped. Only drag steps and typed values
  (`setValue(..., Change::ValueOnly)`) let earlier results keep up; every
  other `setValue` (the setters: mode, Through all, count, preset, target)
  is a parameter change, after which an earlier request's result is the
  old geometry and is dropped. The last good preview stays on screen
  meanwhile; `previewMeshBody()` says which body it stands in for.
  `canCommit()` counts a pending preview as committable (`previewUsable()`):
  the command computes the step again; commit waits for it only when an
  automatic choice depends on it (`commitNeedsPreview`: an extrusion that
  becomes a new body), and drops a job that has not started. When the
  command refuses a value whose preview had not come back, that refusal
  becomes the value's verdict (`refusePendingValue`: the message in the
  value chip, no preview, nothing left pending), as a refused preview's
  would be; the tool stays as it was (one-shot resets - Align, Mirror and
  Pattern back to Move, the Hole tool's remembered settings - happen only
  once the command is accepted), and the value chip keeps the keyboard
  (`AppController::commitOperation` returns whether it applied). Tab to
  the next field (the Hole tool's X, Y) waits for the typed value's verdict
  (`confirmValueText`), so a hole typed off the face keeps its field; a
  keystroke never waits. A click elsewhere first takes a finished preview's verdict
  (`deliverPreviews`); when the command refuses a value whose preview had
  not come back yet, the click goes on to select, as it does when the
  refusal is shown (`applyBeforeSelecting`); Import, Duplicate, Split and a
  Model panel row go on the same way (`applyPendingValue`). Random sessions through these entry points give
  the same documents and operations with and without the worker
  (`AsyncPreview.RandomSessionsMatchSynchronousOnes`). Operations
  created while previews are asynchronous get the scheduler
  (`PreviewSchedulerScope` in `rebuildOperation`), so Pattern's, the
  insert's and the counterbore's first previews are computed on the worker
  too. An operation
  without `clone()` keeps synchronous previews. The UI reads selection
  facts (`selectionSummary`, which face actions to offer) from a cache
  keyed by the selection and its bodies' shape revisions, so a drag step
  makes no kernel call on the GUI thread (a test counts them).
- **Operation hooks** (`Operation.h`): `prompt()` while a further pick is
  needed (Align's target, Mirror's plane); `labelAnchor()` for a value editor
  without an arrow; `neutralValue()` (what Esc returns to, e.g. a hole's
  current diameter, a push/pull's thickness); `neutralIsIdentity()` (the
  neutral value means no preview; Align previews at 0); `relativeBase()`
  (typed `+5` / `-5` are relative to it); `checkValue()` (refuse a value
  before any kernel call, e.g. a thickness of 0);
  `resetAutomaticChoices()` / `reconsider()` (revise an automatic choice once
  the preview is known: an extrusion's new body, Mirror's and Pattern's
  separate bodies); `canCommit()`; `clearPreview()`.
- **Face/body actions:** a single flat face arms Push/Pull and offers Shell,
  Sketch, Hole, Align and Delete face; a single cylindrical face (hole, shaft) arms
  Offset, typed as a diameter; several faces arm Shell. The Delete key on
  selected faces adds a DeleteFaces step. Edges arm Fillet (switchable to
  Chamfer; a hole rim also offers the heat-set insert, Counterbore and
  Countersink: `HeadOperation`, M2-M6 screw presets in the value chip, a
  radial arrow for the diameter and, for a counterbore, one into the hole
  for the depth; the screw size chosen last is kept; one edge offers Align).
  One body (double-click, or its Model-panel row) arms Move and offers Rotate,
  Mirror, Pattern and Duplicate (`BodyTool`; Duplicate is also Ctrl+D and a
  button in the body's expanded Model-panel row; the copy comes out selected
  with the Move arrows, ready to drag away), and "Split into bodies" when it
  is in several pieces (also under the body's warning in the Model panel and
  on the expanded step that left the pieces; a committed step, or a Subtract
  or Intersect, that leaves new pieces says so in a message:
  `suggestSplit`). The Delete key deletes the selected bodies in one undo
  step (`deleteBodies`), except a body that others are built from
  (`Document::bodiesUsing`: bodies that consumed it as a tool; in files from
  before independent copies also its split-off pieces and separate copies):
  that one is hidden instead, with a message, and a
  hidden one cannot be deleted (its Model-panel row says why). Two or more bodies offer Union / Subtract /
  Intersect, applied as one `CompositeCommand` (add `Combine` steps + hide the
  tool bodies); the first selected body is kept and Swap exchanges the two.
  A body built from the other (a Copy or SplitPiece of it, in older files)
  cannot be its tool: Union and Intersect then keep the result in the copy
  instead.
- **Hole tool:** "Hole" on a single flat face (or the palette) arms
  `HoleOperation` (no arrows; the value chip sits at the current hole via
  `labelAnchor()`). Clicks on that face (picked as faces only) add holes:
  `snap()` puts a click within two pick tolerances onto the face's center
  (of its outline's bounding rectangle, `geom::faceOutline`) or a straight
  edge's middle, and otherwise lines X and Y up with those, with circles on
  the face and with the holes placed so far (a snap that lands off the
  face, e.g. the center of a ring-shaped face, is not taken); hovering shows
  where the hole would go. A click on a placed hole makes it the current
  one; Remove hole drops it. A position typed off the face fails the preview
  with "Hole N is off the face" (the step's own "no longer lies on its
  face" is for upstream changes), so Apply waits for a pending preview
  (`commitNeedsPreview`). Previews run on the preview worker like the
  others. Snapping (hover and clicks) tests points against the outline's
  boundary segments (`geom::outlineContains`: curved edges as chords within
  1e-5 of the face's size), not the kernel's classifier, so hovering never
  waits for a preview's kernel call (it did for 110 ms on the enclosure's
  wall); the preview itself checks the holes exactly (`faceContains`).
  Hole again keeps the placed holes. The chip
  edits one field at a time (`field:` actions, Tab = `nextField`):
  diameter, depth (when not through all) and the current hole's X / Y from
  the face's reference corner (the outline's minimum corner) or from the
  hole before it. Screw size (M2-M6) x fit (close / normal per ISO 273, or
  tap) sets the diameter; Counterbore / Countersink use the size's head
  table. Everything placed is one Holes step; Esc leaves the tool; the
  settings are remembered for the next face (`HoleSettings`). The chip's
  actions wrap at 460 px (a hidden row measures their natural width).
- **Align:** Align on a face or edge creates an `AlignOperation` that waits
  for a target on another body (`prompt()`), then previews at offset 0; the
  arrow adds an offset along the target, Flip reverses, "Onto ground" (flat
  faces) lays the face on the XY plane. It commits as a Move step named
  "Align": a one-time placement, not linked to the target.
- **Extrude options:** Symmetric makes the value the total thickness
  (`displayOffset` = value / 2); "Up to face" makes the next face click set
  the distance to a parallel flat face (`ExtrudeOperation::extendToFace`,
  stored as a plain distance). "Draft" makes the value chip edit the draft
  angle instead (degrees; the operation's second "handle" without an arrow,
  so grabbing the arrow returns to the distance); the step stores
  `draftAngle` and the Model panel always offers it. Push/pull steps from
  the UI set `keepEdges`.
- **Mirror / Pattern:** Mirror waits for a flat face (or an origin plane from
  the action bar) and has no value; Apply or Enter commits. Pattern previews
  right away (spacing = the body's extent plus 5 mm); the arrow sets the
  spacing (angle when circular), ± copy changes the count, and clicking an
  edge or a hole/shaft sets the direction or axis. Copies that touch or
  overlap the body are joined into it (one Mirror or Pattern step: a half
  part mirrored across its own face becomes one symmetric body). Copies
  that touch neither the body nor each other (the joined preview has
  (copies + 1) times the body's pieces: `reconsider`, as for an extrusion's
  new body; a body in pieces mirrored across one piece's face joins) become
  **separate, independent bodies** as in Shapr3D, unless there would be
  more than 100 or their imported geometry would not fit in the project;
  the "Separate bodies" option shows the choice and overrides it both ways
  (`separateIsAutomatic()` tells which). Each separate copy is a new body
  (one undo step for all, named like new bodies) made by
  `DuplicateBodyCommand`: the source's history cloned, then a Mirror step
  with `keepOriginal` off or a Move step named "Pattern copy". Editing or
  moving the source later never changes a copy, nor the other way round.
  Their preview shows the source and the copies side by side, unfused
  (`Operation::computePreview`); a message says what happened ("Mirrored as
  a separate body: the image does not touch the original."), and so does
  the hint line before.
- **Profiles in model mode:** sketch regions are pickable, tested on their
  display meshes (no kernel call while hovering; TD-20) (a region lying on a
  face wins over the face; a consumed sketch's region only when it is
  coplanar with the body face hit, so used sketches do not steal clicks);
  selecting profiles arms `ExtrudeOperation`, whose
  arrow follows the plane normal. For sketches on a body, pulling out joins
  and pushing in cuts, unless overridden (New body / Join / Cut). An
  automatic join whose preview would add separate pieces becomes a new body
  (`Operation::reconsider` revises automatic choices after a preview), and
  so does an automatic cut that would remove nothing (a profile beside the
  body pushed in: `reconsiderRefusal` on `ErrorCode::NoEffect`). A cut
  chosen explicitly is refused with the reason instead.
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
  the QML controls can never disagree. **Touch wording:** hints, prompts and
  messages are written for mouse and keyboard; in the touch layout
  `interact::touchWording()` (Qt-free, `interaction/TouchWording`) rewrites
  them — hand-written versions of known sentences ("Shift-click adds more" →
  "tap more to add them", "Esc ends the line" → "tap Line again to end the
  line", "Enter applies" → "✓ applies") and word rules (click → tap) —
  for messages at their source in the interaction layer
  (`InteractionController::forInput`: tool explanations, the sketch
  session's messages, instructions returned in a `Status`), and through
  `touchWording()` in `Main.qml`'s `hintText()` for hints and prompts. Only
  the app's own texts are reworded: `AppController::notifyMessage` passes
  messages on as they are, so a file, project or body name in one ("Exported
  Click lid.stl") is never changed. The help card has a touch text per row
  (`Theme.touch`). `tests/test_touch_wording.cpp` collects every sketch
  hint, tool explanation and operation prompt, the QML hints and the help
  rows, and fails on a mouse or keyboard word left in a touch text. No
  information lives only in a tooltip (tooltips are off in the touch
  layout; what they said is on the help card). A finger or pen tap on
  empty space gives up an Align or Mirror still waiting for its target (a
  mouse keeps waiting).
- **Window-size layout (phones, Split View):** the QML layout follows the
  window, not the device. `Theme.compact` (window narrower than 600 or
  shorter than 500 logical px, bound live from `Main.qml`) turns the
  Create/Modify/Combine palette and the sketch's Draw/Edit palette into a
  strip along the bottom edge that scrolls sideways (the same buttons and
  object names: a `GridLayout` whose `flow` switches), hides the Model panel
  behind a **Model** button (it slides in from the right; `historyOpen`),
  folds the view buttons into a menu behind a **View** button next to the
  axis marker (`viewMenuOpen`; the same buttons, in columns when short),
  makes action rows scroll sideways (`ScrollRow.qml`) and the hint one line
  (a tap shows all of it). Positions derive from the window size
  (`Main.stripTop`, `bottomStackTop`), never from each other in a circle.
  **Safe areas:** `Theme.safeTop/Right/Bottom/Left` come from Qt's
  `SafeArea` attached type (Qt 6.9+: the Dynamic Island or notch, rounded
  corners, the home indicator), or from `--safe-area` on the desktop;
  controls keep `Theme.insetTop/...` from the window edges while the 3D view
  fills the whole window (`ApplicationWindow` padding 0). Overlays center
  their cards in the safe rectangle. Regular windows (desktop, iPad full
  screen) look as before.
- **Buttons:** only a left click (or tap) selects and applies a pending value;
  right/middle drags orbit/pan and their clicks do nothing in 3D. In sketch
  mode a right click acts like Esc (ends the line chain, then leaves the tool).
- **Tools and actions:** `contextActions()` lists what the selection offers;
  the UI shows them in the value chip while a manipulator is active and in the
  selection action bar otherwise (`barAction_<id>` object names, used by the
  acceptance run). `runTool(id)` backs the Modify/Combine palette (ids:
  pushpull, fillet, chamfer, shell, offset, hole, move, rotate, mirror, pattern,
  align, union, subtract, intersect, measure): it runs the tool when the
  selection fits and otherwise explains what to select.

## Threads

- **GUI thread:** Qt events, QML, the interaction core, commands and
  recompute, meshing of changed bodies (`SceneCache`), file I/O.
- **Render thread** (Qt Quick's threaded render loop): `ViewportRenderer`
  copies the `RenderScene` in `synchronize()` while the GUI thread waits.
- **Preview worker** (`interaction/PreviewWorker`, one `std::thread`, owned
  by `InteractionController` after `enableAsyncPreviews`, which the app
  calls): computes and meshes previews on document snapshots. Its log
  lines start with `[worker] `.
- **OpenCASCADE's own threads:** `BRepMesh` meshes faces in parallel inside
  one call.
- **The kernel lock** (`geometry/internal/KernelUtil.h`, TD-4): one thread
  at a time runs OpenCASCADE code. Every kernel entry takes it: the
  `OS_KERNEL_SIGNALS_TO_EXCEPTIONS` scope (so `guarded()`), and a
  `KernelLock` in the functions without one (volume, bounding boxes,
  `solids`, `isValid`, ...). It is recursive, and a scope releases at its
  end the locks a kernel fault jumped over. Meshing writes triangulations
  into faces and polygons into edges that other shapes share, and nearly
  every algorithm reads edges' lists of representations, so a lock around
  meshing alone would not do. With the lock held, OCCT's signal handlers
  are installed and removed by the thread that runs the kernel code (the
  Windows C runtime keeps them per thread): a fault on the worker becomes
  a failed preview like one on the GUI thread (tested). On Windows the
  handler is our own (`onKernelSignal`): OCCT's leaves a process-wide mutex
  locked after a fault, and a later fault on the other thread froze the app
  (`KernelThreads.FaultsOnTwoThreadsInTurnAreAllContained`). On POSIX
  (macOS, iPad) handlers belong to the process, so a kernel call installs
  a dispatcher instead that hands a fault to OCCT only when the faulting
  thread is inside a kernel try block (`Standard_ErrorHandler::IsInTryBlock`,
  per thread); a crash on any other thread meanwhile reaches the crash log
  and the system's crash report as before
  (`KernelThreads.FaultOnAnotherThreadDuringAKernelCallIsNotTheKernels`). The tight
  bounding-box cache in `ShapeData` is guarded by it too (a
  `std::call_once` would stay blocked after a fault jumped out of it).
  While a preview computes, a GUI-thread kernel call (a click on another
  face, a commit's recompute) waits for the worker's current kernel call;
  `geom::interactiveKernelWaits()` counts these waits and the log shows
  them ("gui: waiting for the kernel ... took N ms"). During a drag the GUI
  thread makes none.
- **Where the time goes** (bench_session, a 20-step push/pull drag on the
  249-face enclosure's rim): with previews on the GUI thread each pointer
  move blocked it for 133 ms on average, 259 ms at most; on the worker,
  0.1 ms at most. In the real window (acceptance scenario `previews`, a
  119-face tray) the longest pointer move went from 90 ms to 0.6 ms.
  `ViewportItem` times each pointer move (controller plus the QML updates
  it causes) and `AppController` logs slow ones and each drag's longest.

## Rendering (`render/`, `ui/ViewportItem`)

`ViewportItem` is a `QQuickRhiItem`, so the viewport is part of the Qt Quick
scene (QML overlays compose naturally) and runs on whatever backend Qt Quick
uses (Direct3D 11 on Windows by default, Vulkan/Metal/OpenGL elsewhere).
`ViewportRenderer` copies a `RenderScene` in `synchronize()` (GUI thread
blocked) and draws with 4x MSAA:

1. bodies (lit, two-sided shading), keyed by mesh key so unchanged bodies are
   never re-uploaded;
2. the ground (depth-tested, no depth write, pushed 1.5 px of depth away
   from the viewer so a body's bottom face on the ground hides it): soft
   **contact shadows**, the **grid** and the X/Y/Z **axes** (the orientation
   marker is QML: `AxisTriad` from `InteractionController::axisTriad()`);
3. face highlights (hover, selection, and the orange Model-panel highlight
   with adjacent ranges merged) as **index sub-ranges of the body mesh** (no
   extra buffers);
4. edges as screen-space expanded quads (constant pixel width, depth bias);
5. sketches: profile fills (cached meshes), curves batched per style, point
   markers as zero-length line quads; the sketch being edited draws on top;
6. manipulator arrows and rotation rings on top (no depth test), sized in
   screen pixels.

**Lighting** (`core/Lighting.h`, evaluated in world space by `mesh.frag`):
a sky/ground hemisphere ambient (0.72 for a face turned up, 0.44 turned
down) and a key light at a fixed 50 degree elevation make up-facing faces
the lightest and down-facing ones the darkest from every angle; the key
light's direction around the vertical stays 50 degrees to the viewer's
left (a turntable rig: with lights fixed in the world, some turn of any
box shows two sides in the same shade, and a part seen from behind is in
shadow), so the two sides of a box always differ, the left one lighter; a
faint fill from the viewer and a soft Blinn highlight (strength 0.08,
exponent 40) show curvature. Two-sided without relying on winding: a normal
turned away from the viewer (the eye minus the fragment in perspective, the
view direction in orthographic) is flipped. The acceptance scenario
`shading` measures it on the rendered window (face centres of a cube, a
rounded cube and a plate, 10 views x 2 projections): faces meeting at an
edge differ by at least 21 of 255 levels (it was 0-1 for the two sides in
the isometric view before), tops are lighter than sides, undersides darker,
nothing below 70 or above 229 (the background is ~237). On whole models
(`--face-contrast`: every visible face's median shade; the `committed`
box, `rounded`, `bracket` and `enclosure` demos from 7 views: iso,
orbiting, from behind, low, high, below), the faces meeting at a sharp
edge differ by at least 20 levels, median 47, in both projections; with
the earlier view-space lighting it was 1, median 30, and 11 of 88 pairs
under 8 levels (the two sides seen at once in the isometric, orbiting and
back views). Faces range from 94 (an underside) to 208.

**Depth bias** (edges, sketch curves and fills over faces; the ground
behind them): a point is moved along its view ray by N device pixels' worth
of depth at its distance (`biasedClip` in the vertex shaders: scale by
1 - N x pixel size at depth 1 in perspective, shift z in orthographic).
It replaced a fixed offset in normalized depth, which in perspective came to
~30 mm at the model and showed hidden edges through the body.

**Grid** (`shaders/grid.vert` / `grid.frag`, one quad on z = 0): minor and
major lines computed per pixel from the world position (anti-aliased with
screen-space derivatives), fading out between half the grid's radius and
the radius, with distance from the eye in perspective (the far side, towards
the horizon, first), where neighbouring lines come closer than ~10 px (a
receding plane: no moire) and when the plane is seen nearly edge-on. The
interaction layer picks the spacing (`snapIncrement` for ~14 px on the
ground below the target: in perspective the target can sit on a tall part's
top), the center (on a major line near the target) and the
radius (`InteractionController::groundGrid`: about a view's width around the
target, at least twice the distance to the visible bodies' farthest
footprint corner, so the lines under a model never fade). The axes are
line quads in the line shader's fading mode, reaching 1.5 times the grid's
radius (near the eye they fade too: the Z axis seen from above); the Z
axis's half on the far side of the ground is drawn at 30 % (drawn fully,
the part below the ground read as a line on the ground running towards the
viewer). The
`perspective` scenario follows a major grid line across the fade on the
rendered window, in both projections, and checks that it falls smoothly to
nothing (a hard border shows as a step of 17-24 levels).

**Contact shadows** (`interaction/ContactShadow`, `shaders/shadow.vert`):
the triangles of a body's lowest faces that face straight down (its exact
footprint: an L-shaped part casts an L), flattened onto the ground and drawn
as 37 faint instances spread over a disc (the center and three rings of
12), which add up to a blurred footprint; strength 0.22 when resting on the
ground, fading out as the body rises to half its footprint size, blur 8 %
of the footprint (0.5-6 mm) plus half the height. Not drawn when the eye is
below the ground. Computed once per uploaded mesh.

All shaders share one uniform block (`UniformData`: matrices, colour,
per-draw parameters, eye, pixel scale, lights, grid and fade parameters);
all draws share one dynamic uniform buffer with per-draw offsets.

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

Shaders are GLSL 440 compiled by `qt_add_shaders` into `.qsb` packages
(SPIR-V, HLSL, MSL, GLSL), so they run on Direct3D 11 and on Metal (macOS,
iPadOS) unchanged.

## History panel

`InteractionController::historyRows()` flattens sketches, bodies and each
body's features into rows (name, detail, status — ok, warning, failed,
blocked, suppressed — explanation, editable length parameters).
`AppController` gives QML new rows (`historyChanged`) and a new action list
(`contextActionsChanged`) only when they differ from the last ones: a new
list rebuilds the panel's delegates, and most state changes (a drag step, a
preview arriving) change neither (TD-18). Hovering a row
calls `setHistoryHighlight(id)`: bodies and base features highlight the whole
body, other steps their new faces (`facesCreatedBy`, falling back to
`facesChangedBy`), sketches draw highlighted even when hidden. Clicking a
body row calls `selectBody(id, additive)` (Shift toggles); a tap on it in
the touch layout calls `addBodyToSelection` (`BodyPick::Add`: adds, never
takes out, so tapping the row again to fold it keeps the body selected). The QML `HistoryPanel` edits values through
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
exceptions on: OCCT's reader looped forever on a cut-off text). Imported
geometry has a budget shared by importer, writer and loader
(`doc::kMaxImportedBodyBytes` per step, `doc::kMaxImportedGeometryBytes`
per project): `importBodies` refuses parts beyond it, `buildProjectArchive`
refuses to write more, and `loadProject` reads each `imports/` entry at
most once (an entry named by two steps is refused) and no more than the
budget in all. Versioned with a
migration table; newer versions are refused with a clear message. Readers
treat files as untrusted: size limits, entry-name validation (no traversal),
a JSON nesting limit (256), strict JSON schema checks (a wrong type is an
error, never a thrown exception; `loadProject` also catches any that slip
through), duplicate-UUID rejection; nothing is extracted to
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
new documents, sketch grid snapping, recovery interval), the view's
projection (`view/perspective`, set by the View buttons' toggle), recent files
(`io/RecentFiles`: most recent first; the menu shows the 10 newest that
exist, and a file that is gone never pushes an existing one out; the File
menu rereads the list as it opens, `refreshRecentFiles()`; with an app
folder, entries into its old location — iOS gives an updated app a new data
folder — follow it, `io::rebasedIntoFolder`, as does a recovery copy's
project path) and the
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
makes a re-saved project show its new preview). The path in the source is
base64url of its UTF-8 (`ui/ThumbnailSource`, Qt Core only, tested in
`test_uistate`): Qt hands a provider its id partly percent-decoded, which
broke percent-encoded paths outside ASCII. Cards are the tap
targets; ⋯, a long press or a right click open "Remove from list"
(`removeRecentFile`, `io::withoutRecentFile`). The grid takes as many
columns as fit (two on a phone in portrait) and gets denser in short
windows (a phone in landscape). While Home is shown the keys for the model
(Undo, Redo, Ctrl+D, B, K, F, Delete) do nothing, and closing an overlay
over it (Help, About, Preferences, the unsaved question) gives the keys
back to Home (`focusViewUnlessPanel`).

**Messages** (`AppController::message`, the toast in `Main.qml`) are drawn
above everything, Home and the dialogs included (z 130; they take no
input), and wrap to the window's width (a phone). Every `Text` that shows
a string from a file (body, step and sketch names, sources, messages,
project names and folders, recent files) sets `textFormat: Text.PlainText`:
Qt's automatic format would render markup in a STEP product name as HTML
(and could load remote images).

**Dialogs are overlays** in the window, not native message boxes (touch-sized,
clickable by the acceptance run): `UnsavedOverlay` (Save / Don't Save /
Cancel before New, Open, Open Recent, Restore and closing),
`RecoveryOverlay`, `PreferencesOverlay`, `AboutOverlay`, `HelpOverlay`.
While `UnsavedOverlay` or `RecoveryOverlay` is shown (`window.modalOpen`)
the window's shortcuts are disabled, as behind a native modal dialog, and
`UnsavedOverlay.ask()` ignores a second request: the pending action is the
one the user is being asked about.
Only file choosers stay native (`FileDialog`) — on the desktop. On iOS and
Android (`AppController::savesToAppFolder`: the app's Documents folder,
which the Files app shows; `--app-folder` on the desktop) there is no save
dialog: `SaveNameOverlay` asks for a name (`projectFileBaseName` makes it a
safe file name) and `saveInAppFolder` writes `<folder>/<name>.openshape`;
exports go to `<folder>/Exports/<title>.<ext>` (`exportToAppFolder`); Open
stays Qt's `FileDialog` (the system document picker there). After a menu or overlay
closes, `focusViewUnlessPanel()` gives the keys back to the view (Qt left
them on a hidden menu separator after the Open Recent sub-menu).

## Testing

- GTest suites (`tests/`): core (units, UUID, math), geometry (measurable
  invariants: volumes, bounding boxes, face counts), document/commands/files,
  camera/picking/interaction including a **headless Milestone 0 script**;
  `test_uistate` (Qt Core, no window): settings, window placement and
  recovery sessions with real lock files.
- Robustness suite (`test_robustness`): seeded random modeling sessions
  (`tests/StressHarness.h`: boxes, push/pull, fillets, chamfers, shells,
  sketches, extrusions, moves, rotations, mirrors, patterns, booleans,
  history edits, suppression, deletions; `tests/PortableRandom.h` makes a
  seed replay the same session with every standard library) checked for undo-all / redo-all,
  random undo/redo/edit interleavings and save/open equality (per body:
  volume, box, face count, history with parameters and status); a project
  file fuzzer (truncation, bit flips, broken JSON, wrong types, hostile
  numbers, broken references, unsafe entries: fail with a plain message or
  load a usable document); kernel crash regressions (`tests/data/`).
- `OpenShape --acceptance <dir>` (CTest `acceptance_gui`, label `gui`) drives
  the real application through Qt's platform input path — including clicking
  QML buttons found by `objectName` — and checks geometry after each step,
  saving screenshots. 147 checks, ~30 s (it moves the real mouse cursor):
  help card, the Milestone 0 script, save/open, exports, the Milestone 1
  bracket, a history edit, booleans through the Model panel and the action
  bar, Align, Rotate rings, Pattern, Mirror, two-/three-finger taps and the
  touch layout, the About box, trim/slot/fillet/offset in a sketch,
  symmetric and up-to-face extrusions, a fillet carried by a push, a hole
  resized by its diameter and deleted; `compact` (the window resized live to
  an iPhone's 402x874 and 874x402 with simulated safe areas: tool strip,
  Model panel, View menu, a box pushed by touch, Undo / Redo with their
  messages, a body row tapped twice, the sketch strip; the runner
  restores the run's window size for the next scenario); `appfolder`
  (saving by name and exporting as on an iPhone or iPad, into a temporary
  app folder; the export message keeps a name with "Click" in it in the
  touch layout); `copies` (Mirror and Pattern clicked on a box off the
  origin: separate bodies without asking, the hint line and the toggle,
  then the original's top face pushed twice while the copies stay as they
  were); scenarios `recovery` (a real crash
  of a second OpenShape via `--simulate-crash`, the restore prompt, and a
  second OpenShape ended with unsaved work via `--simulate-quit`),
  `recent`, `preferences` and `files` (Import STEP from the File menu, Ctrl+I
  and Home, the saved thumbnail, Home's cards, menu, long press and
  buttons; then in a 402 x 874 window: Help over Home, a damaged file's
  message above Home, a long message wrapped, markup in a STEP name shown
  as text) and `userguide` (the help card's link to
  docs/USER_GUIDE.md is clicked; a `QDesktopServices` URL handler catches
  it, so no browser opens), `shading` and `perspective` (the viewport's
  look measured on the rendered window, see Rendering; perspective's Fit,
  wheel zoom, orbit pivot, picking, Top view and sketch plane, contact
  shadow). Scenarios start in perspective; a new window's projection is
  checked before the first. The whole run also passes at the CI Mac's
  1024x653 (`--size 1024x653`): clicks on model points that a panel or the
  value chip may cover in a small window pick a free point of the same edge
  (`uncoveredScreenPoint`). `clickItem` scrolls any Flickable around the
  item (both directions) to bring it on screen, and lays out freshly created
  buttons before clicking (a click once landed on the Delete button that
  still sat where Fillet was about to go). Before each step the runner
  waits until camera animations end and previews (computed on the worker)
  are shown; scenario `previews` drags on a 119-face tray and checks that
  no pointer move blocks the window for 50 ms, that Ctrl+Z and Enter work
  while a preview computes, that a refusal arrives from the worker, and a
  drag with 81 Model panel rows.
- `tools/bench/bench_session.cpp` (`-DOPENSHAPE_BUILD_TOOLS=ON`) times drag
  previews, tessellation, recompute and bounding boxes on a filleted part,
  and the GUI thread during a push/pull drag and while hovering in the
  Hole tool on the enclosure (previews on the GUI thread and on the worker);
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
- **Code signing** (docs/CODE_SIGNING.md): when its SignPath secret and
  variable exist, `release.yml` sends `OpenShape.exe` to SignPath after
  packaging and the installer after `make-installer.sh` (GitHub workflow
  artifacts; artifact configurations in `.signpath/artifact-configurations/`),
  and `scripts/windows/use-signed.sh` puts each signed file in place only if
  `scripts/windows/pe-signature.py` finds it byte-identical to the sent file
  apart from the signature and Windows accepts the signature (updating
  `SHA256SUMS.txt`). The release notes (`scripts/ci/install-notes.sh` from
  `packaging/windows/release-notes.md`) say whether a release is signed.
  Both scripts are also checked on every Release run and by ctest on
  Windows (`scripts/ci/test-install-notes.sh`, `pe-signature.py self-test`),
  since otherwise they would only run when a tag is published.

## Known architectural limits (tracked in docs/TECHNICAL_DEBT.md)

- Previews run on the preview worker; commits (the command's recompute),
  undo/redo and meshing of changed bodies still run on the GUI thread, and
  a running kernel call cannot be interrupted (TD-1).
- Only linear per-body history. Features may depend on sketches and (Combine)
  on other bodies; `Document::recomputeDependents` propagates changes
  transitively (a body that changed updates the bodies built on it in turn)
  and `dependsOn` prevents cycles.
- QRhi comes from `Qt6::GuiPrivate`: binaries are tied to the Qt version.
