# Roadmap

Priorities: interaction quality > reliability > discoverability > precision >
performance > breadth. Status markers: ✅ done and verified · 🟡 partial · ⬜ not started.

## Milestone 0 — the full pipeline ✅

Application launches, viewport renders a cube, orbit/pan/zoom, face and edge
hover/selection, planar-face arrow manipulator, drag preview, exact typed
value, commit, undo, redo.

Acceptance script (automated twice: headless in `tests/test_interaction.cpp`
`Milestone0.AcceptanceScript`, and through the real UI in
`OpenShape --acceptance`, CTest `acceptance_gui`):

1. ✅ Launch application
2. ✅ Create 20 mm cube (B or "Add a box")
3. ✅ Orbit around cube
4. ✅ Hover top face → 5. ✅ it highlights
6. ✅ Click top face → 7. ✅ selected → 8. ✅ arrow manipulator appears
9. ✅ Drag upward → 10. ✅ preview updates, document unchanged
11. ✅ Type 35 (the chip shows the height, 20) → 12. ✅ Enter: height is
    exactly 35 mm (volume 14 000 mm³). Until 2026-09-25 the typed value was
    the distance moved (+15).
13. ✅ Undo → 14. ✅ 20 mm
15. ✅ Redo → 16. ✅ 35 mm

Delivered beyond M0: edge fillet/chamfer with manipulator and typed radius,
.openshape save/open, STEP and STL export, unit-aware expressions, touch/pen
input mapping, animated standard views.

## Milestone 1 — basic sketching ✅

- ✅ Constraint solver: FreeCAD's PlaneGCS vendored unmodified (shims only),
  built as a shared library (LGPL-friendly)
- ✅ Sketch objects on the XY plane or any planar face; the view turns to face
  the sketch and returns to 3D on finish
- ✅ Line (chained), rectangle and circle tools; click-click or drag-to-draw
- ✅ Inference shown before commitment: endpoint, origin, midpoint snaps;
  horizontal/vertical guides; zoom-aware grid
- ✅ Typed dimensions while drawing (W Tab H, Ø, L) and editable dimension
  labels (click to edit); unit-aware
- ✅ Constraints: horizontal, vertical, coincident, length, diameter (from
  selection); DOF / "Fully defined" / conflict status
- ✅ Point dragging with live solving; delete with cascade
- ✅ Closed-profile detection (nesting, crossings, dangling lines)
- ✅ Profile selection and extrude with the arrow manipulator: new body, join,
  cut (automatic for sketches on bodies, overridable)
- ✅ Acceptance model: 60 × 30 plate 5 mm, two Ø6 holes cut through, STEP + STL
  export — automated in `SketchFeatures.Milestone1BracketModel`, the
  headless interaction tests, and through the real UI in `acceptance_gui`

Since M1: sketches on the Front (XZ) and Right (YZ) origin planes; horizontal
and vertical distances between any two points (position holes precisely).

Since the first hands-on session: 3-point arcs (typed radius), parallel/
perpendicular/equal/tangent/concentric/on-line/midpoint/radius constraints,
construction toggle, and sketching on a sketch (or its plane) continues it.
Later the same day: slot, trim, corner fillet, offset, "On circle".

Sketch toolkit part 3 (2026-09-26): ✅ center rectangle (E) · ✅ polygon (P,
regular, sized across flats) · ✅ tangent arc (G, smooth arc-arc joins) ·
✅ constraint glyphs on the canvas (select and delete) · ✅ sketch mirror
(Symmetric constraint) and linear/circular pattern · ✅ angle dimension ·
the tools moved to a palette on the left. Not yet: separate sketches on one
plane interacting (TD-27), center-point arc and arc length constraints,
editable pattern spacing (TD-28), splines, text.

## Milestone 2 — common modeling tools ✅

- ✅ Fillet (edges, manipulator + typed radius)
- ✅ Chamfer (equal distance)
- ✅ Shell (open faces, typed wall thickness; OCCT's silent no-op on too-thick
  walls is detected and reported)
- ✅ Move body (X/Y/Z arrows, typed per-axis values) as an editable history step
- ✅ Combine bodies: union, subtract, intersect (tool body consumed and hidden;
  editing the tool updates the result; cycles refused); two or more bodies,
  reachable from the selection action bar and the Combine palette, Swap
  flips which body is cut (the actions were unreachable before 2026-09-25)
- ✅ **Align** (owner request): move a body so a face/edge/circle meets a
  face/edge/circle of another (touching, collinear, concentric; flip, offset,
  onto the ground)
- ✅ Revolve sketch profiles around the sketch's vertical/horizontal axis
  (typed degrees, arrow rides the arc, new body/join/cut; profiles crossing
  the axis are refused with an explanation)
- ✅ Rotate body: X/Y/Z rings (15° snaps, typed angles), editable step
- ✅ Mirror (across a flat face or an origin plane, joined), linear and
  circular patterns (X/Y/Z, a picked edge, or a hole/shaft axis; editable
  count, spacing, angle)
