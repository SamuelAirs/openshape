# Contributing to OpenShape

## Product priorities

When trading things off: **interaction quality > modeling reliability >
discoverability > precision > performance > feature count.** A small number of
excellent, reliable operations beats a long list of mediocre ones. Do not add
UI for features that don't work yet — no buttons that lead to "TODO".

## Architecture rules

- OpenCASCADE headers are included **only** in `src/geometry/`. Everything
  else uses `geom::Shape` and the `geometry/*.h` APIs.
- Every public geometry function that reads or writes OpenCASCADE data runs
  inside a try block that starts with `OS_KERNEL_SIGNALS_TO_EXCEPTIONS` (or
  inside `guarded()`), or holds a `detail::KernelLock`
  (`geometry/internal/KernelUtil.h`). The preview worker runs kernel code
  while the GUI thread does too (ARCHITECTURE.md, "Threads"): a bare
  `BRepGProp` or `BRepMesh` call races with it, fails at random and no test
  catches it reliably.
- Only `src/render`, `src/ui` and `src/app` may depend on Qt.
- Every document mutation is a `cmd::Command` pushed on the `UndoStack`.
  Commands reference objects by UUID, never by pointer.
- Kernel calls return `Result`/`Status` with a plain-language `userMessage` and
  a technical `developerMessage`. Never let a kernel exception escape; never
  swallow an error silently.
- Internal lengths are millimeters, angles radians. Convert only at the UI.
- Touch must remain a first-class input: no workflow may require hover,
  right-click, tiny targets or keyboard modifiers.
- Imported files (projects, STEP, STL) are untrusted input.

## Code style

C++20, 4-space indent, `camelCase` functions, `PascalCase` types, trailing `_`
for members, `#pragma once`. Prefer clear, boring code; no clever
metaprogramming without strong reason. Keep comments for *why*, not *what*.

Mark intentional shortcuts with a milestone tag and record significant ones in
`docs/TECHNICAL_DEBT.md`:

```cpp
// TODO(OpenShape-M3): tessellate on a worker thread for large models.
```

## Tests

Every change to geometry, document, commands, io or interaction needs tests
using **measurable invariants** (volumes, bounding boxes, face counts, exact
values) rather than screenshots. Run everything before sending a change:

```bash
cmake --build build/msys2-ucrt64
ctest --test-dir build/msys2-ucrt64 --output-on-failure
```

`acceptance_gui` needs a desktop session; exclude it on headless machines with
`-LE gui`.

A failing test means the code, the test, or an assumption is wrong — find out
which. Do not disable tests to get green.

Two rules learned the hard way:

- **Every user-facing action gets an acceptance check that reaches it by
  clicking** (e.g. `clickItem("barAction_subtract")`). Headless tests once
  passed while Union/Subtract could not be reached in the UI at all. The
  acceptance run is split into scenarios: `core` (the original story in
  `src/app/AcceptanceRunner.cpp`) and one file per feature area in
  `src/app/acceptance/` that registers itself (see `AcceptanceScenario` in
  `AcceptanceRunner.h`; `acceptance/Views.cpp` is a short example). Each
  scenario starts from a new document. Run one with
  `OpenShape.exe --acceptance <dir> --scenario <name>`. Automated runs
  (`--acceptance`, `--demo`, `--screenshot`) take a machine-wide lock, so
  parallel builds cannot run two at once (they wait their turn).
- **Verify kernel results against an invariant of the intent** (volume
  change, bounding box, face count), not only `IsDone()` and `BRepCheck`:
  OCCT's shell, defeaturing and per-face offset can all report success with
  an unchanged or wrong solid.

Build with `-DOPENSHAPE_WARNINGS_AS_ERRORS=ON` (CI does). A new enum value
without its `switch` cases (`-Wswitch`) or a struct initializer missing a
field (`-Wmissing-field-initializers`) fails CI.

