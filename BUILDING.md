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
`rotate` (a 30° preview about Z), `mirror`, `pattern` (their previews),
`holes` (the Hole tool with two countersunk holes, the second one's Y being
typed),
`arc` (a sketch with arcs), `polygon` (a center rectangle and a hexagon
being drawn: size and side-count labels, the -/+ counter) and
`constraints` (the same finished, plus a tangent arc, in the Select tool:
the constraint glyphs). Without `--screenshot` the window stays open.
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

This produces `dist/OpenShape/` (≈290 MB, 361 files, ~80 s): the
stripped executable, Qt (via `windeployqt6`), OCCT, PlaneGCS and runtime
DLLs, Qt plugins, QML modules, a `qt.conf`, and license files, including
THIRD_PARTY_LICENSES.txt with the license texts of all bundled MSYS2
packages (generated with `pacman`). Verified 2026-09-25 by
running the packaged `OpenShape.exe` with `PATH` reduced to
`C:\Windows\System32`, including the full `--acceptance` run (103/103).
This package of the development build links MSYS2's OCCT and therefore GPL
FFmpeg: the script says `License gate: FAIL - not distributable` (details in
`build/msys2-ucrt64/license-gate.txt`). For something to distribute, see
step 6.

### 6. Release: GPL-free build, installer and zip (verified)

The package from step 5 links MSYS2's OpenCASCADE, which pulls in GPL
FFmpeg: it is for local use only. A distributable release uses OpenShape's
own OCCT build (docs/LICENSING.md). All commands from Git Bash with both
MSYS2 bin folders first (`export PATH=$HOME/msys64/ucrt64/bin:$HOME/msys64/usr/bin:$PATH`),
in the repository root.

1. **OpenCASCADE without FFmpeg/FreeImage** (once; ~26 minutes with 6 jobs on
   this PC, probably over an hour on a 4-core CI runner). Needs `patch` (Git Bash has
   it; in MSYS2: `pacman -S patch`) and MSYS2's FreeType:

   ```bash
   scripts/windows/build-occt.sh $HOME/opt/occt-7.9.3-openshape 6
   ```

   It downloads the OCCT 7.9.3 tag and MSYS2's patches (SHA-256 checked),
   builds only the toolkits OpenShape links (28 DLLs, no TKOpenGl), installs
   them into the prefix and checks that `libTKDESTEP.dll`'s dependencies
   contain no GPL library. Sources and build tree stay in
   `$HOME/opt/occt-7.9.3-openshape-work` (635 MB; the prefix is 169 MB):
   delete it, or keep it for a quick rerun (unchanged sources and options
   are not rebuilt; the rerun still reinstalls the headers, ~10 minutes).

