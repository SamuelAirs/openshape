# OpenShape — notes for AI assistants picking up this project

Start here, in this order:
1. `PROJECT_STATUS.md` — "Handoff: where we left off" first; then what works,
   what's missing, next tasks, test counts.
2. `ARCHITECTURE.md` — how the code is organized (layers, key types).
3. `ROADMAP.md` — milestones with ✅/🟡/⬜ status.
4. `docs/DEVLOG.md` — discoveries and pitfalls; `docs/TECHNICAL_DEBT.md` — known shortcuts.
5. `BUILDING.md` — the exact, verified build/test/package commands, demo scenes
   and developer tools.
6. `CONTRIBUTING.md` — rules and checklists for adding a feature, tool or constraint.
7. `docs/IPAD.md` (the iPad plan, for a session on the owner's Mac) and
   `docs/MANUAL_TESTS.md` (what the owner is asked to try).

## Working agreement with the owner
- Act as the lead engineer: decide engineering questions yourself, build, test,
  and keep the docs above in sync. Ask only about product-level decisions.
- License: **MPL-2.0** (the owner's choice, 2026-09-25). New source files start
  with the MPL notice (copy it from any file); `third_party/` keeps its own
  licenses. See `docs/LICENSING.md`.
- Never claim something works without running it. Prefer measurable geometry
  checks (volumes, bounding boxes) over screenshots.

## Environment (Windows, no admin rights)
- Toolchain: MSYS2 at `%USERPROFILE%\msys64` (UCRT64). In Git Bash:
  `export PATH=$HOME/msys64/ucrt64/bin:$PATH`
- Build & test: `cmake --preset msys2-ucrt64 && cmake --build build/msys2-ucrt64 && ctest --test-dir build/msys2-ucrt64`
  (`-LE gui` skips the real-UI acceptance run: ~30 s, moves the mouse — don't run it
  while the owner is using the machine). The local build cache has
  `OPENSHAPE_WARNINGS_AS_ERRORS=ON` like CI, and `OPENSHAPE_BUILD_TOOLS=ON`.
- Visual check: `build/msys2-ucrt64/bin/OpenShape.exe --demo bracket --screenshot shot.png`
- Package: `export PATH=$HOME/msys64/ucrt64/bin:$HOME/msys64/usr/bin:$PATH && bash scripts/package-windows.sh`
  → `dist/OpenShape/`. The owner launches the app from a desktop shortcut to
  `dist/OpenShape/OpenShape.exe`, so re-run the package script after changes they
  should see. Don't build while it runs (it copies from the build folder).
- Python: `$HOME/msys64/ucrt64/bin/python.exe` is Windows-native (ctypes works).

## Repository and CI
- Remote: https://github.com/SamuelAirs/openshape (**public** since 2026-09-25).
  `gh` is not installed; git over HTTPS works with the owner's stored credentials.
- CI runs on every push (free: public repository); docs-only pushes skip it.
  `ci.yml`: Windows (MSYS2) and macOS (Apple Clang, Homebrew), warnings as errors.
  `ipad.yml`: builds the iPad app on a GitHub Mac and, on `main`, uploads it to
  TestFlight for the owner's iPad (docs/IPAD.md; the owner has no usable Mac).
- Reading results needs no login: run/job status and annotations come from the
  public API, and failed steps put their error lines into annotations
  (docs/IPAD.md, "Reading CI results"). Full logs need the owner's login.
- Commit identity for this repo is set locally (GitHub noreply address).

## Debugging with the owner
- `OPENSHAPE_LOG=debug` logs per-operation timings; the owner likes to model while
  Claude watches the log with `scripts/dev/watch_log.py` (slow steps, warnings,
  GUI stalls) and reports findings.
- `scripts/dev/drive.py` drives the real window (mouse, keys, screenshots);
  `tools/bench/bench_session.cpp` times previews/recompute. See BUILDING.md.
- Measure before optimizing: the 2026-09-25 slowdown was the bounding box, not meshing.
- Every user-facing action needs an acceptance check that clicks it
  (`src/app/AcceptanceRunner.cpp`).

## Pitfalls
- Python on Windows defaults to CRLF/cp1252: open files with `encoding='utf-8', newline=''`.
- Backslash escapes inside Bash heredocs get mangled (a `\n` inside a Python string
  literal passed via heredoc becomes a real newline): write such files with the
  Write/Edit tools.
- Only `src/geometry` may include OpenCASCADE; only `src/render`, `src/ui`, `src/app` may use Qt.
