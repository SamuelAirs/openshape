# Project status

_Last updated: 2026-09-25 (after the first hands-on session with the owner)_

## Current milestone

**Milestones 0 and 1 complete; Milestone 4 core (editable history) done;
Milestone 2 mostly done** (shell, move, booleans). Owner priorities for what
comes next (2026-09-25): booleans and **align tools**, then the sketch
toolkit, transform & repeat, direct face edits, and the iPad/Pencil workflow —
"similar to Shapr3D".

## What works (verified)

- Build: Windows 11, MSYS2 UCRT64, GCC 16.2, Qt 6.11.2, OCCT 7.9.3, Direct3D 11
  (RX 7800 XT). Clean configure+build verified in a fresh directory, also with
  `-DOPENSHAPE_WARNINGS_AS_ERRORS=ON` (as CI uses).
- App launches; QRhi viewport with MSAA, lighting, thick edges, adaptive grid, axes.
- Box creation; orbit (about the point under the cursor), pan, zoom-to-cursor,
  animated standard views, fit, ortho/perspective.
- Face and edge hover highlighting and selection; double-click body selection;
  touch/pen taps additive; larger touch tolerances. Only a left click or tap
  selects; right/middle buttons orbit/pan.
- Push/pull of planar faces, fillet and chamfer of edges: arrow manipulator,
  drag with zoom-aware snapping, live preview, typed unit-aware values,
  Enter/click-away commit, Esc cancel.
- Sketching on the XY/XZ/YZ planes or a flat face: line, rectangle, circle
  tools; endpoint/origin/midpoint snaps; horizontal/vertical inference; typed
  dimensions; click-to-edit dimension labels; horizontal/vertical/coincident/
  length/diameter and horizontal/vertical distance constraints; point
  dragging; DOF status; delete. Right-click or Esc ends a line chain.
- PlaneGCS solver (vendored, unmodified) with conflict detection.
- Closed-profile detection; profile picking; extrude as new body, join or cut.
  An automatic join that would not touch the body becomes a new body.
- Undo/redo for all edits (including every sketch step); failed operations
  never enter history.
- Save/open `.openshape` (versioned, validated, atomic save); STEP/STL/3MF export.
- Milestone 1 bracket built through the real UI by the acceptance runner,
  exported, then edited to 8 mm in the history panel with the holes staying through.
