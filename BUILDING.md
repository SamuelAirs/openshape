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
Qt 6.11.2 (6.9 or newer is required: `SafeArea`), GTest 1.18.0, nlohmann-json 3.12.0, libzip 1.11.4, Eigen 5.0.1.
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
./build/msys2-ucrt64/bin/OpenShape.exe --demo enclosure --view 150,25 --screenshot back.png   # from any angle, framed (see below)
./build/msys2-ucrt64/bin/OpenShape.exe --demo bracket --projection orthographic --screenshot ortho.png   # start in this projection (not remembered)
./build/msys2-ucrt64/bin/OpenShape.exe --demo rounded --view=-135,35 --face-contrast --screenshot s.png   # log how different adjacent faces look
./build/msys2-ucrt64/bin/OpenShape.exe --touch   # the tablet layout (bigger controls, Pen switch)
./build/msys2-ucrt64/bin/OpenShape.exe --touch --size 820x1180 --demo combine --screenshot ipad.png   # iPad portrait layout
./build/msys2-ucrt64/bin/OpenShape.exe --touch --size 402x874 --safe-area 62,0,34,0 --demo bracket --screenshot phone.png   # iPhone 16 Pro, portrait
./build/msys2-ucrt64/bin/OpenShape.exe --touch --size 874x402 --safe-area 0,62,21,62 --demo sketch --screenshot phone-landscape.png
./build/msys2-ucrt64/bin/OpenShape.exe --touch --size 402x874 --safe-area 62,0,34,0 --demo fillet --screenshot phone-fillet.png   # the value chip docked away from the edge
./build/msys2-ucrt64/bin/OpenShape.exe --acceptance out-dir --scenario chipplacement   # the chip vs. the tapped edge / face at phone, iPad and desktop sizes; on the phone its buttons and the dock while typing
./build/msys2-ucrt64/bin/OpenShape.exe --acceptance out-dir --scenario licenses,release   # About → Licenses (every library's text, the LGPL notice, at iPhone size too) and the About card
./build/msys2-ucrt64/bin/OpenShape.exe --touch --size 402x874 --safe-area 62,0,34,0 --demo licenses --screenshot licenses.png   # the Licenses list on an iPhone
./build/msys2-ucrt64/bin/OpenShape.exe --acceptance out-dir --scenario extrudetouch   # a pocket cut on the phone layout with one finger (framed face sketch, arrow and profile drags, Cut / Join / Flip), then with the mouse
./build/msys2-ucrt64/bin/OpenShape.exe --acceptance out-dir --scenario sketchdrag   # sketch editing by dragging: mouse on the desktop, then one finger at 402x874 (touchPress/Move/Release)
./build/msys2-ucrt64/bin/OpenShape.exe --acceptance out-dir --scenario multiselect   # two bodies selected by finger double-taps at iPad and iPhone sizes, Union
./build/msys2-ucrt64/bin/OpenShape.exe --acceptance out-dir
./build/msys2-ucrt64/bin/OpenShape.exe --acceptance out-dir --scenario views   # one scenario (comma-separated list)
./build/msys2-ucrt64/bin/OpenShape.exe --acceptance out-dir --scenario construct,alignorigin   # construction axes and planes, Align onto the origin
./build/msys2-ucrt64/bin/OpenShape.exe --acceptance out-dir --scenario appfolder,share,openin   # iPhone / iPad files: saving by name, the share sheet (stub), Open in OpenShape
./build/msys2-ucrt64/bin/OpenShape.exe --acceptance out-dir --size 1024x653     # at the CI Mac's window size
OPENSHAPE_LOG=debug ./build/msys2-ucrt64/bin/OpenShape.exe
./build/msys2-ucrt64/bin/OpenShape.exe --data-dir some-dir   # settings, recovery copies and log in some-dir
./build/msys2-ucrt64/bin/OpenShape.exe --data-dir some-dir --simulate-crash   # then start with --data-dir some-dir: it offers the box
./build/msys2-ucrt64/bin/OpenShape.exe --data-dir some-dir --simulate-quit    # ended with unsaved work: offered too
```

Demo scenes: `empty`, `hover`, `pushpull` (the cube's top face set to a
35 mm height), `committed`, `fillet`, `rounded` (the fillet applied),
`move`,
`sketch`, `sketchdone`, `extrude`, `bracket`, `revolve`, `combine` (two bodies
selected), `history` (a fillet step highlighted from the Model panel),
`rotate` (a 30° preview about Z), `mirror`, `pattern` (their previews),
`holes` (the Hole tool with two countersunk holes, the second one's Y being
typed), `text` (the Text tool: "Hello" raised 1 mm on a 20 x 20 x 5 plate,
4 mm capitals, being previewed; needs the built-in font, see below),
`arc` (a sketch with arcs), `polygon` (a center rectangle and a hexagon
being drawn: size and side-count labels, the -/+ counter) and
`constraints` (the same finished, plus a tangent arc, in the Select tool:
the constraint glyphs) and `home` (the start screen with four saved
projects and their previews; try `--touch --size 402x874`, `874x402` and
`1180x820` for phones and the iPad) and `enclosure` (a 60 x 40 x 25 mm
project box built from a box: sizes typed, corners rounded, shelled, a
cable hole cut, and the hole's diameter being set to 10.4 mm; the
README's picture); the App Store scenes `store-*` (below). Panels to look at (layout checks at phone
sizes): `help`, `about`, `licenses` (About → Licenses), `preferences`, `savename` (the overlay open),
`modelpanel` (the compact layout's Model panel open on the `history` scene)
and `viewmenu` (the compact View menu open on `combine`). Without
`--screenshot` the window stays open. A normal start without a file opens on Home; automated runs
(`--acceptance`, `--demo`, `--screenshot`) start in an empty document.

**Camera for screenshots.** `--view` looks at a demo scene from a standard
view (`iso`, `front`, `back`, `left`, `right`, `top`, `bottom`) or from
`yaw,pitch` in degrees (yaw -90 is the front view, 0 looks from +X; pitch 90
from above, negative from below; write `--view=-60,12` when yaw is
negative), with everything framed; several views separated by `;`
(`--view "iso;front;30,20"`) save one screenshot each, `<file>-<view>.png`,
from one run. `--projection perspective|orthographic`
starts in that projection without remembering it (automated runs start in
perspective, the default, as their settings file is new). `--face-contrast`
(with `--screenshot`) first clears the value, selection and hover, then logs
one line measured on the saved image (`app/FaceContrast`): the faces
measured, the pairs meeting at a sharp edge, their smallest and median
difference in shade (luma, 0-255), how many differ by less than 8 levels,
and the darkest and lightest face.

**Phone and Split View layouts.** Below 600 logical px wide or 500 tall the
window gets the compact layout (tools in a strip along the bottom, Model and
View buttons); `--size WxH` sets any size (also below the desktop minimum),
e.g. 402x874 / 874x402 (iPhone 16 Pro), 375x667 (a small iPhone), 500x800
(half an iPad in Split View), 1180x820 / 820x1180 (iPad Air 11"). A window
resized while it runs switches layouts live. `--safe-area top,right,bottom,left`
(logical px) simulates a phone's safe-area insets and shades them, with the
Dynamic Island and the home indicator drawn in: iPhone 16 Pro portrait
`62,0,34,0`, landscape `0,62,21,62` (an iPad: `24,0,20,0`).
`--app-folder <dir>` saves and exports as on an iPhone or iPad: Save asks for
a name only and writes `<dir>/<name>.openshape`, exports go to
`<dir>/Exports` (docs/IPAD.md, "Files on iPhone and iPad"); the `savename`
demo scene shows that prompt. The share sheet exists only on iOS (on the
desktop there is no File → Share Project…); the `share` scenario puts a
stub in, and `openin` hands files over the way Qt's iOS delegate does.

**App Store screenshots.** `bash scripts/dev/appstore_screenshots.sh
[names...]` (docs/APP_STORE.md, section 3) renders the six App Store scenes
(`store-enclosure`, `store-text`, `store-sketch`, `store-planes`,
`store-history`, `store-home`; `src/app/StoreScenes.cpp`, built with the
same tools a user taps) for the iPhone 6.9" (`--size 440x956 --dpr 3
--safe-area 62,0,34,0`, 1320 x 2868 pixels) and the iPad 13" (`--size
1376x1032 --dpr 2 --safe-area 24,0,20,0`, 2752 x 2064) into
`docs/appstore/screenshots/`, then checks them with
`scripts/dev/check_appstore_screenshots.py` (exact size, no alpha channel,
not blank); about three minutes. The pictures are rendered into
`build/appstore-screenshots/` and copied into `docs/` only when the whole
set passes the check; interrupting the script (Ctrl+C, closing the terminal,
killing it) closes the window in progress and starts no further one, and
only one copy runs at a time. `--dpr <factor>` draws at that device pixel
ratio whatever the monitor's scaling (it turns off Qt's use of the screen's
scale). `--store-screenshot` (with `--screenshot` and `--size`) makes the
window frameless and exactly `--size`, also taller than the desktop (Windows
keeps framed windows within its height), stops the view reacting to the mouse
pointer and moves the pointer off the window, keeps the `--safe-area` insets
free without shading them, hides a message left by building the scene, and
saves the picture without an alpha channel (App Store Connect refuses those);
a scene that has not produced its picture 3 minutes after it started exits
with status 4 (a watchdog thread: also when the scene hangs).
`python scripts/dev/check_appstore_texts.py` checks the listing texts in
docs/APP_STORE.md against Apple's length limits.

`OPENSHAPE_LOG=debug` adds per-operation timings (PERFORMANCE category:
tessellation, recompute, kernel operations) to the log; those computed on
the preview worker thread start with `[worker]`, and GUI-thread blocks
start with `gui:` (a pointer move of 16 ms or more, the longest move of
each drag, a wait for the kernel while the worker held it).
`OPENSHAPE_SYNC_PREVIEWS=1` computes previews on the GUI thread again, as
before 2026-09-26 (to compare).


**The Text tool's font.** Noto Sans Regular and Bold (`resources/fonts/`,
in the repository since 2026-09-26; see its README and `THIRD_PARTY.md` for
the release and hashes) are built into the executable at CMake's configure
time (after replacing the files, re-run the configure step; a checkout
without them builds with a CMake warning, and the tool says text is not
available). For development only, `OPENSHAPE_TEXT_FONT=<file.ttf>` makes a
build without them use another font in its place. The text tests
(`test_text*.cpp`, and the Text test in `test_async_preview.cpp`) read the
bundled Noto Sans from the source tree (`tests/TestFonts.h`; they fail when
it cannot be read); `OPENSHAPE_TEST_FONT=<file.ttf>` tries another typeface
for the regular font (the checks are areas, volumes and extents, which hold
for any outline font).

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
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\windows\test-installer.ps1 -Setup build\installer-test\OpenShape-0.2.0-windows-x64-setup.exe -InstallDir build\installer-test\Programs\OpenShape -PackageDir dist\OpenShape -TestDesktopDir build\installer-test\desktop -Screenshot build\installer-test\installed.png -TestRunningApp
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
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\windows\test-installer-dialogs.ps1 -Setup build\installer-test\OpenShape-0.2.0-windows-x64-setup.exe -InstallDir build\installer-test\Programs\OpenShape -TestDesktopDir build\installer-test\desktop
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
workflow artifact. (The setup file's name carries the version: adjust the
installer-test commands above after a version change.)

Before tagging a release:
1. Set the version in `CMakeLists.txt` and date the release's section in
   `CHANGELOG.md` (`## <version> (YYYY-MM-DD)`): the release notes are that
   section (`scripts/ci/changelog-section.sh`); `release.yml` refuses a tag
   whose section is missing, and a `v<version>` tag (not a `-rc` one) whose
   section still says "(unreleased)".