- ✅ Offset face (holes and shafts by diameter, other faces by distance;
  verified against area x distance) and delete faces (defeaturing: holes,
  fillets, chamfers, bosses)

Since: ✅ extrude Symmetric and Up to face; ✅ push/pull takes fillets and
chamfers along (TD-21, where the moved region is straight walls).

Bodies and copies (2026-09-26):

- ✅ Duplicate a body (Ctrl+D): an independent copy, selected with the Move
  arrows
- ✅ Split into bodies (TD-22): the largest piece stays, every other piece
  becomes a body that follows upstream edits
- ✅ Mirror / Pattern as separate bodies (TD-26): parametric copies that
  follow the source, up to 100
- ✅ Rotate about a picked straight edge or hole/shaft, or around a picked
  corner or circle (TD-24)
- ✅ Deleting a body that others are built from hides it instead
- ✅ Extrude with draft (typed angle; see Milestone 5)

Since 2026-09-26 (owner feedback): ✅ Mirror / Pattern copies made as
separate bodies and split pieces are **independent** (editing the source no
longer changes them; older files keep their linked copies); ✅ **Align onto
the origin** (X/Y/Z axis, XY/XZ/YZ plane, the origin point; also by clicking
the drawn axis lines); ✅ **construction axes and planes** (Construct → Axis:
through a hole or shaft, along an edge, through two points, parallel to
X/Y/Z; Construct → Plane: offset from a face or origin plane, at an angle
through an edge, midway between two faces), used by Rotate, Pattern, Mirror,
Align and Sketch, following the faces they were made from (TD-64..67).

Follow-ups: patterns and mirrors of individual features, draft for Revolve
and push/pull (TD-48), construction points and more construction methods
(TD-67).

## Milestone 3 — interaction quality 🟡

Done so far: push/pull shows and sets the part's size to the opposite face
(no arithmetic; +5 / -5 relative), X/Y/Z axes with an orientation marker,
help overlay (? / F1), seam edges hidden from display and
picking, arrow handles placed where the geometry is (value chip beside the
tip, revolve handle on the arc), consumed sketches recede, view returns to 3D
after sketching, QML delegate lifetime bugs fixed. Since the first hands-on
session: right-click ends a line chain (and no longer selects/commits in 3D),
Modify/Combine tool palette that explains what to select, selection action
bar, Model panel hover highlighting, automatic new body for joins that miss,
3x faster drag previews (cached bounding boxes).

Also done: touch-sized controls in the touch layout, a scrollable tool
palette, the About box.

Overnight 2026-09-26: ✅ BVH picking (TD-2; hover picking on a 249-face
enclosure 2.5-3.1 ms → 0.04-0.07 ms, results identical to the linear scan)
· ✅ failure messages that say what to try (a working size while
previewing, a reason for operations that would change nothing) · ✅ tools
waiting for a face pick faces only · ✅ File → Open Recent, Preferences
(Ctrl+,), remembered window, unsaved-changes overlay.

2026-09-26/27: ✅ previews computed on a worker thread (TD-1, TD-4) and
finer-grained UI updates (TD-18) · ✅ project thumbnails and a Home screen
with recent projects · ✅ first-run hints, the help card with touch wording,
README and docs/USER_GUIDE.md · ✅ the value box keeps clear of what was
tapped (docks on phones, above the on-screen keyboard while typing; TD-58)
· ✅ a clearer look: perspective by default, lighting anchored to the world,
a grid that fades, contact shadows (measured: the smallest shade difference
between adjacent faces 1 → 20 of 255; TD-62).

Still to do: box/lasso selection, cycling through stacked faces, snapping
while moving bodies, an icon set (TD-9), sketch and grid geometry cached
(TD-19's remainder).

## Milestone 4 — editable parametric history 🟡

- ✅ Feature history with recompute from the changed feature; failures keep the
  document intact and mark the failing feature (core + tests)
- ✅ `SetParameterCommand` (undoable parameter edits, reject or keep-failed modes)
- ✅ History panel: sketches, bodies, steps with status dots; click a step to
  edit its values; failure explanations in place; suppress/restore; delete;
  hide/show; edit sketch
- ✅ Sketches attached to faces follow them when upstream steps change;
  through-all cuts keep through-holes through (verified: 5 → 8 mm plate
  edit through the real UI)
- ✅ Model panel ↔ view: hovering a row highlights its geometry (a step's new
  faces), body rows select bodies, steps leaving several pieces are flagged
- ✅ Changes propagate through chains of dependent bodies (transitive
  recompute); a step that becomes a no-op after an upstream edit shows a
  warning instead of blocking the steps after it
- ⬜ Provenance-based topological naming (docs/TOPOLOGICAL_NAMING.md)
- ⬜ Reordering steps; rolling back to a step

## Milestone 5 — maker features 🟡

- ✅ Measure: select two faces/edges (same or different bodies) for the exact
  minimum distance, the gap between parallel faces (wall thickness) and the
  angle between flat faces or straight edges
- ✅ 3MF export: welded, closed manifold meshes at 0.01 mm deflection, units in
  millimeters (verified: every edge shared twice, mesh volume within 0.1 % of
  the exact volume)
- ✅ Heat-set insert helper: select a hole's rim → "Heat-set insert" → M2–M5
  presets propose a pilot hole (typical diameter and depth, editable), drilled
  as an editable Hole step
- ✅ Counterbore and countersink on a hole's rim: M2–M6 screw presets (DIN
  974-1 / ISO 4762 counterbores, ISO 10642 countersinks), also on blind
  holes; refused with a reason when the head does not fit
