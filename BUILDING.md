# Building OpenShape

Only instructions that have actually been run are listed. Verified on
2026-09-25: Windows 11 Pro (26200), no administrator rights.

## Windows (MSYS2 UCRT64) — verified

### 1. Install MSYS2

Either run the official installer from <https://www.msys2.org> (installs to
`C:\msys64`), or — without admin rights, as verified here — extract the base
archive into your profile:

```bash
curl -L -o msys2-base.tar.xz https://github.com/msys2/msys2-installer/releases/download/nightly-x86_64/msys2-base-x86_64-latest.tar.xz
tar -xf msys2-base.tar.xz -C "$USERPROFILE"
```

Start the **MSYS2 UCRT64** shell (`msys64\ucrt64.exe`) once; it initializes
its keyring on first launch.

### 2. Install the toolchain and libraries (UCRT64 shell)

```bash
pacman -Syu --noconfirm
pacman -S --needed --noconfirm \
  mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-gdb \
  mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-opencascade \
  mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-declarative \
  mingw-w64-ucrt-x86_64-qt6-shadertools mingw-w64-ucrt-x86_64-qt6-svg \
  mingw-w64-ucrt-x86_64-gtest mingw-w64-ucrt-x86_64-nlohmann-json \
  mingw-w64-ucrt-x86_64-libzip mingw-w64-ucrt-x86_64-eigen3
```

Verified versions: GCC 16.2.0, CMake 4.4.3, Ninja 1.13.2, OCCT 7.9.3,
Qt 6.11.2, GTest 1.18.0, nlohmann-json 3.12.0, libzip 1.11.4, Eigen 5.0.1.
The PlaneGCS sketch solver is vendored in `third_party/planegcs` and built
from source (as a C++23 shared library) automatically.

### 3. Configure, build, test (UCRT64 shell, in the repository root)

```bash
cmake --preset msys2-ucrt64
cmake --build build/msys2-ucrt64
ctest --test-dir build/msys2-ucrt64 --output-on-failure
```