2. Do TD-55's documentation steps: retake the screenshots
   (`scripts/dev/doc_screenshots.sh`), walk through the README's Quick
   start, grep the guide for renamed buttons, and update the README's
   "Early pre-release" line.
3. If an iOS library or Qt version changed: its pin in
   `scripts/ios/sources.txt` and the Licenses view's texts are current
   (`python scripts/licenses/licenses.py check`, also run by ctest as
   `licenses_check`; see "The iOS app's licenses" below).
4. Push `main`, wait for CI (the iPad workflow's last run on `main` should
   say `License gate: PASS`), and run Release once by hand (GitHub →
   Actions → Release → Run workflow; a version bump alone does not start
   it). Push the tag `v<version>` only when both are green.
5. The tag also builds the iOS app for the App Store (`ipad.yml`, docs/
   LICENSING.md "Releasing an App Store (or public beta) version"): submit
   only the build that run uploads (its notice names the build number)
   for App Store review, and only once the tag's GitHub release lists
   `OpenShape-<version>-ios-sources.tar` (the file its Licenses view
   names). If the Release run failed, re-run it or run Actions → Attach
   sources → Run workflow for the tag (it makes a pre-release with the
   source files). Builds of `main` go to TestFlight testers (the public
   beta) but not to review.
6. Right after pushing the tag, raise the version in `CMakeLists.txt` on
   `main` and add a `## <next version> (unreleased)` section to
   `CHANGELOG.md`. App Store Connect takes no more builds of a version that
   is in review or released, so otherwise every TestFlight upload from
   `main` fails (docs/IPAD.md, "Reading CI results").

