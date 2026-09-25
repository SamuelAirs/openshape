# Project status

_Last updated: 2026-09-25, end of the first long session (hands-on testing
with the owner, then the owner's five priorities). New here? Read "Handoff"
first._

## Current milestone

**Milestones 0, 1 and 2 complete; Milestone 4 core (editable history) done;
Milestones 3 and 5 partial.** The owner's priorities from 2026-09-25 —
booleans and **align tools**, the sketch toolkit, transform & repeat, direct
face edits, the iPad/Pencil workflow ("similar to Shapr3D") — each have a
first, tested version. Their follow-ups are the next tasks below.

## Handoff: where we left off

- **State:** everything is committed and pushed to `origin/main`
  (<https://github.com/SamuelAirs/openshape>, private), and the owner's
  package (`dist/OpenShape/`, which their desktop shortcut starts) was
  rebuilt from it. If the working tree is not clean, someone changed it
  after this note.
- **Verify first:** build, `ctest --test-dir build/msys2-ucrt64 -LE gui`
  (211 pass), then — only when nobody is using the mouse —
  `ctest --test-dir build/msys2-ucrt64 -L gui` (103 real-UI checks, ~20 s).
- **Last session (2026-09-25):** the owner modeled hands-on while the debug
  log was watched; everything they reported was fixed (right-click ends a
  line, sketching on a sketch continues it, separate pieces are flagged and
  joins that miss make new bodies, Model-panel rows highlight their
  geometry, booleans are reachable). Then their priorities were built in
  order: booleans + Align, Rotate, Mirror/Pattern, the sketch toolkit (arcs,
  eight constraints, construction), direct face edits, touch & pen
  groundwork. Details and lessons: docs/DEVLOG.md.
- **Waiting on the owner** (product decisions; do not guess):
  1. The license (LICENSE_PENDING.md). It also decides how the iPad app can
     be distributed.
  2. Their global git `user.email` is malformed. This repository overrides it
     locally with their GitHub noreply address, so commits made here are
     attributed correctly; fixing the global setting is up to them.
  3. CI has never been seen green: the repository is private and `gh` is not
     installed here. The owner can check the Actions tab (or install `gh`
     and log in).
- **Working with the owner:** they test hands-on and report issues while you
  watch the log (`OPENSHAPE_LOG=debug`, `scripts/dev/watch_log.py`). For each
  report: reproduce, fix, add a check that clicks the fixed path, re-package
  so their shortcut runs the fix. Shapr3D is the reference for UX questions.
  Push in batches (CI minutes on a private repo bill at 2x).

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
  and 3-point **arc** tools; endpoint/origin/midpoint snaps; horizontal/
  vertical inference; typed dimensions (incl. arc radius); click-to-edit
  dimension labels; constraints: horizontal, vertical, coincident, length,
  diameter, radius, horizontal/vertical distance, **parallel, perpendicular,
  equal, tangent, concentric, on line, midpoint**; construction toggle;
  point dragging; DOF status; delete. Right-click or Esc ends a line chain.
  Sketching on a sketch (or on a plane that already has one) continues it,
  so new curves split its shapes.
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
- **Direct face edits**: select holes, fillets, chamfers or bosses and press
  Delete to remove them (the neighbours heal the gap); click a hole or shaft
  wall and type its new diameter (print tolerance); offset other faces with
  their neighbours following (refused when they cannot follow).
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
Re-measure with `bench_session` (BUILDING.md, "Developer tools"); the
end-of-session run gave 45 / 0.93 / 14.9 ms.

## Partially implemented

- STEP import: kernel function and tests; not exposed in the UI.
- Touch/pen: gestures (tap, drag, double-tap, two-finger pan/pinch after real
  movement, two-finger tap = undo, three-finger tap = redo) and pen mode (pen
  selects/draws, fingers navigate) are implemented and tested with synthetic
  Qt touch events; not yet tried on real touch hardware or an iPad.
- Disconnected pieces: flagged in the Model panel, not yet split into bodies (TD-22).

## Broken / missing

- Packaging: a verified self-contained folder (`scripts/package-windows.sh`);
  no installer, large (~290 MB, see TD-6); **not distributable** (TD-17).
- No thumbnails in project files.
- CI runs on GitHub (private repo) but has not been confirmed green yet; Linux unverified.
- Sketch: no offset, trim, sketch fillet, slot, polygon, center rectangle,
  spline or text yet; no sketch-level patterns/mirror; constraints have no
  on-canvas icons yet. Separate (hidden or consumed) sketches on one plane
  still do not interact.
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
- Rotate and Align are Move steps with an optional rotation (`MoveFeature` +
  `RigidMotion`): one feature kind, one file-format entry. Align stores the
  resulting motion, not a link to its target.
- Mirror and Pattern join their copies into the body in one General Fuse
  pass; their plane/axis/direction is stored as geometry.
- Kernel results are checked against an invariant of the intent (shell
  removes volume, defeaturing changes the shape, offset face changes the
  volume by about area x distance): OCCT can report success with a wrong
  result.
- Touch gestures are recognized by a Qt-free class (`TouchGestureRecognizer`)
  so they are unit-tested; two fingers do not navigate until they move, which
  makes two-/three-finger taps usable as undo/redo.

## Known technical risks

- **Do not distribute the Windows package yet:** it contains GPL FFmpeg/x264/x265
  DLLs pulled in by MSYS2's OCCT (TD-17). Fix: build OCCT without FFmpeg.
- Topological naming on symmetric parts after large upstream edits (TD-3).
- GUI-thread previews and QML binding churn will stutter on big models (TD-1, TD-18, TD-19).
- QRhi via GuiPrivate ties builds to a Qt minor version (TD-5).
- Project license still undecided (see LICENSE_PENDING.md).

## Next concrete tasks (owner priorities)

1. Acceptance gaps: click the Arc tool, a sketch constraint, Delete face and
   a hole-diameter Offset through the real UI in `AcceptanceRunner` (today
   they are covered by headless tests only).
2. Sketch toolkit, part 2: slot, center rectangle, polygon, offset, trim,
   sketch fillet, tangent arc, constraint icons, sketch patterns; separate
   sketches on one plane should interact (TD-27, TD-28).
3. Direct face edits, part 2: extrude symmetric / to a face / with draft;
   move a face together with tangent fillets (TD-21).
4. Responsiveness: asynchronous previews (TD-1), split `stateChanged` and list
   models (TD-18), cache sketch/grid geometry (TD-19), BVH picking (TD-2, TD-20).
5. iPad: touch-sized targets (44 pt) when touch is used, a pen-mode switch in
   the UI (TD-29), then the iOS build on the owner's Mac (Qt for iOS, OCCT for
   iOS, licensing — see LICENSE_PENDING.md).
6. Split disconnected pieces into separate bodies (TD-22); pattern/mirror as
   separate bodies (copies) as an option (TD-26).
7. Align follow-ups: snap alignment while moving (Shapr3D-style); rotate
   about a picked edge or point (TD-24).
8. Installer and smaller, distributable package (TD-6, TD-17).

## Tests currently passing

211/211 (`ctest -LE gui`): GTest suites for core, geometry, profiles, sketch
model and solver, document, commands, project files, sketch features, face
attachment, camera, picking, interaction (headless M0 script, sketch
workflows, history editing, highlight, booleans, right-click, align,
rotate, mirror, pattern, sketch constraints, arcs, face edits, touch
gestures, pen mode), plus `acceptance_gui`: 103 end-to-end checks through the
real UI (including multi-finger taps). Build with
`-DOPENSHAPE_WARNINGS_AS_ERRORS=ON` (as CI does): 0 warnings.

## Platforms verified

| Platform | Build | Tests | Runs |
|---|---|---|---|
| Windows 11 x64 (MSYS2 UCRT64, D3D11) | ✅ | ✅ | ✅ |
| Linux | ⬜ | ⬜ | ⬜ |
| macOS | ⬜ | ⬜ | ⬜ |
