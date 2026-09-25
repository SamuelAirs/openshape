# OpenShape

**Precise solid CAD that feels direct.** Select a face, pull the arrow, type
`15`, press Enter — the part is now exactly 15 mm taller. Undo it. Round an
edge by typing its radius. Save, reopen, export STEP and STL for your slicer.

OpenShape is an original open-source CAD application for makers, 3D-printing
users and product designers, built on the OpenCASCADE exact B-rep kernel with
a Qt Quick interface designed for mouse, touch and stylus.

> Status: early development (Milestones 0 and 1 complete). Windows is the first
> verified platform. See [PROJECT_STATUS.md](PROJECT_STATUS.md).

## What works today

- Create boxes; push/pull any flat face with a draggable arrow or an exact
  typed value (units and arithmetic: `25`, `1in`, `20+5`)
- Fillet and chamfer edges (drag or type the size)
- Sketch on the ground or on any flat face: lines, rectangles, circles with
  snapping, horizontal/vertical inference and typed dimensions; constraint
  status ("Fully defined"); click a dimension to change it
- Extrude closed sketch profiles into new bodies, or join/cut into a body
- Live previews; failures explained in plain language, never corrupting the model
- Undo/redo for every change
- Orbit, pan, zoom, standard views, orthographic/perspective
- Hover highlighting, face/edge/body selection, touch-friendly tolerances
- `.openshape` project files that reopen with their full history (sketches stay editable)
- STEP export and import (kernel), STL export

## Quick start

See [BUILDING.md](BUILDING.md). In the app: **Add a box** (or press `B`),
click the top face, drag the arrow or type a number, press **Enter**.

Or sketch: press `K` (Sketch), click the origin, type `60`, `Tab`, `30`,
`Enter`, click **Finish sketch**, click inside the rectangle, type `5`,
`Enter` — a 60 × 30 × 5 mm plate.

| Action | Mouse | Touch |
|---|---|---|
| Orbit | drag empty space (left or right button) | one-finger drag |
| Pan | Shift+drag or middle-drag | two-finger drag |
| Zoom | wheel | pinch |
| Select | click (Shift/Ctrl adds) | tap (taps add; tap empty space to clear) |
| Select body | double-click | double-tap |
| Apply / cancel | Enter / Esc | ✓ / ✕ |

## Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md) — how it is built
- [ROADMAP.md](ROADMAP.md) — milestones and progress
- [BUILDING.md](BUILDING.md) — tested build steps
- [CONTRIBUTING.md](CONTRIBUTING.md) — rules and conventions
- [THIRD_PARTY.md](THIRD_PARTY.md) — dependencies and licenses
- [docs/](docs/) — technology evaluation, file format, topological naming,
  technical debt, development log

## License

Not yet chosen — see [LICENSE_PENDING.md](LICENSE_PENDING.md).