Every Release run also downloads the source code of the LGPL libraries the
package ships (`scripts/ci/mirror-sources.sh`: the MSYS2 source archives
`THIRD_PARTY_LICENSES.txt` names, and OpenCASCADE's source with the
patches `build-occt.sh` applies); a release attaches it (TD-46). It also
downloads the source of every library in the iOS app
(`scripts/ios/mirror-sources.sh <dir> [tag]`: the archives pinned in
`scripts/ios/sources.txt`, SHA-256 checked, with `SOURCES-SHA256SUMS.txt`
and a README), early in the run; a release attaches it as one file,
`OpenShape-<tag without v>-ios-sources.tar`, which the app's Licenses view
names. By hand (Git Bash with curl works; 9 archives, 151 MB, verified
2026-09-27):

```bash
bash scripts/ios/mirror-sources.sh build/ios-sources v0.3.0
```

The Windows icon is made from the SVG with
`python scripts/windows/make-icon.py` (needs MSYS2's `rsvg-convert`,
`pacman -S mingw-w64-ucrt-x86_64-librsvg`).

**Code signing** happens only in `release.yml`, through SignPath Foundation
(docs/CODE_SIGNING.md: policy, and the owner's setup steps). It switches on
when the repository has the secret `SIGNPATH_API_TOKEN` and the variable
`SIGNPATH_ORGANIZATION_ID` (optional: `SIGNPATH_PROJECT_SLUG`, default
`openshape`); without them the workflow runs exactly as above, unsigned, and
says so in a notice. With them, `OpenShape.exe` is signed between steps 3
and 4 (so the installer and the zip contain it signed) and the installer
after step 4; tags use SignPath's `release-signing` policy (OpenCASCADE is
then built in the run instead of restored from the cache; the owner approves
each request, the workflow waits up to an hour each), other runs
`test-signing`. `scripts/windows/use-signed.sh` accepts a returned file only
if it is the sent file plus a signature (`scripts/windows/pe-signature.py`)
and Windows accepts the signature, then updates `SHA256SUMS.txt`; the
installer test runs on the signed installer.