2. **Release build and tests** — the preset `msys2-ucrt64-release` finds OCCT
   in `$OPENSHAPE_OCCT_PREFIX`, default `%USERPROFILE%\opt\occt-7.9.3-openshape`
   (only there: the system's OCCT is never picked up). `ctest` runs the
   tests with the own OCCT first on `PATH` (a `TEST_LAUNCHER`; needs CMake
   3.29), and `OcctBuild.TestsLoadOpenShapesOwnOcct` fails if any OCCT
   toolkit is loaded from elsewhere (MSYS2's has the same version and would
   otherwise load silently). Running a test program or `OpenShape.exe` from
   the build folder by hand needs `PATH=$HOME/opt/occt-7.9.3-openshape/bin:$PATH`.

   ```bash
   cmake --preset msys2-ucrt64-release -DOPENSHAPE_WARNINGS_AS_ERRORS=ON -DOPENSHAPE_BUILD_TOOLS=ON
   cmake --build build/msys2-ucrt64-release -j 3
   ctest --test-dir build/msys2-ucrt64-release -LE gui -j 3
   ```

3. **Package** (with the license gate; fails for a release build that would
   ship anything GPL):

   ```bash
   bash scripts/package-windows.sh build/msys2-ucrt64-release dist/OpenShape
   ```

   → `dist/OpenShape/` (269 files, 158 MB, 45 s; the dev package of step 5
   has 361 files, 291 MB). It ends with `License gate: PASS`; the report
   lists the 24 bundled MSYS2 packages with their licenses.

4. **Installer, zip, checksums** (`pacman -S mingw-w64-ucrt-x86_64-nsis`; the
   zip uses MSYS2's Python):

   ```bash
   bash scripts/windows/make-installer.sh dist/OpenShape dist
   ```

   → `dist/OpenShape-<version>-windows-x64-setup.exe` (41 MB),
   `dist/OpenShape-<version>-windows-x64.zip` (61 MB), `dist/SHA256SUMS.txt`.
   The installer is per user (no administrator rights):
   `%LOCALAPPDATA%\Programs\OpenShape`, Start-menu entry, optional desktop
   shortcut (last page; `/DESKTOP` when silent), Apps & features entry,
   `.openshape` association; installing over an existing version replaces
   it; uninstalling keeps projects, settings and `%LOCALAPPDATA%\OpenShape`.
   Silent: `setup.exe /S [/DESKTOP] /D=C:\path` (`/D` last, unquoted);
   `Uninstall.exe /S`.

5. **Installer test** (installs into a test folder, checks files, registry,
   association and shortcuts, starts the installed app with `PATH` reduced
   to `C:\Windows\System32`, upgrades over it — also while it runs —,
   uninstalls and checks that everything is gone; refuses to run where
   OpenShape is installed):

   ```bash
   env OPENSHAPE_TEST_DESKTOP_DIR=build/installer-test/desktop bash scripts/windows/make-installer.sh dist/OpenShape build/installer-test
   ```

   It must print `TEST BUILD: desktop shortcut goes to ...` (the setup's
   Comments field then names that folder). Both installer tests refuse a
   setup that is not such a test build for their `-TestDesktopDir`: a
   release build puts its shortcut on the real desktop, replacing the
   owner's own `OpenShape.lnk` when the box is ticked and deleting it when
   it is unticked or uninstalled. (On 2026-09-26 a run of the dialog test
   with a setup built without the variable did exactly that, before this
   check existed.)

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\windows\test-installer.ps1 -Setup build\installer-test\OpenShape-0.1.0-windows-x64-setup.exe -InstallDir build\installer-test\Programs\OpenShape -PackageDir dist\OpenShape -TestDesktopDir build\installer-test\desktop -Screenshot build\installer-test\installed.png -TestRunningApp
   ```

   Verified 2026-09-25: 42/42 checks. The test installer puts its desktop
   shortcut into the given folder, not on the real desktop (where the
   owner's own `OpenShape.lnk` must stay untouched — the test checks that);
   `release.yml` runs the test without the app launches. The uninstaller's
   temporary copy (`%TEMP%\~nsu*.tmp`) stays until Windows cleans up.

   The dialogs (license page, folder page, finish page checkboxes, "Run
   OpenShape", the "OpenShape is running" Retry box, the uninstaller's
   pages) are clicked through by UI Automation, without moving the mouse;
   it holds OpenShape's automation lock meanwhile and needs a desktop
   session:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\windows\test-installer-dialogs.ps1 -Setup build\installer-test\OpenShape-0.1.0-windows-x64-setup.exe -InstallDir build\installer-test\Programs\OpenShape -TestDesktopDir build\installer-test\desktop
   ```

   Run on 2026-09-26: 53 of 56 checks passed (license text, folder page,
   finish-page defaults, "Run OpenShape" starting the app, upgrade offering
   the installed folder and showing the shortcut ticked, unticking removing
   it, Retry after closing the running app, the uninstaller's dialog
   removing files, registry entries and shortcuts). The 3 failures: the
   setup was not a test build (see above), so the shortcut went to the
   real desktop, replacing and then deleting the owner's `OpenShape.lnk`.
   Only the test-build check was added after that run (checked against a
   test and a release setup, not in a full run).

6. **The packaged app's full acceptance run**, with nothing but Windows on
   `PATH` (verified 2026-09-25: 174/174 checks — core, views, release — in
   29 s). From PowerShell:

   ```powershell
   $env:PATH = 'C:\Windows\System32'; dist\OpenShape\OpenShape.exe --acceptance build\acceptance-dist
   ```

The version is `project(OpenShape VERSION ...)` in `CMakeLists.txt`. A tag
`v<version>` pushed to GitHub runs `.github/workflows/release.yml`, which does
all of the above and publishes a GitHub Release (pre-release for 0.x);
manual runs and packaging changes on `main` only upload the files as a
workflow artifact. The Windows icon is made from the SVG with
`python scripts/windows/make-icon.py` (needs MSYS2's `rsvg-convert`,
`pacman -S mingw-w64-ucrt-x86_64-librsvg`).

### 7. Developer tools

- **Benchmark** — times a push/pull drag preview, tessellation, recompute,
  bounding boxes and recovery copies / full saves on a 21-face filleted part
  (and saves on a 21-body, 1528-face model), then the same plus a fillet
  drag and hover picking (1200 pointer positions over the part) on a
  249-face enclosure (shelled, rounded, 95 vent holes with chamfers, screw
  bosses; 25k triangles). Numbers in PROJECT_STATUS.md:

  ```bash
  cmake --preset msys2-ucrt64 -DOPENSHAPE_BUILD_TOOLS=ON
  cmake --build build/msys2-ucrt64 --target bench_session
  ./build/msys2-ucrt64/bin/bench_session.exe
  ```

- **Longer stress hunts** — the robustness suite runs a few fixed seeds;
  `OPENSHAPE_STRESS_SEEDS=N` runs N other seeds of each random session
  instead (undo/redo, interleaved, save/open; ~3 s each). A seed replays the
  same session on Windows and macOS (`tests/PortableRandom.h`: no standard
  distributions, whose output differs between libstdc++ and libc++).
  Failures print the action log and every body's steps with status:

  ```bash
  OPENSHAPE_STRESS_SEEDS=20 GTEST_FILTER='Seeds/*' ./build/msys2-ucrt64/bin/test_robustness.exe
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