`ctest` includes `acceptance_gui`, which opens the application window and
drives it with synthetic input for ~20 s (it moves the mouse cursor, so
don't use the machine meanwhile). On a machine without a desktop session,
skip it:

```bash
ctest --test-dir build/msys2-ucrt64 -LE gui
```

Options: `-DOPENSHAPE_BUILD_APP=OFF` builds only the Qt-free core and its
tests; `-DOPENSHAPE_WARNINGS_AS_ERRORS=ON` makes warnings fatal (CI uses it,
so use it locally too — a new enum value without its `switch` cases fails
there); `-DOPENSHAPE_BUILD_TOOLS=ON` adds the developer tools (see below).

### 4. Run

From the UCRT64 shell (so Qt and OCCT DLLs are on `PATH`):

```bash
./build/msys2-ucrt64/bin/OpenShape.exe
./build/msys2-ucrt64/bin/OpenShape.exe path/to/model.openshape
```

Developer switches:

```bash
./build/msys2-ucrt64/bin/OpenShape.exe --demo bracket --screenshot shot.png
./build/msys2-ucrt64/bin/OpenShape.exe --touch   # the tablet layout (bigger controls, Pen switch)
./build/msys2-ucrt64/bin/OpenShape.exe --touch --size 820x1180 --demo combine --screenshot ipad.png   # iPad portrait layout
./build/msys2-ucrt64/bin/OpenShape.exe --acceptance out-dir
./build/msys2-ucrt64/bin/OpenShape.exe --acceptance out-dir --scenario views   # one scenario (comma-separated list)
OPENSHAPE_LOG=debug ./build/msys2-ucrt64/bin/OpenShape.exe
./build/msys2-ucrt64/bin/OpenShape.exe --data-dir some-dir   # settings, recovery copies and log in some-dir
./build/msys2-ucrt64/bin/OpenShape.exe --data-dir some-dir --simulate-crash   # then start with --data-dir some-dir: it offers the box
./build/msys2-ucrt64/bin/OpenShape.exe --data-dir some-dir --simulate-quit    # ended with unsaved work: offered too
```

Demo scenes: `empty`, `hover`, `pushpull` (the cube's top face set to a
35 mm height), `committed`, `fillet`, `move`,
`sketch`, `sketchdone`, `extrude`, `bracket`, `revolve`, `combine` (two bodies
selected), `history` (a fillet step highlighted from the Model panel),
`rotate` (a 30° preview about Z), `mirror`, `pattern` (their previews) and
`arc` (a sketch with arcs). Without `--screenshot` the window stays open.
`OPENSHAPE_LOG=debug` adds per-operation timings (PERFORMANCE category:
tessellation, recompute, kernel operations) to the log.

Where the app keeps things (Windows):

- **Log:** stderr and `%LOCALAPPDATA%\OpenShape\OpenShape\logs\openshape.log`
  (4 MB, then it starts over). An unexpected termination adds one line
  ("OpenShape closed unexpectedly: exception 0xC0000005 (access violation)
  at Module.dll+0x…"); map the offset with `addr2line -e <module>`.
- **Settings** (preferences, recent files, window place): the registry,
  `HKEY_CURRENT_USER\Software\OpenShape\OpenShape`.
- **Recovery copies** of unsaved work: `%LOCALAPPDATA%\OpenShape\OpenShape\recovery\`
  (`<session>.openshape` + `.json` + `.lock` per running app; see
  docs/FILE_FORMAT.md). They are removed on Save, New, Open and when the
  user chooses Don't Save. If the app ends while there is unsaved work the
  user did not discard (a crash, iPadOS ending the app, logging off), the
  next start offers it.
- `--data-dir <dir>` puts all three into `<dir>` (`logs/`, `settings/` as an
  INI file, `recovery/`). `--acceptance`, `--demo` and `--screenshot` use a
  temporary folder for settings and recovery copies (removed at exit; the log
  still goes to the usual file), never the user's, and show no restore prompt
  and do not remember the window.
- `--simulate-crash` (no window): adds a box, writes its recovery copy and
  crashes with an access violation (without the Windows crash dialog);
  `--simulate-quit` adds a box and quits at once without asking (as when
  iPadOS ends the app), which must keep the box as a recovery copy. The
  `recovery` acceptance scenario uses both.

Building from another shell (e.g. Git Bash) also works if
`<msys64>/ucrt64/bin` is first on `PATH`.

### 5. Package a self-contained folder (verified)

```bash
pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-ntldd
bash scripts/package-windows.sh
```

From Git Bash instead of the UCRT64 shell, put both MSYS2 bin folders first:

```bash
export PATH=$HOME/msys64/ucrt64/bin:$HOME/msys64/usr/bin:$PATH
bash scripts/package-windows.sh
```

Don't build while the script runs: it copies from the build folder.

This produces `dist/OpenShape/` (≈290 MB, 364 files, ~5 minutes): the
stripped executable, Qt (via `windeployqt6`), OCCT, PlaneGCS and runtime
DLLs, Qt plugins, QML modules, a `qt.conf`, and license files, including
THIRD_PARTY_LICENSES.txt with the license texts of all bundled MSYS2
packages (generated with `pacman`). Verified 2026-09-25 by
running the packaged `OpenShape.exe` with `PATH` reduced to
`C:\Windows\System32`, including the full `--acceptance` run (103/103).
No installer yet, and not distributable yet (TD-17).

### 6. Developer tools

- **Benchmark** — times a push/pull drag preview, tessellation, recompute,
  bounding boxes and recovery copies / full saves on a 21-face filleted part
  (and saves on a 21-body, 1528-face model) (numbers in
  PROJECT_STATUS.md):

  ```bash
  cmake --preset msys2-ucrt64 -DOPENSHAPE_BUILD_TOOLS=ON
  cmake --build build/msys2-ucrt64 --target bench_session
  ./build/msys2-ucrt64/bin/bench_session.exe
  ```

- **`scripts/dev/drive.py`** — drives a running OpenShape window with real
  mouse and keyboard input and captures screenshots, for exploring the UI
  like a user (it takes over the mouse). Needs a Windows-native Python; the
  MSYS2 one works:

  ```bash
  $HOME/msys64/ucrt64/bin/python.exe scripts/dev/drive.py "focus; info; shot view.png 0.5"
  ```

  Paths inside the command string must be Windows paths (`C:/...`): the
  shell does not convert them. Coordinates are window client pixels, as in
  the screenshots.

- **`scripts/dev/ci_status.py`** — GitHub Actions results without a login:
  runs, steps, and the annotations where failed steps put their errors
  (`python scripts/dev/ci_status.py status`, or `watch` in the background
  after a push). See docs/IPAD.md, "Reading CI results".

- **`scripts/dev/watch_log.py`** — while someone uses the app started with
  `OPENSHAPE_LOG=debug`, prints slow operations, warnings, messages shown to
  the user, failed previews and GUI-thread stalls, one line per event
  (run it in the background).

## macOS and iPad — prepared, not yet verified

A `macos` preset (Homebrew packages) and a macOS CI job exist; the iPad build
is described step by step in [docs/IPAD.md](docs/IPAD.md). Neither has been
run on a Mac yet.

## Other platforms — not yet verified

The CMake project uses only `find_package` for OCCT (≥ 7.8 recommended),
Qt ≥ 6.8 (Core, Gui, GuiPrivate, Qml, Quick, QuickControls2, ShaderTools),
nlohmann_json, libzip, Eigen3 and GTest, and needs a C++23 compiler for the
vendored solver, so Linux distribution packages and Homebrew
should work with the `linux` preset. This has **not** been run yet.