Every Release run (signed or not) also checks the release notes template in
both variants, before the build, and runs the signature check's self-test
on the packaged `OpenShape.exe`; on Windows, ctest runs both too
(`release_notes_template`, needs bash; `release_pe_signature_selftest`,
needs Python 3 and the app), so a broken template or check fails on a push,
not an hour into a tag's release. By hand:

```bash
bash scripts/ci/test-install-notes.sh                                           # the template, both variants (25 checks)
python scripts/windows/pe-signature.py self-test dist/OpenShape/OpenShape.exe  # 14 checks on made-up signatures
python scripts/windows/pe-signature.py info <signed.exe>                        # where its signature is
bash scripts/ci/install-notes.sh 0.2.0 0.2.0 v0.2.0 signed                       # release notes, signed variant
```

### 7. Developer tools

- **Benchmark** — times a push/pull drag preview, tessellation, recompute,
  bounding boxes and recovery copies / full saves on a 21-face filleted part
  (and saves and the project thumbnail on a 21-body, 1528-face model),
  then the same plus a fillet drag and hover picking (1200 pointer
  positions over the part) on a 249-face enclosure (shelled, rounded, 95
  vent holes with chamfers, screw bosses; 25k triangles). Last, the GUI
  thread during a 20-step push/pull drag on the enclosure's rim (pointer
  moves 16 ms apart: per move the controller plus what the UI reads back,
  and delivering finished previews between moves), once with previews on
  the GUI thread and once on the preview worker, and the same for the Hole
  tool on the enclosure's front wall (two clicks, each followed by 15 hover
  moves while its preview computes). Numbers in PROJECT_STATUS.md:

  ```bash
  cmake --preset msys2-ucrt64 -DOPENSHAPE_BUILD_TOOLS=ON
  cmake --build build/msys2-ucrt64 --target bench_session
  ./build/msys2-ucrt64/bin/bench_session.exe
  ```