- Model panel: edit any step's values after reopening; failures explained in
  place; suppress/delete/hide. Hovering a row highlights it in the view (a
  step's new geometry, e.g. the fillet surface); clicking a body row selects
  the body, Shift adds. Steps that leave a body in several pieces are flagged.
- Sketches on faces follow their faces; through-all cuts.
- Shell, Move (X/Y/Z arrows), Rotate (X/Y/Z rings, 15° snaps), Revolve —
  editable history steps.
- **Mirror** a body across a flat face or an origin plane (joined), and
  **Pattern** it in a row (X/Y/Z or along an edge) or around an axis (X/Y/Z
  or a hole/shaft), count/spacing/angle editable later.
- **Align**: a face/edge/circle of one body onto a face/edge/circle of another
  (touching faces, collinear edges, concentric holes/shafts), Flip, offset
  arrow, or "Onto ground" to lay a flat face on the build plate.
- **Booleans reachable in the UI**: Union / Subtract / Intersect for two or
  more bodies from the selection action bar or the Combine palette; Swap
  flips which body is cut; one undo step.
- Modify/Combine tool palette: each tool runs with a fitting selection or says
  what to select.
- Measure (distance / parallel gap / angle), heat-set insert helper, help overlay (F1).
- `OPENSHAPE_LOG=debug` logs per-operation timings.

## Performance (measured, 21-face filleted part, i5-13500)

| | Before 2026-09-25 fix | Now |
|---|---|---|
| Push/pull drag preview (per pointer move) | 130 ms | 43 ms |
| Body tessellation (820 triangles) | 43 ms | 0.85 ms |
| Recompute after an upstream edit | 57 ms | 14.5 ms |

The remaining preview cost is the kernel boolean (38 ms when the pushed face
meets tangent fillets, 6 ms otherwise). Previews still run on the GUI thread (TD-1).

## Partially implemented

- STEP import: kernel function and tests; not exposed in the UI.
- Touch/pen: input mapping implemented; not tested on real touch hardware.
- Disconnected pieces: flagged in the Model panel, not yet split into bodies (TD-22).

## Broken / missing

- Packaging: a verified self-contained folder (`scripts/package-windows.sh`);
  no installer, large (~290 MB, see TD-6); **not distributable** (TD-17).
- No thumbnails in project files.
- CI runs on GitHub (private repo) but has not been confirmed green yet; Linux unverified.
- Sketch: no arcs, construction toggle, parallel/perpendicular/tangent/equal.
  Curves of different sketches on one plane do not split each other, and
  drawing on a plane that has a sketch does not continue it (Shapr3D does both).
- Push/pull is prism + boolean: fillets next to a pushed face do not follow it (TD-21).

## Recent architectural decisions

- Tight bounding boxes are cached per Shape; display paths (mesh resolution,
  camera, signature scale) use `approximateBoundingBox` (microseconds).
- A step's highlight is derived from kernel identity: faces of its output not
  present in its input, preferring those on new surface objects
  (`facesCreatedBy`), falling back to all changed faces (`facesChangedBy`).
- Operations can revise automatic choices after seeing the preview
  (`Operation::reconsider`), used for join -> new body.
- Every user-facing action gets at least one acceptance check that reaches it
  by clicking (booleans were unreachable while headless tests passed).

## Known technical risks

- **Do not distribute the Windows package yet:** it contains GPL FFmpeg/x264/x265
  DLLs pulled in by MSYS2's OCCT (TD-17). Fix: build OCCT without FFmpeg.
- Topological naming on symmetric parts after large upstream edits (TD-3).
- GUI-thread previews and QML binding churn will stutter on big models (TD-1, TD-18, TD-19).
- QRhi via GuiPrivate ties builds to a Qt minor version (TD-5).
- Project license still undecided (see LICENSE_PENDING.md).

## Next concrete tasks (owner priorities)

1. Sketch toolkit: arc, center rectangle, polygon, slot, offset, trim, sketch
   fillet, construction toggle; parallel/perpendicular/tangent/equal; curves on
   one plane interact; continue a sketch by drawing on its plane.
2. Direct face edits: move/offset face with neighbours following, delete face;
   extrude symmetric / to face / with draft.
3. Responsiveness: asynchronous previews (TD-1), split `stateChanged` and list
   models (TD-18), cache sketch/grid geometry (TD-19), BVH picking (TD-2, TD-20).
4. Touch & Pencil: pen draws/selects, fingers navigate, two-/three-finger taps
   undo/redo, touch-sized targets. The iPad build needs the owner's Mac.
5. Split disconnected pieces into separate bodies (TD-22); pattern/mirror as
   separate bodies (copies) as an option.
6. Align follow-ups: snap alignment while moving (Shapr3D-style).
7. Installer and smaller, distributable package (TD-6, TD-17).

## Tests currently passing

187/187 (`ctest -LE gui`): GTest suites for core, geometry, profiles, sketch
model and solver, document, commands, project files, sketch features, face
attachment, camera, picking, interaction (headless M0 script, sketch
workflows, history editing, highlight, booleans, right-click, align,
rotate, mirror, pattern), plus `acceptance_gui`: 100 end-to-end checks
through the real UI.

## Platforms verified

| Platform | Build | Tests | Runs |
|---|---|---|---|
| Windows 11 x64 (MSYS2 UCRT64, D3D11) | ✅ | ✅ | ✅ |
| Linux | ⬜ | ⬜ | ⬜ |
| macOS | ⬜ | ⬜ | ⬜ |
