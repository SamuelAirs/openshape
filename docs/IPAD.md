# Running OpenShape on an iPhone and iPad

**Status (2026-09-26):** the owner's MacBook is too old for current Xcode,
so the iOS app is built on GitHub's Macs (free for a public repository)
and delivered through **TestFlight**; no Mac is needed. It is one
**universal app** for iPhone and iPad (2026-09-26; it was iPad only
before). The owner's devices: iPad Air 11-inch (M2, iPadOS 26.6.1, 1180x820
points), iPhone 16 Pro (6.3", 402x874 points, Dynamic Island), and the
foldable iPhone Duo once it ships (2026-10-23, iOS 27: a 5.4" outer and a
7.6" inner display, Split View). (The file keeps its old name so links stay valid.)

| Step | State |
|---|---|
| Mac app (Xcode 26.3, Apple Clang 17, `-Werror`) | ✅ builds; 239/239 tests; Metal screenshot renders |
| Mac real-UI acceptance run (1024x653 window) | 🟡 127/144 checks (small-window failures being fixed) |
| iOS libraries (`build-deps.sh`) | ✅ 19 min on a 3-core runner, then cached |
| Qt 6.11.2 for iOS (`install-qt.sh`) | ✅ 2.4 min, then cached |
| iOS app archive (`build-app.sh`) | ✅ 50 MB, arm64, iPadOS 17+, icon, privacy manifest; the 30 QML modules it needs are linked (checked by the build). Universal (iPhone + iPad) since 2026-09-26: ⬜ not yet built on CI |
| Signing + TestFlight upload (`testflight.sh`) | ⬜ waits for the owner's Apple Developer enrollment and the secrets |
| Running on the iPad | ⬜ |
| Running on the iPhone | ⬜ |

What is ready:

- Touch and pen: gestures (one finger, two-finger pan/pinch, two-/three-finger
  tap = undo/redo), pen mode (pen selects and draws, fingers only navigate)
  with a Pen switch, touch-sized controls (44 pt) that are on from the start
  on a tablet, a tool palette that scrolls. Try it on Windows with
  `OpenShape.exe --touch`.
- Build settings for Apple platforms: warning flags Clang accepts, the
  PlaneGCS solver built as a static library on iOS, a universal app bundle
  (`TARGETED_DEVICE_FAMILY` 1,2 in `src/app/CMakeLists.txt`) with an
  `Info.plist` (`src/app/ios/Info.plist.in`: iPhone in portrait and both
  landscapes, iPad in all four orientations, no `UIRequiresFullScreen` so
  Split View, Stage Manager and a folding phone's split screen work,
  projects visible in the Files app, no export-compliance question), one
  App Store icon for both (`src/app/ios/Assets.xcassets`, rendered from
  `resources/icons/openshape.svg`; Xcode derives every size), a launch
  screen in the app's background color (`LaunchBackground` color set),
  archive-friendly install settings.
- A layout that follows the window, not the device: below about 600 points
  wide or 500 tall (an iPhone either way up, a Split View half, the Duo's
  outer display) the tools become a strip along the bottom, the Model panel
  slides in behind a **Model** button and the view buttons fold into one
  **View** menu. Try it on Windows with
  `OpenShape.exe --touch --size 402x874` (see BUILDING.md).
- CI (`.github/workflows/`):
  - `ci.yml`, job `macos`: the Mac app with Apple Clang and Homebrew
    packages, the headless tests, one Metal screenshot (summarized as
    numbers by `scripts/ci/png_stats.py`) and the real-UI acceptance run.
  - `ipad.yml`: the iOS libraries (`scripts/ios/build-deps.sh`; cached),
    Qt 6.11.2 for iOS (`scripts/ios/install-qt.sh`; cached), an unsigned
    archive of the app (`scripts/ios/build-app.sh`), then signing and the
    TestFlight upload (`scripts/ios/testflight.sh`).

## 1. One-time setup (the owner, about 10 minutes)

Needs the paid Apple Developer Program (the owner has it).

1. **Team ID:** developer.apple.com → Account → Membership details → copy
   the 10-character Team ID.
2. **App ID:** Certificates, IDs & Profiles → Identifiers → **+** → App IDs →
   App → Description `OpenShape`, Bundle ID **Explicit**
   `io.github.samuelairs.openshape` → Continue → Register. (If Apple refuses
   the identifier, choose another and change it in `src/app/CMakeLists.txt`.)
3. **App record:** appstoreconnect.apple.com → Apps → **+** → New App →
   iOS, name `OpenShape` (names are unique store-wide; if taken, e.g.
   `OpenShape CAD` — the home screen still says OpenShape), language,
   the bundle ID from step 2, SKU `openshape`, Full Access → Create.
4. **API key:** App Store Connect → Users and Access → Integrations → App
   Store Connect API (Request Access the first time) → Team Keys → **+** →
   name `GitHub`, access **Admin** (Xcode's cloud signing needs it to create
   the distribution certificate) → Generate. Download the `.p8` file (only
   possible once) and note the **Key ID** and the **Issuer ID** above the
   list. The key can be revoked there at any time.
5. **GitHub secrets:** github.com/SamuelAirs/openshape → Settings → Secrets
   and variables → Actions → New repository secret, four times:
   `APPLE_TEAM_ID`, `APP_STORE_CONNECT_KEY_ID`,
   `APP_STORE_CONNECT_ISSUER_ID`, `APP_STORE_CONNECT_KEY` (the whole text
   of the `.p8` file, including the BEGIN and END lines). Secrets are not
   visible to anyone, and pull requests from forks cannot read them.
6. **iPhone and iPad:** install **TestFlight** from the App Store on each
   and sign in with the developer account's Apple ID (one TestFlight build
   installs on both).
7. **After the first upload:** App Store Connect → OpenShape → TestFlight →
   Internal Testing → **+** → a group (e.g. `Me`) with automatic
   distribution → add yourself. Later builds then arrive by themselves.

## 2. Getting a new build onto the iPhone or iPad

Every push to `main` that changes code runs `ipad.yml` (so does a commit
whose message contains `[testflight]` on another branch); to run it by
hand, e.g. right after adding the secrets: GitHub → Actions → **iPad** →
**Run workflow** (branch `main`). With the secrets set, it uploads build
number = the workflow's run number. Apple processes
it (usually 5–30 minutes; an e-mail says when), then TestFlight on the
iPhone and iPad offers **Install** / **Update**. TestFlight builds expire after 90
days. The first run builds OpenCASCADE for iOS (about half an hour); later
runs take its cached copy.

## 3. Reading CI results (anyone, no sign-in)

GitHub shows job logs only to signed-in users, but for a public
repository the run and job status and the annotations are public. Failed
steps put their error lines and last lines into annotations
(`scripts/ci/run-logged.sh`); successful ones add notices (test counts,
screenshot numbers, app size, minimum iPadOS).

`scripts/dev/ci_status.py` prints all of it (`status`, `watch` in the
background until a run finishes, `report <run id>`); by hand:

```bash
curl -s "https://api.github.com/repos/SamuelAirs/openshape/actions/runs?head_sha=$(git rev-parse HEAD)"
curl -s https://api.github.com/repos/SamuelAirs/openshape/actions/runs/<run id>/jobs
curl -s https://api.github.com/repos/SamuelAirs/openshape/check-runs/<job id>/annotations
```

Anonymous API calls are limited to 60 per hour per IP address. A push to
`main` cancels the CI run still in progress (not `ipad.yml`, which queues):
wait for the macOS job (about 3 minutes) before pushing again.

## 4. Alternative: building on a Mac with Xcode

For a Mac with a current Xcode (26 or newer), e.g. to run the app on a
connected iPhone or iPad from Xcode or to debug it there. The commands are the ones
CI runs; see `.github/workflows/` for the exact sequence.

The desktop app (checks the code on Apple's compiler):

```bash
brew install ninja opencascade qtbase qtdeclarative qtshadertools qtsvg nlohmann-json libzip eigen googletest
cmake --preset macos
cmake --build build/macos
ctest --test-dir build/macos -LE gui
open build/macos/bin/OpenShape.app
```

The iOS app (iPhone and iPad):

```bash
scripts/ios/build-deps.sh ~/openshape-ios-deps       # static arm64 libraries, ~30+ min
scripts/ios/install-qt.sh 6.11.2 ~/Qt                # or the Qt Online Installer (macOS + iOS)
scripts/ios/build-app.sh ~/Qt/6.11.2/ios ~/openshape-ios-deps
open build/ios/OpenShape.xcodeproj
```

In Xcode: the `openshape` scheme, your iPhone or iPad as the destination,
Signing & Capabilities → your team → **Run**. On the device, the first
time: Settings → Privacy & Security → **Developer Mode** (on, restart).

## Files on iPhone and iPad

What Qt 6.11 offers on iOS (read from Qt's iOS platform plugin and
QtQuick.Dialogs): `FileDialog` in open mode shows the system document
picker (`UIDocumentPickerViewController`, filtered by the name filter's
extension); in save mode the native dialog refuses to show, and Qt falls
back to its own QML file browser of the app's sandbox — no use on a phone.
So on iOS (and Android) OpenShape saves without a dialog
(`AppController::savesToAppFolder`, set at start to the app's Documents
folder, which the Files app shows under On My iPhone / On My iPad →
OpenShape):

- **Save** of a new project, **Save As**, and "Save changes?" → Save ask for
  a **name** only (`SaveNameOverlay.qml`, near the top on a phone so the
  keyboard does not cover it); the project goes into OpenShape's folder as
  `<name>.openshape` (characters a file system refuses become "-"; the same
  name says **Replace**). A saved project saves in place.
- **Open** is the system document picker, starting in OpenShape's folder;
  `Info.plist` declares the `.openshape` type
  (`io.github.samuelairs.openshape.project`) so the picker shows projects.
- **Export STL / 3MF / STEP** write `Exports/<project name>.<ext>` in
  OpenShape's folder at once (replacing an earlier export of that name) and
  say where; from the Files app they can be shared to a slicer, AirDrop or
  Mail.
- Open Recent, recovery copies and the log (`Logs/openshape.log`) work as on
  the desktop.
- Windows and macOS are unchanged (file dialogs). `--app-folder <dir>`
  tries the iOS behaviour on the desktop; the acceptance scenario
  `appfolder` checks it.

Not done yet: the system share sheet straight from Export (needs a few
lines of Objective-C: `UIActivityViewController` with the file URL); opening
a project from the Files app into OpenShape (needs `CFBundleDocumentTypes`
and handling `QFileOpenEvent`); projects outside OpenShape's folder (iCloud
Drive) may not open if Qt does not start security-scoped access for the
picked file — copy them into OpenShape's folder in the Files app first.

## Expected rough edges

- Files: see above; the Open picker for projects outside OpenShape's folder
  needs testing on a device.
- No hover highlight (touch has no hover; Apple Pencil hover is not used yet).
- Typing values: tap the value field for the on-screen keyboard (typing
  without tapping needs a hardware keyboard).
- Not measured yet: speed and memory with bigger parts.

The app's log is in the Files app: On My iPhone / On My iPad → OpenShape → Logs →
`openshape.log` (share it with the Share button when reporting a problem).
TestFlight also collects crash reports and screenshot feedback in App Store
Connect.

## Licenses

For your own iPhone and iPad there is nothing to do. For the App Store see
docs/LICENSING.md: MPL-2.0 allows it; the LGPL parts (Qt, OCCT, PlaneGCS)
are linked statically on iOS, so their object files must be offered for
relinking.

## What to test on the iPad

1. Launch: the model view, toolbars and the axis marker appear; controls are
   comfortably tappable.
2. One finger: tap a face (selects), tap empty space (clears), drag empty
   space (orbits), double-tap a body (selects it).
3. Two fingers: pan and pinch-zoom; a quick two-finger tap undoes, three
   fingers redo.
4. Box → tap the top face → drag the arrow; tap the value field and type a
   height → Enter.
5. Sketch: draw a rectangle with a finger, then with the Pencil; turn on
   **Pen** and check that a resting hand does not draw or select.
6. Extrude a sketch profile; fillet an edge; save; close; reopen from the
   Files app.
7. Rotate the iPad: the layout follows; the tool palette scrolls when short
   (in a sketch too: Draw / Edit on the left).
   In a sketch: tap a constraint glyph (H, V, =, …) and delete it; tap right
   next to a line and check the line is selected, not the glyph; draw a
   polygon and change its sides with the on-screen − / + buttons.
8. Work is kept: add a box without saving, go to the Home Screen and swipe
   OpenShape away in the App Switcher. Start it again: it offers to restore
   the box.
9. Split View / Stage Manager: put OpenShape beside another app and drag
   the divider: at about half the width the compact layout appears (tools
   along the bottom, **Model** and **View** buttons), and back again.
10. Note anything slow, hard to hit, or missing — with a screenshot
   (TestFlight: take a screenshot and share it as feedback, or send it).

## What to test on the iPhone

The same app; the compact layout is on from the start. On Windows the
iPhone layouts can be previewed with `--touch --size 402x874` (portrait) or
`--size 874x402` (landscape), plus `--safe-area` for the Dynamic Island's
margins (BUILDING.md).

1. Launch in portrait: nothing sits under the Dynamic Island, the status
   bar (time, battery) is readable, the bottom tool strip stays above the
   home indicator; the model fills the whole screen behind the controls.
2. The tool strip along the bottom scrolls sideways (Box, Sketch, Push/Pull,
   Fillet, … Union, Subtract, Intersect); every button is easy to hit.
3. **Model** (top right) slides the Model panel in from the right; tap a
   step, change a value, close it with **Close**.
4. **View** (beside the X/Y/Z marker) opens Fit, Iso, Top, Front, Right,
   the projection and the unit; each works and closes the menu.
5. Box → tap the top face → drag the arrow, or tap the value field: the
   on-screen keyboard must not hide the field; ✓ applies.
6. Select two bodies (double-tap one, double-tap the other): the actions
   (Union, Subtract, …) scroll sideways above the tool strip.
7. Sketch: the Draw / Edit tools are in the bottom strip; draw a rectangle
   and a circle, tap a dimension to change it, **Finish sketch**, extrude.
8. Turn the phone to landscape and back, in the middle of an operation: the
   layout follows at once, nothing overlaps (in landscape the Dynamic Island
   is on the left or right: no button sits under it).
9. The hint line under the model is one short line: tap it to read all of it.
10. **?** opens the help card: it talks about taps and two-finger gestures
   (no mouse or keyboard terms); File → Preferences and About fit the
   screen and scroll.
11. File → Save: the first time it asks for a name and saves into OpenShape's
   folder (Files app → On My iPhone → OpenShape); File → Open shows the
   system file picker; Export STL/3MF/STEP writes into Exports there.
12. iPhone Duo (once available): fold and unfold with a model open, and use
   the inner display's Split View: the layout changes live, no restart.