- **Longer stress hunts** — the robustness suite runs a few fixed seeds;
  `OPENSHAPE_STRESS_SEEDS=N` runs N other seeds of each random session
  instead (undo/redo, interleaved, save/open; ~3 s each), and as many random
  UI sessions with previews on the worker against synchronous ones
  (`test_interaction.exe`, `AsyncPreview.RandomSessionsMatchSynchronousOnes`). A seed replays the
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
  (run it in the background). `GUI` lines are what froze the window
  (pointer moves, drags, waits for the kernel), `SLOW` kernel work on the
  GUI thread (commits, undo), `WORKER` slow previews on the worker thread
  (the window stays responsive meanwhile).

- **`scripts/dev/doc_screenshots.sh`** — retakes the screenshots in
  `docs/images/` (README.md, docs/USER_GUIDE.md) from the demo scenes
  (1400x900; `tablet.png` with `--touch --size 1180x820`) and passes each
  through `scripts/dev/shrink_png.py`, which drops the opaque alpha channel
  and recompresses (13-22 % smaller, the same pixels; standard-library
  Python). Run it from the repository root with the toolchain on `PATH`
  after a UI change, keep the mouse pointer away from where the window
  opens (a tooltip under it would be in the picture), and look at every
  image; `bash scripts/dev/doc_screenshots.sh hero tablet` retakes only
  those.

## macOS and iPad — built on GitHub's Macs

A `macos` preset (Homebrew packages) exists; CI builds it with Apple Clang
and runs the headless tests and the real-UI acceptance run on a GitHub Mac
(`.github/workflows/ci.yml`). The universal iPhone/iPad app is built on
GitHub's Macs and delivered through TestFlight (`.github/workflows/ipad.yml`,
[docs/IPAD.md](docs/IPAD.md)). A build on a local Mac has not been tried.

### The iOS app's licenses

The iOS app links Qt, OpenCASCADE, PlaneGCS, FreeType and libzip
statically, so it shows every license text itself (About → Licenses;
docs/LICENSING.md, "The iOS app and the App Store"). The pieces:

- `scripts/ios/sources.txt`: the exact source archive of each library
  (name, version, SHA-256, URL). `scripts/ios/fetch-source.sh` downloads
  one and checks it; `build-deps.sh` builds only those.
- `resources/licenses/` (built into the app on every platform): generated
  by `scripts/licenses/licenses.py` from those archives,
  `scripts/licenses/components.json` and Qt's `qt_attribution.json` files;
  `source-offer.txt` there is the hand-written LGPL notice and written
  offer (placeholders filled in by the app).
- The checks: `python scripts/licenses/licenses.py check` (ctest
  `licenses_check`, 69 checks; also the first step of `ipad.yml`),
  `python scripts/licenses/licenses.py self-test` (ctest
  `licenses_selftest`, 20 checks) and `bash scripts/ios/test-sources.sh`
  (ctest `ios_sources_scripts`, 16 checks, no downloads; it also covers
  `scripts/ios/released-pins.sh`, which `ipad.yml` uses to warn while no
  release tag has the current pins).

After changing a version in `sources.txt` (or `components.json`, or one of
the repository's license files the app shows: `LICENSE`,
`third_party/planegcs/COPYING.LIB`, `resources/fonts/OFL.txt`), regenerate
and commit the texts (verified 2026-09-27 on Windows, Git Bash: 50
entries, 51 texts, 277 KiB; the generation takes about 45 s):

```bash
bash scripts/ios/mirror-sources.sh build/ios-sources      # the pinned archives (151 MB)
python scripts/licenses/licenses.py generate --archives build/ios-sources
python scripts/licenses/licenses.py check
```

A Qt update whose sources contain a new third-party part makes `generate`
fail until `components.json` includes or excludes it (with the reason). Re-run
CMake's configure step when texts were added or removed.