- ✅ Hole tool: holes at clicked points on a flat face (snapping to the
  face's center, edge middles and line-ups; typed X/Y), ISO 273 close/normal
  clearance or tap drill, through all or a depth, counterbore/countersink;
  one step whose holes follow the face (positions not editable afterwards,
  TD-47)
- ✅ Draft: extrude with a typed draft angle (tapered walls, sharp corners;
  refused when the walls would close before the full height, TD-48)
- ✅ Print allowance: clearance holes and counterbore/countersink seats get
  +0.2 mm by default (Preferences; tap and insert sizes unchanged)
- ✅ Text: raised or cut words on a flat face (Noto Sans regular/bold; size,
  depth, angle editable; one line, no kerning: TD-60)
- ⬜ Magnet pockets, snap-fit helpers, threads, section view, text on
  curved faces

## Touch & pen (iPad groundwork) 🟡

- ✅ Gesture recognizer (Qt-free): tap, drag, double-tap, two-finger pan and
  pinch after real movement, two-finger tap = undo, three-finger tap = redo
- ✅ Pen mode: pen selects and draws, fingers only navigate
- ✅ Touch layout: 44 pt controls and a Pen switch once touch is used (from
  the start on tablets; `--touch` on Windows)
- ✅ iPhone + iPad: one universal app built on GitHub's Macs (`ipad.yml`)
  and delivered with TestFlight (running on the owner's iPhone 16 Pro and
  iPad since 2026-09-26); a compact layout below 600x500 that adapts live
  (phone portrait/landscape, foldables, Split View), safe areas, touch-worded
  hints, the value box docked on phones
- ⬜ Tried on the iPhone Duo (ships 2026-10-23)

## Reliability and app shell

- ✅ Recovery copies of unsaved work (after edits, periodically, when the
  app goes to the background) and a restore prompt after a crash or any
  exit with unsaved work the user did not discard; a one-line crash log
  (TD-39)
- ✅ Faults inside OpenCASCADE become failed steps with a message; crashes
  elsewhere stay real, logged crashes (TD-41)
- ✅ Stress tests: seeded random modeling sessions with undo/redo,
  interleaved edits and save/open, replayed identically on Windows and
  macOS; a project-file fuzzer. They found and fixed seven bugs, a kernel
  crash among them
- ✅ Booleans leave their inputs untouched (OCCT's non-destructive mode)
- ✅ Open Recent, Preferences (units, grid snapping, recovery interval),
  remembered window, unsaved-changes overlay
- ⬜ Minidumps or stack traces for crash reports (TD-39)

## Platform & infrastructure

- ✅ Windows 11 / MSYS2 UCRT64 / GCC 16 / Qt 6.11 / OCCT 7.9.3 (Direct3D 11)
- ✅ CI on Windows: `.github/workflows/ci.yml` (MSYS2, warnings as errors,
  headless tests) on every push to the GitHub repository
  (SamuelAirs/openshape, public since 2026-09-25); 6–11 min per run as
  measured on 2026-09-25.
  Pushes that only change documentation skip it
- 🟡 macOS CI job (Apple Clang, Homebrew): builds and passes the headless
  tests; its acceptance run (continue-on-error, 1024x653 window) passes every
  check since CI #32 (TD-35 resolved)
- ⬜ Linux CI: no Linux environment was available to verify it (WSL is not
  installed and needs admin rights)
- ✅ License: MPL-2.0 (`LICENSE`, notice in every source file; see
  docs/LICENSING.md)
- ✅ Windows release: OpenCASCADE built without FFmpeg/FreeImage (TD-17), a
  license gate over every packaged file, per-user NSIS installer, portable
  zip and checksums, icon and version resource; 269 files / 160 MB
  (installer 40 MB, zip 59 MB); `release.yml` (tag → GitHub Release) ran
  green. ✅ v0.1.0 (2026-09-26) and v0.2.0 (2026-09-27, with the LGPL libraries'
  sources attached) published as pre-releases. 🟡 Code
  signing (TD-44): the SignPath Foundation steps are in `release.yml`
  (docs/CODE_SIGNING.md); the owner applied, approval pending
- ⬜ MSVC + vcpkg build
- ⬜ macOS, Linux runtime verification
- ⬜ OCCT 8.x migration
