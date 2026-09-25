# Project status

_Last updated: 2026-09-25_

## Current milestone

**Milestone 0 complete.** Starting Milestone 1 (sketching).

## What works (verified)

- Build: Windows 11, MSYS2 UCRT64, GCC 16.2, Qt 6.11.2, OCCT 7.9.3, Direct3D 11
  (RX 7800 XT). Clean configure+build verified in a fresh directory.
- App launches; QRhi viewport with MSAA, lighting, thick edges, adaptive grid, axes.
- Box creation; orbit (about the point under the cursor), pan, zoom-to-cursor,
  animated standard views, fit, ortho/perspective.
- Face and edge hover highlighting and selection; double-click body selection;
  touch/pen taps additive; larger touch tolerances.
- Push/pull of planar faces: arrow manipulator, drag with zoom-aware snapping,
  live preview, typed unit-aware values, Enter/click-away commit, Esc cancel.
- Fillet/chamfer of edges with the same interaction.
- Undo/redo for all edits; failed operations never enter history.
- Save/open `.openshape` (versioned, validated, atomic save); STEP/STL export.
- Plain-language error messages (e.g. too-large fillet), technical detail in the log.

## Partially implemented

- Booleans, move/rotate: kernel functions and tests exist; no UI.
- Parametric history: recompute, failure marking and undoable parameter edits
  work in the core; no history panel UI.
- STEP import: kernel function and tests; not exposed in the UI.
- Touch/pen: input mapping implemented; not tested on real touch hardware.

## Broken / missing

- No sketching yet (Milestone 1).
- No packaging: the exe runs only with MSYS2 DLLs on PATH.
- No thumbnails in project files.
- No CI.

## Recent architectural decisions

- Qt Quick + own QRhi renderer (not OCCT AIS, not Qt Quick 3D).
- CPU picking on our own tessellation; highlights drawn as index sub-ranges.
- Interim topological naming: index hint + geometric signature, fail loudly.
- MSYS2 for the dev toolchain; vcpkg/MSVC deferred to release engineering.
- Real-input GUI acceptance runner (`--acceptance`) as part of CTest.

## Known technical risks

- Topological naming on symmetric parts after large upstream edits (TD-3).
- GUI-thread tessellation/booleans will stutter on big models (TD-1).
- QRhi via GuiPrivate ties builds to a Qt minor version (TD-5).
- Sketch solver choice depends on the undecided project license.

## Next concrete tasks

1. Sketch data model (plane, 2D entities, UUIDs) and commands.
2. Solver integration decision (PlaneGCS extraction vs. interim own solver).
3. Sketch tools: line, rectangle, circle with inference hints.
4. Profile detection and extrude (new body / cut).
5. Bracket acceptance model (automated).

## Tests currently passing

98/98 (`ctest`): 97 GTest cases (core, geometry, document, commands, project
files, camera, picking, interaction incl. headless Milestone 0 script) plus
`acceptance_gui` (29 end-to-end checks through the real UI).

## Platforms verified

| Platform | Build | Tests | Runs |
|---|---|---|---|
| Windows 11 x64 (MSYS2 UCRT64, D3D11) | ✅ | ✅ | ✅ |
| Linux | ⬜ | ⬜ | ⬜ |
| macOS | ⬜ | ⬜ | ⬜ |