Every iOS build ends with the **license gate** (`scripts/ios/build-app.sh`,
`licenses.py gate`): the linker writes a map of every file linked into the
app (`build/ios/OpenShape-LinkMap.txt`), and each must come from Qt (its
module's source pinned), the iOS libraries, the build itself or Apple's
SDK, and be in the Licenses view. Report: `build/ios/license-gate.txt`
(also a CI notice). It warns on ordinary builds and fails release tags
(`OPENSHAPE_LICENSE_GATE=required`).

### Rebuilding the iOS app with modified libraries

For anyone who got OpenShape for iPhone or iPad and wants to use it with a
modified Qt, Open CASCADE or PlaneGCS, as the LGPL allows (docs/LICENSING.md).
These are the scripts CI runs for every build (`.github/workflows/ipad.yml`);
the variations for modified libraries and your own signing (marked below)
have not been run on a Mac yet. You need a Mac with Xcode 26 or newer,
CMake, Ninja and Python 3 (`brew install cmake ninja`), and an Apple
Account (a free one installs on your own devices for 7 days at a time; the
Apple Developer Program for a year).

1. **The source of your version.** About → Licenses → "Your rights to the
   LGPL libraries" names the release tag (or commit) of your copy:

   ```bash
   git clone --branch v0.3.0 https://github.com/SamuelAirs/openshape
   cd openshape
   # the libraries' exact sources, from the same release page:
   curl -LO https://github.com/SamuelAirs/openshape/releases/download/v0.3.0/OpenShape-0.3.0-ios-sources.tar
   tar -xf OpenShape-0.3.0-ios-sources.tar     # -> ios-sources/
   export OPENSHAPE_SOURCES_DIR=$PWD/ios-sources   # build from these instead of downloading
   ```

   A TestFlight build between releases names a commit instead: `git clone
   https://github.com/SamuelAirs/openshape && git -C openshape checkout
   <commit>`, then `scripts/ios/mirror-sources.sh ios-sources` downloads
   and checks the archives that commit's `scripts/ios/sources.txt` pins
   (the release whose `sources.txt` is the same has copies of them).

2. **Open CASCADE, FreeType, libzip** (and the header-only json, Eigen):

   ```bash
   scripts/ios/build-deps.sh ~/openshape-ios-deps ~/openshape-ios-work
   ```

   Each library is unpacked once into `~/openshape-ios-work/<name>`
   (`occt`, `freetype`, `libzip`, `json`, `eigen`) and built into
   `~/openshape-ios-deps`. *To modify one* (not run yet): change its
   source in that folder and run the same command again; a folder that is
   already there is built as it is.

3. **Qt.** The Qt Company's build, as CI uses it:
   `scripts/ios/install-qt.sh 6.11.2 ~/Qt`. *A modified Qt* (not run
   yet): unpack `qtbase-6.11.2.tar.xz` (and `qtdeclarative`, `qtsvg`,
   `qtshadertools`) from `ios-sources/`, change it, build Qt for macOS
   (the host tools) and then Qt for iOS from it, as Qt documents
   (<https://doc.qt.io/qt-6/ios-building-from-source.html>:
   `configure -platform macx-ios-clang -release -qt-host-path <host Qt>`),
   and install both.

4. **PlaneGCS** is in the source: change `third_party/planegcs`.

5. **The app**, with your own bundle identifier (Apple ties OpenShape's to
   its developer's team; *not run yet*):

   ```bash
   OPENSHAPE_BUNDLE_ID=com.example.openshape \
       scripts/ios/build-app.sh ~/Qt/6.11.2/ios ~/openshape-ios-deps
   # a self-built Qt: QT_HOST_PATH=<your Qt for macOS> scripts/ios/build-app.sh <your Qt for iOS> ~/openshape-ios-deps
   open build/ios/OpenShape.xcodeproj
   ```

   In Xcode: the `openshape` scheme, your iPhone or iPad as the
   destination, Signing & Capabilities → your team → Run. On the device,
   the first time: Settings → Privacy & Security → Developer Mode.

## Other platforms — not yet verified

The CMake project uses only `find_package` for OCCT (≥ 7.8 recommended),
Qt ≥ 6.8 (Core, Gui, GuiPrivate, Qml, Quick, QuickControls2, ShaderTools),
nlohmann_json, libzip, Eigen3 and GTest, and needs a C++23 compiler for the
vendored solver, so Linux distribution packages and Homebrew
should work with the `linux` preset. This has **not** been run yet.
