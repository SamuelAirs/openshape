# OpenShape

*Open source CAD based 3D design software specialized for touch operation and
3D printing.*

**Precise solid CAD that feels direct.** Select a face, pull the arrow, type
`15`, press Enter — the part is now exactly 15 mm taller. Undo it. Round an
edge by typing its radius. Save, reopen, export STEP and STL for your slicer.

OpenShape is an original open-source CAD application for makers, 3D-printing
users and product designers, built on the OpenCASCADE exact B-rep kernel with
a Qt Quick interface designed for mouse, touch and stylus.

> Status: early development (Milestones 0–2 complete, editable history, first
> maker tools). Windows is the first verified platform; iPad is planned. See
> [PROJECT_STATUS.md](PROJECT_STATUS.md).

## What works today

- Create boxes; push/pull any flat face with a draggable arrow or an exact
  typed value (units and arithmetic: `25`, `1in`, `20+5`)
- Fillet and chamfer edges (drag or type the size); shell a body
- Sketch on the ground, an origin plane or any flat face: lines, rectangles,
  circles and arcs with snapping, horizontal/vertical inference and typed
  dimensions; constraints (parallel, perpendicular, equal, tangent,
  concentric, midpoint, …) and construction lines; click a dimension to
  change it; sketch on a sketch to add to it
- Extrude or revolve closed sketch profiles into new bodies, or join/cut into
  a body
- Move, rotate, align (face to face, edge to edge, hole to shaft, or onto the
  build plate), mirror and pattern bodies
- Combine bodies: union, subtract, intersect (select bodies by double-click
  or in the Model panel; Shift adds)
- Direct face edits: select a hole, fillet or chamfer and press Delete to
  remove it; click a hole's wall and type its new diameter
- Heat-set insert pilot holes (M2–M5 presets)
- Live previews; failures explained in plain language, never corrupting the model
- Undo/redo for every change
- Model panel: hover a step to see what it made; click a step to change its
  values later
- Orbit, pan, zoom, standard views, orthographic/perspective
- Hover highlighting, face/edge/body selection, touch-friendly tolerances;
  with a pen, the pen draws and fingers only navigate
- `.openshape` project files that reopen with their full history (sketches stay editable)
- Measure wall thickness, distances and angles between faces/edges
- STEP export (and import in the kernel), STL and 3MF export for slicers
- `F1` (or the `?` button) shows every gesture, shortcut and tool

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
| Stop drawing lines | right-click or Esc | ✕ |
| Undo / redo | Ctrl+Z / Ctrl+Y | two-finger tap / three-finger tap |
| Help | F1 | ? button |

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
