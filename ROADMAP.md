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

## Milestone 1 — basic sketching ⬜ (next)

- ⬜ Choose and integrate a constraint solver (PlaneGCS preferred; see
  docs/TECHNOLOGY_EVALUATION.md and LICENSE_PENDING.md)
- ⬜ Sketch document object on a plane (XY/XZ/YZ or a planar face), with a
  2D coordinate system; view aligns to the sketch plane
- ⬜ Line, rectangle, circle tools with inference hints (horizontal/vertical,
  endpoint/midpoint/origin snaps) shown before commitment
- ⬜ Dimensions: rectangle width/height, circle diameter (typed, unit-aware)
- ⬜ Constraints: horizontal, vertical, coincident
- ⬜ Closed-profile detection; select a profile region
- ⬜ Extrude profile (new body, join, cut) with the same arrow manipulator
- ⬜ Acceptance model: 60 × 30 plate, 5 mm; Ø6 hole cut through; hole
  duplicated; export STEP + STL (automated)

## Milestone 2 — common modeling tools 🟡

- ✅ Fillet (edges, manipulator + typed radius)
- ✅ Chamfer (equal distance)
- 🟡 Boolean union/subtract/intersect (kernel + tests done; no UI yet)
- 🟡 Move / rotate body (kernel done; no UI yet)
- ⬜ Mirror, linear pattern, circular pattern
- ⬜ Shell
- ⬜ Offset face (push/pull of non-planar faces)

## Milestone 3 — interaction quality ⬜

Contextual tools, manipulator feel, snapping, box/touch selection, selection
cycling, keyboard shortcuts, touch-sized targets, viewport transitions,
error messages, discoverability. Also: BVH picking, off-thread tessellation.

## Milestone 4 — editable parametric history 🟡

- ✅ Feature history with recompute from the changed feature; failures keep the
  document intact and mark the failing feature (core + tests)
- ✅ `SetParameterCommand` (undoable parameter edits, reject or keep-failed modes)
- ⬜ History panel UI (list, double-click to edit, failure markers, suppress)
- ⬜ Provenance-based topological naming (docs/TOPOLOGICAL_NAMING.md)

## Milestone 5 — maker features ⬜

Hole tool, countersink/counterbore, heat-set insert presets, magnet pockets,
clearance helper, draft, emboss/deboss, text, snap-fit helpers, threads,
measure, section view, 3MF export.

## Platform & infrastructure

- ✅ Windows 11 / MSYS2 UCRT64 / GCC 16 / Qt 6.11 / OCCT 7.9.3 (Direct3D 11)
- ⬜ CI: Windows (MSYS2) + Linux builds and unit tests
- ⬜ Windows packaging (windeployqt, installer, license bundle)
- ⬜ MSVC + vcpkg build
- ⬜ macOS, Linux runtime verification
- ⬜ OCCT 8.x migration
