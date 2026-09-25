# Contributing to OpenShape

## Product priorities

When trading things off: **interaction quality > modeling reliability >
discoverability > precision > performance > feature count.** A small number of
excellent, reliable operations beats a long list of mediocre ones. Do not add
UI for features that don't work yet — no buttons that lead to "TODO".

## Architecture rules

- OpenCASCADE headers are included **only** in `src/geometry/`. Everything
  else uses `geom::Shape` and the `geometry/*.h` APIs.
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

## Documentation

Keep `ARCHITECTURE.md`, `ROADMAP.md`, `PROJECT_STATUS.md` and `BUILDING.md`
in step with the code. Record notable discoveries in `docs/DEVLOG.md`.
Only document build commands that have actually been run.

## Licensing

The project license is still pending (see `LICENSE_PENDING.md`). Please don't
submit code you cannot license under common open-source licenses.