## Checklists for common extensions

**A new history step (feature kind)**
1. `document/Feature.h/.cpp`: the class (`compute`, `parameters` /
   `setParameter`, `writeParams` / `readParams`, `clone`, `dependencies` and
   `remapReferences` if it reads sketches, other bodies or their steps —
   Duplicate re-points copied references through it); append to
   `FeatureKind`; add it to `toString`, `featureKindFromString` and
   `createFeature`.
2. `commands/DocumentCommands.cpp`: its undo label.
3. `interaction/InteractionController.cpp`: `featureTitle` and
   `featureDetail` (Model panel).
4. `docs/FILE_FORMAT.md`: its params. Unknown types make older builds refuse
   the file, which is intended.
5. Tests: geometry of `compute`, and a project-file round trip
   (`tests/test_project_file.cpp`).

**A new operation or tool**
1. `interaction/Operation.h/.cpp`: an `Operation` subclass (`makeFeature`,
   `title`, `valueLabel`; hooks such as `prompt()`, `neutralValue()`,
   `labelAnchor()` as needed). Give it `clone()` (one line, like the
   others) or its previews stay on the GUI thread; `canCommit()` uses
   `previewUsable()` (a pending preview counts); an automatic choice made
   from the preview needs `adoptAutomaticChoices()` and
   `commitNeedsPreview()`. Everything its preview reads must come from the
   operation's own members and the document passed in (a snapshot on the
   worker), never from the controller. A setter that changes what the
   preview computes (a mode, a count, a target) calls `setValue(value(),
   document)` with the default `Change::Parameters`, so a result computed
   before the change is not shown; only a pure value change (a drag step, a
   typed value) passes `Change::ValueOnly`. A query the GUI thread makes
   while hovering or dragging (snapping, picking, labels) must not call the
   kernel: the kernel lock makes it wait for the worker's preview (use the
   display mesh, the operation's own members or a memoized result).
2. `InteractionController`: arm it in `rebuildOperation()`, offer it in
   `contextActions()`, handle its id in `triggerAction()`, and in `runTool()`
   when it belongs in the palette.
3. `ui/qml/Main.qml`: palette entry (Modify/Combine) and its hint in
   `hintText()`; `ui/qml/HelpOverlay.qml`: a row.
4. Tests: a headless flow in `tests/test_interaction.cpp` and an acceptance
   step that clicks it.

**A new sketch entity or constraint**
1. `sketch/Sketch.h/.cpp`: the entity, or an appended `ConstraintKind` with
   its name (`toString` / parsing) and validity check; JSON; the removal
   cascade.
2. `sketch/SketchSolver.cpp`: the PlaneGCS mapping.
3. Entities also need `document/SketchProfiles.cpp` and `geometry/Profiles`
   (curves for regions), `SketchSession` picking, drawing and labels, and
   `InteractionController::renderScene` (sketches outside edit mode).
4. `SketchSession::contextActions()` / `triggerAction()` for the button;
   `ui/qml/SketchOverlay.qml` for a new tool.
5. Tests: `tests/test_sketch.cpp` (solved geometry, JSON) and
   `tests/test_sketch_interaction.cpp` (the tool or action).

## Documentation

Keep `ARCHITECTURE.md`, `ROADMAP.md`, `PROJECT_STATUS.md` and `BUILDING.md`
in step with the code. Record notable discoveries in `docs/DEVLOG.md` and
intentional shortcuts in `docs/TECHNICAL_DEBT.md`. Only document build
commands that have actually been run. User-visible behaviour also belongs in
the in-app help card (`HelpOverlay.qml`).

## Licensing

OpenShape is licensed under the Mozilla Public License 2.0 (`LICENSE`), and
contributions are accepted under the same license. Every source file starts
with the MPL notice; add it to new files (copy it from any existing file).
Code in `third_party/` keeps its upstream license. See `docs/LICENSING.md`.
