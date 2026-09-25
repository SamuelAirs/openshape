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
11. ✅ Type 15 → 12. ✅ Enter: height is exactly 35 mm (volume 14 000 mm³)
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
Not yet: slot, center rectangle, polygon, offset, trim, sketch fillet,
splines, text, constraint icons, sketch patterns.

## Milestone 2 — common modeling tools 🟡

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

## Milestone 3 — interaction quality 🟡

Done so far: help overlay (? / F1), seam edges hidden from display and
picking, arrow handles placed where the geometry is (value chip beside the
tip, revolve handle on the arc), consumed sketches recede, view returns to 3D
after sketching, QML delegate lifetime bugs fixed. Since the first hands-on
session: right-click ends a line chain (and no longer selects/commits in 3D),
Modify/Combine tool palette that explains what to select, selection action
bar, Model panel hover highlighting, automatic new body for joins that miss,
3x faster drag previews (cached bounding boxes).


Contextual tools, manipulator feel, snapping, box/touch selection, selection
cycling, keyboard shortcuts, touch-sized targets, viewport transitions,
error messages, discoverability. Also: BVH picking, off-thread tessellation.

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
- ⬜ General hole tool, countersink/counterbore, magnet pockets, clearance
  helper, draft, emboss/deboss, text, snap-fit helpers, threads, section view

## Platform & infrastructure

- ✅ Windows 11 / MSYS2 UCRT64 / GCC 16 / Qt 6.11 / OCCT 7.9.3 (Direct3D 11)
- 🟡 CI: `.github/workflows/ci.yml` (Windows/MSYS2, mirrors the verified local
  build) written but **never run** — the repository has no remote yet.
  Linux not configured: no Linux environment was available to verify it
  (WSL is not installed and needs admin rights)
- 🟡 Windows packaging: self-contained folder verified (clean PATH, full
  acceptance run); installer and size reduction pending
- ⬜ MSVC + vcpkg build
- ⬜ macOS, Linux runtime verification
- ⬜ OCCT 8.x migration
