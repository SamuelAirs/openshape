# Project status

_Last updated: 2026-09-25_

## Current milestone

**Milestones 0 and 1 complete; Milestone 4 core (editable history) done;
Milestone 2 mostly done** (shell, move, booleans). Next: rotate/mirror/patterns,
packaging, interaction polish (M3).

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
  the real UI by the acceptance runner, exported to STEP and STL, then edited
  to 8 mm in the history panel with the holes staying through.
- History panel: edit any step's values after reopening; failures explained
  in place; suppress/delete/hide.
- Sketches on faces follow their faces; through-all cuts.
- Shell (enclosures), Move (X/Y/Z arrows), Union/Subtract/Intersect between
  bodies, Revolve (spacers, knobs) — all as editable history steps.
- Sketch planes: ground (XY), front (XZ), right (YZ), or any flat face.
- Measure (distance / parallel gap / angle) and 3MF export.
- Heat-set insert helper (hole rim → M2–M5 pilot hole presets).
- Help overlay (? button, F1).

## Partially implemented

- Rotate: kernel function and tests; no UI.
- STEP import: kernel function and tests; not exposed in the UI.
- Touch/pen: input mapping implemented; not tested on real touch hardware.

## Broken / missing

- Packaging: a verified self-contained folder (`scripts/package-windows.sh`);
  no installer, large (~290 MB, see TD-6).
- No thumbnails in project files.
- CI workflow written but never run (no remote); Linux unverified.
- Sketch: no arcs, construction toggle, parallel/perpendicular/tangent/equal.

## Recent architectural decisions

- PlaneGCS chosen as the constraint solver; vendored unmodified with shims,
  built as a separate C++23 shared library.
- Closed profiles found with OCCT's General Fuse rather than own arrangement code.
- Features get an `EvalContext` and declare dependencies (extrude → sketch).
- Sketch edits are snapshot commands (exact undo, simple).
- Real-input GUI acceptance runner extended to M1 (52 checks).

## Known technical risks

- **Do not distribute the Windows package yet:** it contains GPL FFmpeg/x264/x265
  DLLs pulled in by MSYS2's OCCT (TD-17). Fix: build OCCT without FFmpeg.

- Topological naming on symmetric parts after large upstream edits (TD-3).
- GUI-thread tessellation/booleans/profile detection will stutter on big models (TD-1, TD-13).
- QRhi via GuiPrivate ties builds to a Qt minor version (TD-5).
- Project license still undecided (see LICENSE_PENDING.md).

## Next concrete tasks

1. Rotate (ring manipulator), mirror, linear/circular pattern.
2. Arc tool and parallel/perpendicular/equal constraints.
3. Off-GUI-thread tessellation and previews (TD-1).
4. Installer and smaller package (TD-6).

## Tests currently passing

164/164 (`ctest`): 163 GTest cases (core, geometry, profiles, sketch model and
solver, document, commands, project files, sketch features, face attachment,
camera, picking, interaction incl. headless M0 script, sketch workflows,
history editing, shell, move, combine, revolve, measure, 3MF, heat-set inserts) plus `acceptance_gui` (64 end-to-end checks through the
real UI).

## Platforms verified

| Platform | Build | Tests | Runs |
|---|---|---|---|
| Windows 11 x64 (MSYS2 UCRT64, D3D11) | ✅ | ✅ | ✅ |
| Linux | ⬜ | ⬜ | ⬜ |
| macOS | ⬜ | ⬜ | ⬜ |
