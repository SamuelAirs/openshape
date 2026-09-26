# OpenShape

**Direct-modeling CAD for makers and 3D printing.** Push and pull faces,
sketch a profile and extrude it, and type exact sizes as you go, in the
spirit of Shapr3D. Free and open source (MPL-2.0), built on the
OpenCASCADE solid-modeling kernel.

![OpenShape with a small project box: rounded corners, 2 mm walls and a cable hole whose diameter is being typed](docs/images/hero.png)

*A 60 × 40 × 25 mm project box made from a box: three sizes typed, the
corners rounded, the body shelled, a cable hole cut, and now the hole's
diameter set to 10.4 mm for print clearance. Every step in the Model panel
(right) can still be changed.*

**[Download for Windows](https://github.com/SamuelAirs/openshape/releases)** ·
[User guide](docs/USER_GUIDE.md) · [Build from source](BUILDING.md) ·
[Report a bug](https://github.com/SamuelAirs/openshape/issues)

> **Early pre-release (0.1).** OpenShape is young: it does the things below,
> but expect rough edges. It keeps recovery copies of unsaved work, and bug
> reports are very welcome.

## What it does

**Modeling**
- Click a flat face: it shows the part's size to the opposite side (height,
  width, depth). Drag the arrow or type the new size; `+5` / `-5` change it
  by that much. Fillets and chamfers around the face move with it (on
  straight walls).
- Fillet and chamfer edges, shell a body to a wall thickness, offset a face.
- Click a hole's wall and type its new diameter; select a hole, fillet or
  chamfer and press Delete to remove it (the faces around it close the gap).
- Extrude sketch profiles (a distance, both sides, or up to a face; as a new
  body, joined, or cut, also through everything) or revolve them.
- Lengths accept units and arithmetic: `25`, `1in`, `2,5cm`, `20+5`,
  `(10+2)*3`.

**Sketching**
- Sketch on the ground, an origin plane or any flat face.
- Line, rectangle, center rectangle, polygon, circle, arc, tangent arc and
  slot; trim, round a corner, offset, mirror and pattern curves.
- Type sizes while drawing; click a dimension later to change it.
- Constraints (horizontal, vertical, parallel, perpendicular, equal,
  tangent, concentric, midpoint, symmetric, distances, angles …) shown as
  small glyphs, and a "Fully defined" status.

**Bodies**
- Move, rotate (also about an edge or a hole), align one body to another or
  lay a face on the build plate.
- Mirror and pattern (in a row or around an axis), joined or as separate
  bodies; duplicate; split a body that fell apart into pieces.
- Union, subtract and intersect.
- The Model panel lists every step: change its values later, suppress,
  delete or hide it. Everything can be undone.

**Print helpers**
- Heat-set insert holes (M2 to M5 presets, depth adjustable).
- Hole diameters typed directly, e.g. for a tolerance.
- Measure: a clicked face shows its size to the opposite face; two faces or
  edges show their distance, gap or angle.
- Export STL or 3MF for your slicer (millimeters; 3MF keeps each body a
  separate object).

**Files**
- `.openshape` project files keep the full history: sketches and steps stay
  editable after reopening.
- STEP export for other CAD programs; recent files; preferences (units,
  sketch grid snapping, recovery copies).

**Reliability**
- Unsaved work is copied in the background and offered again after a crash.
- When a step cannot be done, OpenShape says why in plain words, often with
  a size that works, and the model stays as it was.
- The tools are exercised through the real window by an automated test
  run, and random modeling sessions check undo, redo and save/open.

## Platforms

| Platform | Status |
|---|---|
| Windows (64-bit; tested on Windows 11) | Installer and portable zip on the [Releases page](https://github.com/SamuelAirs/openshape/releases) |
| iPad and iPhone | In development: the app builds in CI; TestFlight testing is pending |
| macOS | Builds and passes the tests in CI; no download yet |
| Linux | Not tried yet (see [BUILDING.md](BUILDING.md)) |

OpenShape has a touch layout (larger controls, one finger orbits, two
fingers pan and zoom, two- and three-finger taps undo and redo) and a pen
mode; on Windows the touch layout appears once you touch the screen. So far
it has been tested with simulated touch input, not yet on a real tablet.

## Download and install (Windows)

1. Open the [Releases page](https://github.com/SamuelAirs/openshape/releases)
   and download `OpenShape-<version>-windows-x64-setup.exe` (or the `.zip`
   to run it without installing).
2. Run the installer. It installs for your user only (no administrator
   rights needed) and adds OpenShape to the Start menu.
3. The installer is **not code-signed yet**, so Windows SmartScreen may say
   "Windows protected your PC": click **More info**, then **Run anyway**.
   `SHA256SUMS.txt` on the release lets you check the download.

## Quick start: your first printed part

A 60 × 30 × 5 mm mounting plate with two 6 mm holes:

1. **Sketch.** Press `K` (or click **Sketch**, then **Top (XY)**): you are
   drawing on the ground with the Rectangle tool. Click where the red and
   green axes cross, type `60`, press `Tab`, type `30`, press `Enter`. Click
   **Finish sketch**.
2. **Extrude.** Click inside the rectangle, type `5`, press `Enter`: a
   5 mm thick plate.
3. **Holes.** Click the plate's top face and press `K` to sketch on it.
   Press `C` (Circle), click where a hole goes, type `6`, `Enter`; again for
   the second hole. Click **Finish sketch**.
4. **Cut.** Click inside one circle, `Shift`-click the other, type `-5`,
   press `Enter`: two holes through the plate. (Once the value is negative
   the extrude is a cut, and a **Through all** button appears for cutting
   through any thickness.)
5. **Print.** **File → Export 3MF…** (or **Export STL…**), open the file in
   your slicer (it is in millimeters) and print. **File → Save** keeps the
   editable project.

The [user guide](docs/USER_GUIDE.md) explains every tool, gesture and
shortcut; in the app, press `F1` or the **?** button for the same on one
card.

## Documentation

- [docs/USER_GUIDE.md](docs/USER_GUIDE.md): using OpenShape
- [BUILDING.md](BUILDING.md): building, testing and packaging from source
- [CONTRIBUTING.md](CONTRIBUTING.md): how to contribute
- [ARCHITECTURE.md](ARCHITECTURE.md): how the code is organized
- [ROADMAP.md](ROADMAP.md) and [PROJECT_STATUS.md](PROJECT_STATUS.md):
  where the project stands
- [docs/](docs/): file format, licensing, technical debt, development log

## License

OpenShape is free software under the [Mozilla Public License 2.0](LICENSE).
Third-party components keep their own licenses: see
[THIRD_PARTY.md](THIRD_PARTY.md), and [docs/LICENSING.md](docs/LICENSING.md)
for what that means for distribution.
