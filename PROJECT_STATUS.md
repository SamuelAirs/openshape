# Project status

_Last updated: 2026-09-25_

## Current milestone

**Milestones 0 and 1 complete.** Next: editable history panel (Milestone 4
core) so every parameter of a reopened model can be changed, then Milestone 2
tools.

## What works (verified)

- Build: Windows 11, MSYS2 UCRT64, GCC 16.2, Qt 6.11.2, OCCT 7.9.3, Direct3D 11
  (RX 7800 XT). Clean configure+build verified in a fresh directory.
- App launches; QRhi viewport with MSAA, lighting, thick edges, adaptive grid, axes.
- Box creation; orbit (about the point under the cursor), pan, zoom-to-cursor,
  animated standard views, fit, ortho/perspective.
- Face and edge hover highlighting and selection; double-click body selection;
  touch/pen taps additive; larger touch tolerances.
- Push/pull of planar faces, fillet and chamfer of edges: arrow manipulator,
  drag with zoom-aware snapping, live preview, typed unit-aware values,
  Enter/click-away commit, Esc cancel.
- Sketching on the XY plane or a flat face: line, rectangle, circle tools;
  endpoint/origin/midpoint snaps; horizontal/vertical inference; typed
  dimensions; click-to-edit dimension labels; horizontal/vertical/coincident/
  length/diameter constraints; point dragging; DOF status; delete.
- PlaneGCS solver (vendored, unmodified) with conflict detection.
- Closed-profile detection; profile picking; extrude as new body, join or cut.
- Undo/redo for all edits (including every sketch step); failed operations
  never enter history.
- Save/open `.openshape` (versioned, validated, atomic save); STEP/STL export.
- Milestone 1 bracket (60 × 30 × 5 plate, two Ø6 through-holes) built through
  the real UI by the acceptance runner, exported to STEP and STL.

## Partially implemented

- Parametric history: recompute from changed features, failure marking,
  undoable parameter edits work in the core; **no history panel UI** (feature
  parameters such as extrude distance cannot yet be edited after commit).
- Booleans, move/rotate: kernel functions and tests exist; no UI.
- STEP import: kernel function and tests; not exposed in the UI.
- Touch/pen: input mapping implemented; not tested on real touch hardware.

## Broken / missing

- No packaging: the exe runs only with MSYS2 DLLs on PATH.
- No thumbnails in project files. No CI.
- Sketch: no arcs, construction toggle, parallel/perpendicular/tangent/equal.

## Recent architectural decisions

- PlaneGCS chosen as the constraint solver; vendored unmodified with shims,
  built as a separate C++23 shared library.
- Closed profiles found with OCCT's General Fuse rather than own arrangement code.
- Features get an `EvalContext` and declare dependencies (extrude → sketch).
- Sketch edits are snapshot commands (exact undo, simple).
- Real-input GUI acceptance runner extended to M1 (52 checks).

## Known technical risks

- Topological naming on symmetric parts after large upstream edits (TD-3);
  sketches on faces do not follow face changes (TD-14).
- GUI-thread tessellation/booleans/profile detection will stutter on big models (TD-1, TD-13).
- QRhi via GuiPrivate ties builds to a Qt minor version (TD-5).
- Project license still undecided (see LICENSE_PENDING.md).

## Next concrete tasks

1. History panel: list bodies/sketches/features, edit parameters, show failures.
2. Boolean and move/rotate UI (Milestone 2).
3. Arc tool and parallel/perpendicular/equal constraints.
4. CI (Windows MSYS2 + Linux) and Windows packaging.

## Tests currently passing

134/134 (`ctest`): 133 GTest cases (core, geometry, profiles, sketch model and
solver, document, commands, project files, sketch features, camera, picking,
interaction incl. headless M0 script and sketch workflows) plus
`acceptance_gui` (52 end-to-end checks through the real UI).

## Platforms verified

| Platform | Build | Tests | Runs |
|---|---|---|---|
| Windows 11 x64 (MSYS2 UCRT64, D3D11) | ✅ | ✅ | ✅ |
| Linux | ⬜ | ⬜ | ⬜ |
| macOS | ⬜ | ⬜ | ⬜ |
