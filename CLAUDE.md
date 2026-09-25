# OpenShape — notes for AI assistants picking up this project

Start here, in this order:
1. `PROJECT_STATUS.md` — what works, what's missing, next tasks, test counts.
2. `ARCHITECTURE.md` — how the code is organized (layers, key types).
3. `ROADMAP.md` — milestones with ✅/🟡/⬜ status.
4. `docs/DEVLOG.md` — discoveries and pitfalls; `docs/TECHNICAL_DEBT.md` — known shortcuts.
5. `BUILDING.md` — the exact, verified build/test/package commands.

## Working agreement with the owner
- Act as the lead engineer: decide engineering questions yourself, build, test,
  and keep the docs above in sync. Ask only about product-level decisions.
- Open decision: the project **license** (see `LICENSE_PENDING.md`).
- Never claim something works without running it. Prefer measurable geometry
  checks (volumes, bounding boxes) over screenshots.

## Environment (Windows, no admin rights)
- Toolchain: MSYS2 at `C:\Users\ayers\msys64` (UCRT64). In Git Bash:
  `export PATH=/c/Users/ayers/msys64/ucrt64/bin:$PATH`
- Build & test: `cmake --preset msys2-ucrt64 && cmake --build build/msys2-ucrt64 && ctest --test-dir build/msys2-ucrt64`
  (`-LE gui` skips the real-UI acceptance run, which moves the mouse).
- Visual check: `build/msys2-ucrt64/bin/OpenShape.exe --demo bracket --screenshot shot.png`
- Package: `bash scripts/package-windows.sh` (run in the MSYS2 UCRT64 shell) → `dist/OpenShape/`.
  The owner launches the app from a desktop shortcut to `dist/OpenShape/OpenShape.exe`,
  so re-run the package script after changes they should see.

## Pitfalls
- Python on Windows defaults to CRLF/cp1252: open files with `encoding='utf-8', newline=''`.
- Backslash escapes inside Bash heredocs get mangled: write scripts with the Write tool.
- Only `src/geometry` may include OpenCASCADE; only `src/render`, `src/ui`, `src/app` may use Qt.
