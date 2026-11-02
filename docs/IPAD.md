# Running OpenShape on an iPhone and iPad

**Status (2026-09-27):** the owner's MacBook is too old for current Xcode,
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
| Mac real-UI acceptance run (1024x653 window) | ✅ all checks since CI run #29 (TD-35 resolved); the step stays allowed to fail until the new `compact` and `appfolder` scenarios have passed there |
| iOS libraries (`build-deps.sh`) | ✅ 19 min on a 3-core runner, then cached |
| Qt 6.11.2 for iOS (`install-qt.sh`) | ✅ 2.4 min, then cached |
| iOS app archive (`build-app.sh`) | ✅ 50 MB, arm64, iPadOS 17+, icon, privacy manifest; the 30 QML modules it needs are linked (checked by the build). Universal (iPhone + iPad) since 2026-09-26: ✅ built on CI |
| iPhone Duo build (iOS 27.1 SDK) | ✅ 2026-10-30: Xcode 27.1 (27A9275), iOS SDK 27.1 on the `xcode-27` runner (ipad.yml run 41160904873 on branch duo-ci: 57 MB, iOS 17+, iPhone + iPad); `UIApplicationSceneManifest` declared; **to try on the owner's Duo** (section "The iPhone Duo") |
| Signing + TestFlight upload (`testflight.sh`) | ✅ on every push to `main` (build number = the workflow run number); since 2026-09-27 builds may also go to external testers (`testFlightInternalTestingOnly` false: the public beta, section 1b). Only builds of release tags `v<version>...` are submitted for App Store review (they name the tag in About → Licenses and require the license gate; docs/LICENSING.md) |
| Debug symbols (dSYM) | ✅ 2026-09-27 (ipad.yml run 39): Release is built with debug information (same optimization); the archive carries `OpenShape.app.dSYM` (62 MB; the app stays 55 MB). Xcode's archive step leaves the dSYM of a CMake project out (run 37), so `build-app.sh` copies the build's in and fails unless its UUID is the app binary's and OpenShape's functions are in it; the upload sends it (`uploadSymbols`), so TestFlight crash reports symbolicate |
| Share sheet after exports, File → Share Project… | 2026-09-27: built on CI (`src/ui/ios/ShareSheet.mm`, run 39); the `share` acceptance scenario checks it with a stub sheet on Windows and the Mac (CI run 47); **to try on the devices** |
| Open in FoldForm (Files app, Mail) | 2026-09-27: document types for projects and STEP files (checked by the build and `test_uistate`), `QFileOpenEvent` handling; the `openin` scenario checks it on Windows and the Mac through Qt's own entry point; **to try on the devices** |
| Home: Rename, Duplicate, Delete on a project card | 2026-09-27: Delete moves the project to the Files app's Recently Deleted (Qt's `QFile::moveToTrash` → `trashItemAtURL` → `Documents/.Trash`; asks again and deletes for good only if that fails); the `homeprojects` scenario checks all three with a stand-in trash on Windows and the Mac; **to try on the devices** (iPhone item 18 below, also on the iPad) |
| Running on the iPad | ✅ the owner's iPad Air (TestFlight, 2026-09-26); on-screen keyboard docking still to try |
| Apple Pencil: palm rejection, hover, the finger-tap hint (2026-10-30) | built and checked on Windows (`test_pencil`, the `pencil` scenario injecting Qt tablet events the way the iOS plugin delivers them); **to try on the iPad** ("Apple Pencil" below), and on the iPhone Duo once Apple enables Pencil there |
| Running on the iPhone | ✅ the owner's iPhone 16 Pro (TestFlight, 2026-09-26) |
| Licenses for the App Store (2026-09-27) | ✅ checked (docs/LICENSING.md); the app shows every license (About → Licenses); release tags build the App Store upload with the license gate required; the release carries the iOS sources. 🟡 on CI: the license gate's first real run (TD-103); the owner: the custom EULA and the source-offer contact (TD-102) |
| App Store: FoldForm 0.3.0, free with the in-app purchase Unlimited Projects (2026-10-31) | ✅ build 54 from the tag `v0.3.0` uploaded (ipad.yml run 41224892140, license gate PASS 15/15); the release v0.3.0 carries the iOS sources; texts, screenshots and the purchase's review screenshot in the repository ([APP_STORE.md](APP_STORE.md)); privacy and support pages live. 🟡 the owner's App Store Connect steps, in order: [LAUNCH_CHECKLIST.md](LAUNCH_CHECKLIST.md) |

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
  **View** menu. It changes live when the window changes (rotation, Split
  View, folding). Try it on Windows with
  `OpenShape.exe --touch --size 402x874` (see BUILDING.md).
- Safe areas: panels and buttons stay clear of the Dynamic Island or notch,
  the rounded corners and the home indicator (Qt's `SafeArea`, in portrait
  and landscape); the model fills the whole screen behind them. Preview with
  `--safe-area 62,0,34,0` (iPhone 16 Pro portrait) or `0,62,21,62`
  (landscape).
- Touch wording: hints, messages and the help card speak of taps, two
  fingers and the on-screen ✓ / ✕ (never Shift-click, Esc, Enter, the scroll
  wheel or hovering); tooltips are off on touch (the help card has what
  they said).
- Files without dialogs where iOS has none: see "Files on iPhone and iPad"
  below.
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
   Before the App Store release, see [APP_STORE.md](APP_STORE.md) section
   0: a free app called OpenShape3D does the same thing.
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

## 1b. The public beta (external testers; the owner, once)

Decided 2026-09-27: a public TestFlight beta before the paid App Store
release. Builds uploaded since then may go to external testers
(`scripts/ios/testflight.sh` sets `testFlightInternalTestingOnly` to false;
builds uploaded before stay internal). The steps in App Store Connect (Test
Information with the Privacy Policy URL, Beta App Review information, the
`Public beta` group, adding a build, the public link) and the texts to paste
are in one place: [APP_STORE.md, section 5](APP_STORE.md#5-the-public-beta-testflight).
In short (enough to do it without that file):

1. App Store Connect → OpenShape → **TestFlight** → **Test Information**:
   a *Beta App Description* (what OpenShape is, what to try), a *Feedback
   Email* (shown to testers), *Marketing URL*
   `https://github.com/SamuelAirs/openshape`, *Privacy Policy URL* (the
   public web address of OpenShape's privacy policy, the same one the App
   Store listing uses: OpenShape collects no data). **Beta App Review
   Information**: your name, phone (international format) and email;
   **Sign-in required: unchecked** (OpenShape has no accounts); *Review
   Notes*: that it needs no account and no network.
2. The **+** next to *External Testing* → group `Public beta`.
3. In `Public beta` → **Add Builds** → a build uploaded from `main` since
   2026-09-27 → *What to Test* (e.g. model a part, then Export STL and pick
   the slicer or AirDrop in the share sheet) → **Submit Review**.
4. After Beta App Review (an email; plan 1 to 2 days): `Public beta` →
   **Testers** → **Create Public Link** → share the link.

Testers' crash reports and screenshot feedback appear under TestFlight →
Feedback (crashes symbolicated with the uploaded dSYM). The App Store
listing itself (description, screenshots, price, age rating, privacy
answers) is in the same checklist, [APP_STORE.md](APP_STORE.md).

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

If the TestFlight upload fails on every push after an App Store release
("train closed", "must contain a higher version"), main still has the
released version: raise it in `CMakeLists.txt` (BUILDING.md, "Before
tagging a release", step 6); `ipad.yml` puts this into an error
annotation when it recognises the message.

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
  A project picked outside OpenShape's folder (iCloud Drive, On My iPad) is
  copied in and the copy opened (below).
- **Export STL / 3MF / STEP** write `Exports/<project name>.<ext>` in
  OpenShape's folder at once (replacing an earlier export of that name)
  and then open the **share sheet** with the file
  (`UIActivityViewController`: a slicer app, AirDrop, Save to Files, Mail;
  on an iPad a popover at the File button). **File → Share Project…** saves
  (a new project asks for its name) and shares the `.openshape` file.
  Sent or closed is only logged; a failure is said in a message (without
  a share sheet, as in `--app-folder` on the desktop, a message says where
  the file is). The sheet
  is Objective-C++ (`src/ui/ios/ShareSheet.mm`), behind a handler the
  acceptance run replaces with a stub (`AppController::setShareHandler`).
- **Open in OpenShape:** `Info.plist` lists projects (rank Owner) and STEP
  files (rank Alternate; an imported type `org.iso.step` for `.step` /
  `.stp`, as Apple declares none) in `CFBundleDocumentTypes`. How Qt 6.11.2
  delivers them (read from its iOS plugin, `qiosapplicationdelegate.mm`):
  its scene delegate's `scene:openURLContexts:` (and the URL contexts of a
  cold start) passes each file URL through
  `qt_apple_urlFromPossiblySecurityScopedURL` — which starts security-scoped
  access, keeps a bookmark and stops again; Qt's security-scoped file engine
  then starts and stops access around each `QFile` use — and on to
  `QWindowSystemInterface::handleFileOpenEvent`, a `QFileOpenEvent` to the
  application object (not `QDesktopServices`, which gets only non-file
  URLs). `AppController` filters that event into `openIncomingFile()`.
  Since OpenCASCADE and the project reader open files themselves, the file
  is first brought inside through `QFile` (`ui/IncomingFiles`): a project
  into OpenShape's folder (an identical copy there is reused, a different
  one of the same name becomes "Name 2"; the system's copy in
  `Documents/Inbox`, as Mail leaves attachments, is moved out and the empty
  Inbox removed), a STEP file into a scratch copy that is removed after the
  import. Then it opens like Home's Open or Import STEP (a new project),
  after "Save changes?" when there are unsaved changes. A project already in
  OpenShape's folder (tapped in the Files app) opens in place.
- Open Recent, recovery copies and the log (`Logs/openshape.log`) work as on
  the desktop. iOS gives an updated app (every TestFlight build) a new data
  folder, so remembered paths into OpenShape's folder go stale: Open Recent
  entries and a recovery copy's project follow the folder to its new place
  (`io::rebasedIntoFolder`). A project from outside OpenShape's folder is
  opened as its copy there, so Open Recent lists the copy.
- Windows and macOS are unchanged (file dialogs; no Share Project). On a Mac
  a file handed over by Finder opens where it is.

Try without an iPhone: `--app-folder <dir>` on the desktop; the acceptance
scenarios `appfolder` (saving by name, exports), `share` (a stub share
sheet) and `openin` (files handed over through `handleFileOpenEvent`, the
call Qt's iOS delegate makes; the Open and Import STEP pickers with files
from elsewhere) check it. Not verified on a device yet (TD-69): the share
sheet itself, which apps offer "Open in FoldForm" for STEP files (another
app's own STEP type may win, TD-68), and files in iCloud Drive that are not
downloaded yet.

## Expected rough edges

- Files: see above; sharing and "Open in FoldForm" need trying on the
  devices (the tests below).
- Hover highlights only for an Apple Pencil held over an iPad that supports
  hover (fingers have none); the iPhone Duo has no Pencil hover.
- Typing values: tap the value field for FoldForm's numeric keypad
  (typing without tapping needs a hardware keyboard).
- Apple Pencil Scribble (handwriting into a field) is not expected to work,
  but has not been tried on a device: Qt 6.11's iOS platform plugin
  (`qiosinputcontext.mm`, `qiostextresponder.mm`, `quiview.mm`, read on
  2026-09-27) has no `UIScribbleInteraction` /
  `UIIndirectScribbleInteraction` code, and a field only gets a text
  responder once it has the focus; forum threads (Esri Survey123,
  MerginMaps) report it not working and cite QTBUG-90932 (not read: the
  Qt bug tracker did not load). Making it work would mean patching Qt's
  iOS platform plugin. The value fields are read-only on
  touch anyway (the keypad types into them, so the system keyboard stays
  down); the keypad's keys take Pencil taps. See TD-90.
- Not measured yet: speed and memory with bigger parts.

The app's log is in the Files app: On My iPhone / On My iPad → FoldForm → Logs →
`openshape.log` (share it with the Share button when reporting a problem).
TestFlight also collects crash reports and screenshot feedback in App Store
Connect.

## The App Store

FoldForm 0.3.0 (build 54) goes to App Review directly, free with the
in-app purchase below; the owner's steps, in order, are
[LAUNCH_CHECKLIST.md](LAUNCH_CHECKLIST.md), and the details and texts to
paste [APP_STORE.md](APP_STORE.md) (agreements, tax and bank, the Small
Business Program, the DSA, app information, the "Data Not Collected"
privacy answers, age rating, listing texts, price, the submission). The privacy policy is [PRIVACY.md](PRIVACY.md) (also linked in
File → About), the support page [SUPPORT.md](SUPPORT.md). The screenshots
(iPhone 6.9" 1320 x 2868, iPad 13" 2752 x 2064) are made on Windows from
the real app by `bash scripts/dev/appstore_screenshots.sh` (BUILDING.md).

### The in-app purchase "Unlimited projects" (TestFlight)

FoldForm on iPhone and iPad is free for one project; the in-app purchase
"Unlimited projects" (non-consumable, product ID
`io.github.samuelairs.openshape.unlimited`: the bundle ID plus
`.unlimited`, `src/core/Version.h.in`) unlocks more. Before it works in a
TestFlight build:

1. App Store Connect → Business: the **Paid Apps Agreement** must be active
   (tax and bank details done); without it the App Store returns no product
   and the sheet says the App Store can't be reached.
2. App Store Connect → the app → Monetization → In-App Purchases → **+**:
   type *Non-Consumable*, reference name "Unlimited projects", product ID
   exactly `io.github.samuelairs.openshape.unlimited`; a price (about USD 10:
   FoldForm shows whatever the store returns, in the buyer's currency);
   availability; a display name and description; **Review Information**:
   the screenshot `docs/appstore/iap-review.png` (made by
   `bash scripts/dev/appstore_screenshots.sh iap-review`) and a note such as
   "Home → New project with one project saved opens the purchase; Restore
   purchase is also in File → About".
3. Submit the in-app purchase **with** the app version for review (the
   first one must go with a new app version; its state shows "Ready to
   Submit" until then). "Show to all App Store users" (on by default)
   puts it on the product page; FoldForm accepts a purchase started there
   (`shouldAddStorePayment`).

TestFlight builds buy in Apple's **sandbox**: the same sheet, "[Environment:
Sandbox]", nothing is charged. A tester with several projects from earlier
builds keeps them all; only adding more asks.

## Licenses

Checked on 2026-09-27 for the paid App Store release (docs/LICENSING.md,
"The iOS app and the App Store"; not legal advice): selling is allowed; Qt,
OCCT and PlaneGCS are LGPL and linked statically, which is fine because the
app's complete source, build scripts and the libraries' exact sources are
public at every release tag, and the app says so and shows every license
text (About → **Licenses**). What that means in practice:

- **Only builds of release tags go to App Store review.** A tag
  `v<version>` (or `v<version>-beta1`) builds the app with the tag inside
  it, requires the license gate to pass and uploads the TestFlight build to
  submit (the run's notice names the build number). Builds of `main` go to
  internal and external testers (the public beta, 1b) and name their
  commit in About → Licenses; each run warns when their library pins are on
  no release yet, so a beta tag keeps those sources. The tag's GitHub
  release carries `OpenShape-<version>-ios-sources.tar`.
- **Decided 2026-10-31:** Apple's standard EULA ([EULA.md](EULA.md) is
  not used), and the written source offer will give ayers177@gmail.com
  from 0.3.1 (TD-102; build 54 names the GitHub issue tracker). The license sentence
  is in the App Store description since 2026-10-31 (docs/LICENSING.md,
  "Releasing an App Store (or public beta) version").
- Every build's notices on CI list what the app links, by origin, and the
  license gate's result (`scripts/ios/build-app.sh`).

## Apple Pencil

The owner's Pencil (USB-C) has tilt only: no pressure, no double-tap, no
squeeze. Hover works on the iPad Air (M2, iPadOS 26), not on the iPhone
Duo (Apple says Pencil support for it comes later in 2026). Nothing in
OpenShape depends on pressure, hover or Pencil gestures; hover is a bonus.

How it works: Qt 6.11's iOS plugin delivers the Pencil as tablet events
(its hover through `UIHoverGestureRecognizer` as tablet moves without
buttons) and turns those no item takes into mouse events; `ViewportItem`
watches the window's tablet events for the Pencil's contact and hover.
**Palm rejection** (`interaction/PalmRejection`): a finger touch that
begins while the Pencil touches the screen, or within 0.5 s after it
lifts, is ignored until it lifts; touches already down when the Pencil
lands are ended (a one-finger press is cancelled, a pan stops). The
controller also never lets a finger's press disturb the Pencil's stroke.

Not yet seen on a device: whether iPadOS cancels palm touches itself
(OpenShape then ends only the fingers' press, never the Pencil's
stroke); whether the hover highlight goes away when the Pencil moves out
of hover range (Qt may send no event then: TD-143); whether 0.5 s feels
right.

**On the iPad:**

1. Hover: hold the Pencil tip about 1 cm above a box's face: the face
   lights up; over an edge, the edge. Start a sketch (Rectangle): a dot
   follows the tip and jumps to the face's corners and edge middles.
2. Tap a face with the Pencil: "Pen detected" shows and **Pen** is on.
3. Rest the side of your hand on the screen, then draw a rectangle with
   the Pencil, sliding the hand while you draw: the rectangle is drawn, the
   view does not move, nothing else is selected or drawn. No magnifier
   (loupe) appears beside the Pencil.
4. Lift the Pencil and put your hand down and up right away, also with two
   fingers' worth of hand: nothing happens (no undo).
5. Wait a moment, then drag one finger: the view turns; two fingers pan and
   zoom; a quick two-finger tap undoes.
6. Tap a face with a finger: nothing is selected and a message says how to
   select with a finger (it does not repeat on every tap). Turn **Pen**
   off (the view buttons): a finger tap selects again.
7. Tell us if a stroke ever broke off, the view jumped while drawing, or a
   finger was ignored when you meant to navigate (and how soon after
   drawing).

**On the iPhone Duo** (once Apple enables the Pencil there): steps 2 to 7;
there is no hover, so nothing lights up before the tip touches.

## What to test on the iPad

1. Launch: the model view, toolbars and the view cube appear; controls are
   comfortably tappable. Tap the cube's faces and corners (the view turns
   there) and drag it with one finger (orbits, also with a sketch tool armed).
2. One finger: tap a face (selects), tap empty space (clears), drag empty
   space (orbits), double-tap a body (selects it).
3. Two fingers: pan and pinch-zoom; a quick two-finger tap undoes, three
   fingers redo.
4. Box → tap the top face → drag the arrow; tap the value field: the
   numeric keypad opens beside the value box (never the system keyboard),
   away from the face and its arrow; tap `1`, `0`, `0` quickly: the model
   does not jump to 1 mm and 10 mm; ✓ makes it 100 mm. Again with the
   Pencil, and with `2`, `5`, `mm`, then `in` (it becomes 25 in), ⌫ (the
   whole unit goes), `×` `2`, pause: the preview follows after a moment.
   With a keyboard case: tap the value and type on the keyboard (the
   keypad stays open, the keys still type).
5. Sketch: draw a rectangle with a finger, then with the Pencil (the
   Pencil steps are in "Apple Pencil" above).
6. Extrude a sketch profile; fillet an edge; save; close; reopen from the
   Files app.
7. Rotate the iPad: the layout follows; the tool palette scrolls when short
   (in a sketch too: Draw / Edit on the left).
   In a sketch: tap a constraint glyph (H, V, =, …) and delete it; tap right
   next to a line and check the line is selected, not the glyph; draw a
   polygon and change its sides with the on-screen − / + buttons.
8. Work is kept: add a box without saving, go to the Home Screen and swipe
   FoldForm away in the App Switcher. Start it again: it offers to restore
   the box.
9. Split View / Stage Manager: put FoldForm beside another app and drag
   the divider: at about half the width the compact layout appears (tools
   along the bottom, **Model** and **View** buttons), and back again. In a
   window (iPadOS 26 windowed apps), check that the window's controls at
   the top left do not cover **File** (they may sit outside the safe area
   Qt reports; TD-54).
10. Text: tap a flat face, **Text**: the on-screen keyboard should come up
   for the words (they appear on the face as you type); tap the face to
   move them, drag the arrow up or down, **Deboss**, **Bold**, ✓.
11. Axes and planes: Construct → Plane, tap a top face, type `10`, ✓; tap
   the dashed axis or the plane's outline with a finger (easy to hit?);
   with Pattern → Circular waiting, tap an axis made through a hole; tap
   empty space while the Axis tool waits for its first pick (it gives up).
12. The value box stays out of the way: tap edges and faces on the left,
   the right, high and low on the model (and zoomed in): the box sits next
   to the arrow but never over what you tapped, the arrow or the selected
   edge / face; drag the arrow and it moves along without jumping. Move a
   body with its X arrow, then its Y arrow (zoomed in), and turn a long
   part a quarter turn: the box stays off the body where it went. Tap the
   value and type with the on-screen keyboard (no hardware keyboard): the
   box moves above the keyboard and stays readable. With the Pencil, rest
   your palm on the screen while the box shows: it does not move. Draw a
   rectangle and a circle with a finger: the live width / height /
   diameter show above the finger, not under it.
13. Two or more bodies (your report of 2026-09-27): add two boxes apart
   from each other, double-tap one, then double-tap the other, at your
   normal speed and a little sloppily (the second tap on another face):
   both stay selected and Union / Subtract / Intersect appear. Double-tap
   one of them again: only it leaves the selection. With a body selected,
   a single tap on another body adds it too. Also with the Pencil.
14. The keypad elsewhere: in a sketch, tap the first corner of a rectangle,
   then its live width: the keypad shows "Width" and what you type; `40`, **Next**,
   `25`, ✓ draws a 40 × 25 rectangle. Tap its width label: the keypad
   again; `50` ✓. Tap it once more and ✓ without typing: it stays exactly
   50 (also with inches shown). Tap it, type `6`, `0` and tap an empty
   spot in the view: 60, and no rectangle starts where you tapped. On a
   value, **×** **2** ✓ doubles it. In the Model panel, tap the Box step, then Height:
   `30` ✓. The Hole tool: tap the diameter, **Next** goes to depth, X, Y.
   Try Apple Pencil handwriting in a value field (Scribble): it is not
   expected to work (TD-90); tell us if you miss it.
15. Share to a slicer: File → Export STL: the share sheet opens as a
   popover pointing at **File**; pick the slicer app (the file arrives
   there), then Export 3MF and **AirDrop** it to a Mac or iPhone; Export
   STEP and **Save to Files**; close the sheet once without choosing (no
   message; the file is in FoldForm → Exports). Then with the iPad in
   Split View at half width: the sheet still appears.
16. File → Share Project… on a new project: it asks for a name, saves, then
   the sheet offers AirDrop / Mail / Save to Files; send it to yourself by
   Mail.
17. Open in FoldForm: in the Files app, on a `.openshape` project in
   iCloud Drive: Share → FoldForm (or tap it): FoldForm opens it and says
   "a copy in FoldForm's folder"; the copy is in On My iPad → FoldForm.
   Tap a project in On My iPad → FoldForm: it opens directly (no copy).
   A `.step` file in the Files app: Share → FoldForm: a new project with its
   bodies. In Mail (the project mailed in 15, and a STEP attachment): touch
   and hold the attachment → FoldForm. With unsaved changes, each first
   asks "Save changes?". Afterwards On My iPad → FoldForm has no "Inbox"
   folder left.
18. Loft: sketch a rectangle on the ground, Construct → Plane → From XY,
   type `30`, ✓, **Sketch** on the plane, a circle over the rectangle,
   **Finish sketch**. Tap the rectangle, then the circle, then **Loft**:
   a transition piece appears at once. **Straight**, ✓; in the Model panel
   tap the plane and change its distance: the loft follows.
19. Note anything slow, hard to hit, or missing — with a screenshot
   (TestFlight: take a screenshot and share it as feedback, or send it).

## The iPhone Duo

**Built for it since 2026-10-30.** `ipad.yml` builds the app with Xcode
27.1 and the iOS 27.1 SDK on GitHub's `xcode-27` runner. An app built with
an older SDK (every TestFlight build before that date) runs on the inner
display at a smaller, phone-like size; built with 27.1 it fills it. The iOS
27 SDK also requires the UIScene life cycle, which `Info.plist` declares
(one scene; Qt's iOS plugin is scene based), and the iPhone lists every
orientation, because an app that cannot turn is scaled on the inner display.
The Apple Pencil (USB-C) works on the Duo once Apple enables it there
(later in 2026), without hover (see "Apple Pencil").

The same app on both displays; the window keeps everything it had (the tool,
values typed, the selection, an open Model panel, Home) when it moves
between them. Point sizes from Apple's spec page and screenshot sizes; the
safe areas are estimates until a device log says otherwise (TD-150). The
first Duo log (builds 54 and 55) gave none: Qt kept the window inside them
(867x635 of 951x669 inner, 382x644 of 466x678 outer, black bars at the
right and bottom); from build 57 the window covers the whole display and
its `window ...` lines carry the real safe areas.

| Screen | Window (points) | Layout |
|---|---|---|
| Outer display, upright | 466x678 (top ~59, bottom ~34) | the phone layout; a keypad of 44 pt keys, so the face stays in sight between the value box and the keypad |
| Outer display, sideways | 678x466 (sides ~59) | the phone layout held sideways (one-row value box, the keypad in a bottom corner) |
| Inner display (book) | 951x669, a side bar of ~84 on the right or the left | the iPad layout, inside the bar: the sketch's tool bar goes below the top bar when it does not fit beside the Model panel; the tool palette ends above the hint; when the value box finds no spot beside the arrow, the view moves the selection into the free room |
| Inner display, turned | 669x951 (the bar along the top) | the iPad layout in portrait |
| Split View half | ~430-475 x 669 | the phone layout (a narrower value field, so the value box docks beside Model / View) |

Folding, unfolding or a Split View change re-places the keypad and brings
the selection back into sight if the change left it off screen. Each
change of the window's size, pixel ratio, safe area or screen writes one
line to the log, e.g. `window 951x669 pt at 3x (2853x2007 px), safe area top
0 right 84 bottom 20 left 0, usable 867x649 pt, regular layout; screen ...`.
Try it on Windows with the `duo` scenario (BUILDING.md) or
`OpenShape.exe --touch --size 951x669 --safe-area 0,84,20,0`.

What to try:

1. Fold and unfold with a model open, on Home, and with the Model panel open
   (it stays open; on the inner display it is the regular panel).
2. Mid-sketch: Rectangle tool, tap the first corner, type its width (keypad
   or keyboard), fold or unfold, then finish it: the rectangle has the size
   typed and the Rectangle tool is still on.
3. Mid-edit: tap a top face, tap the value, type `4` `2` on the keypad, fold
   or unfold: the keypad comes back beside or under the value box, the face
   in sight, 42 still typed; ✓ applies it.
4. On the inner display: turn it (669x951 and back), and move the side bar
   to the other side if iOS does that: nothing sits under the bar.
5. Split View with another app, on either side of the bar; drag the divider.
6. Half fold the phone (tent or laptop pose) while typing a value: tell us
   whether the value box, the keypad or a card sits on the crease (TD-140).
7. Selecting in a sketch (since 2026-10-31; the owner could not find how):
   draw two lines with the Line tool, then tap one of them with a finger.
   It turns blue, the tool bar shows **Select**, its length shows: tap the
   length to type one. A drag from the line still draws. On the inner
   display Select and Trim are at the top of the tool list.
8. Then send **Logs/openshape.log** (Files app → On My iPhone → FoldForm →
   Logs → Share): its `window ...` lines tell the real sizes and insets.

Not done: placement aware of the fold (TD-140: keep the value box, the
keypad and dialogs off the crease when the phone is half folded; the iOS
27.1 SDK's hinge API is not used yet).

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
4. **View** (above the view cube) opens Fit, Iso, Top, Front, Right,
   the projection and the unit; each works and closes the menu.
5. Box → tap the top face → drag the arrow, or tap the value field: the
   numeric keypad comes up along the bottom (not the system keyboard), the
   value box moves to the top and the face stays in sight between them;
   `1`, `0`, `0` typed quickly previews only 100; ✓ applies.
6. Select two bodies (double-tap one, double-tap the other, or tap the
   other once): the actions (Union, Subtract, …) scroll sideways above the
   tool strip. Next to each other on the small screen the first body's
   Move arrows reach over the second: a tap on the second body beside an
   arrow still adds it. Double-tap a selected body to take it out.
7. Sketch: the Draw / Edit tools are in the bottom strip; draw a rectangle
   and a circle, tap a dimension to change it (the keypad; the dimension's
   field moves above it), **Finish sketch**, extrude. Tap a rectangle's
   first corner, then its live width (the keypad says "Width"): type `12`, **Next**, `8`, ✓.
8. Turn the phone to landscape and back, in the middle of an operation: the
   layout follows at once, nothing overlaps (in landscape the Dynamic Island
   is on the left or right: no button sits under it). In landscape, tap a
   value: the keypad is four rows high in a bottom corner, below File,
   Model and **Finish sketch**.
9. The hint line under the model is one short line: tap it to read all of it.
10. **?** opens the help card: it talks about taps and two-finger gestures
   (no mouse or keyboard terms); File → Preferences and About fit the
   screen and scroll. About → **Licenses**: the list scrolls; tap Qt, then
   Back; tap "Your rights to the LGPL libraries": it names this version and
   build; the text pages scroll and fit the screen. About's line "Privacy
   policy: FoldForm collects no data about you" opens the policy in Safari.
11. File → Save: the first time it asks for a name and saves into FoldForm's
   folder (Files app → On My iPhone → FoldForm); File → Open shows Home,
   which lists every project in that folder, also in folders made in the
   Files app (since 0.3.1: the system picker from File → Open closed the app
   on the iPhone Duo, TD-158); Home's **Open…** shows the system file picker
   (a project from iCloud Drive opens as a copy in FoldForm's folder);
   Export STL/3MF/STEP writes into Exports there and opens the share sheet.
   After the next TestFlight update, File → Open Recent still lists those
   projects and opens them. On Home, the project's card says *FoldForm
   (Files app)* under its date (before: a long /var/mobile/… path).
12. iPhone Duo: see "The iPhone Duo" below.
13. The value box never covers what you tapped (your screen recording of
   2026-09-26): tap an edge on the left of a body low on the screen, then
   faces in different places: the box docks below the top bar or above the
   hint, whichever is farther from the selection, and stays there while
   you drag the arrow.
   Tap the top face of a box, then the value field: the box moves below the
   top bar while the keyboard is up, and the field stays in sight as you
   type. Swipe its row of actions sideways and tap the last one.
   Draw a rectangle with a finger, also next to the Dynamic Island in
   landscape: its width and height show above the finger, whole.
   In landscape the box is one row, beside the top bar, when its New body /
   Join / Cut fit there; otherwise its actions go on a second row.
14. Share: File → Export STL: the share sheet slides up from the bottom;
   send it to the slicer app, AirDrop it to the iPad or a Mac; File → Share
   Project… (asks a name for a new project) → Mail. Open the mailed project
   and a mailed STEP file from Mail (touch and hold the attachment →
   FoldForm), and a STEP file from the Files app (Share → FoldForm): the
   project opens as a copy in FoldForm's folder, the STEP file as a new
   project.
15. Cut a pocket (the owner's flow of 2026-09-27): Box → tap the top face →
   **Sketch**: the face fills most of the width; draw a 10 x 10 rectangle
   with one finger (corners land on whole millimeters) → **Finish sketch** →
   tap the rectangle: the arrow starts where you tapped and the hint says
   "drag the arrow out … into it to cut". Drag the rectangle itself (or the
   arrow) down 5 mm: the value says **Cut depth**, **Cut** is highlighted,
   ✓ cuts the pocket. Also: type 5, then tap **Cut** (it goes in, not a
   dead end); tap **Join** and push in (refused, and the hint says why);
   tap a second face by mistake, then **Sketch** (it goes on the last face).
   Tap the pocket's floor: the floor is selected, not its edge.
16. **Editing a sketch by dragging** (your report of 2026-09-27). In a
   sketch, pick **Select** in the strip, then with one finger:
   drag a rectangle's side (it moves, the rectangle grows), a circle's rim
   (its size); tap inside a shape, then drag inside it (all of it moves);
   a drag inside a shape you did not tap first turns the view. Tap a side,
   then a finger right beside it drags it (not its size label); tap its
   dimmed length and type one (the rectangle resizes); double-tap a
   side (the whole rectangle is selected) and Delete; drag the end of a
   line onto a corner or a side (it stays joined). Blue items can still
   move, dark ones cannot; dragging a fully sized one says so. Tap inside
   a closed shape → **Extrude**: the sketch closes and the arrow is under
   your finger. Try the same with the Pencil on the iPad.
17. Loft on the phone: the same as the iPad's step 18; the Loft options
   (New body / Join / Cut, Smooth, Straight, Apply) scroll sideways above
   the tool strip.
18. **Managing projects on Home** (2026-09-27). Touch and hold a project's
   card (or tap its ⋯): **Duplicate** (a "… copy" card with the same
   picture appears first; the Files app shows the new file in FoldForm's
   folder), **Rename…** (the keyboard does not cover the card; a taken name
   is refused under the field; the new name shows on the card, in the Files
   app and, for the open project, after Save), **Delete…** (the card names
   the project and says it goes to Recently Deleted in the Files app).
   Then Files app → Browse → **Recently Deleted**: the project should be
   there; **Recover** puts it back (Home shows it again). Qt moves it with
   iOS's `trashItemAtURL` into `Documents/.Trash`, which the Files app
   shows as Recently Deleted; if FoldForm instead asks "Delete … for
   good?", iOS refused that: please report it (it then deletes for good,
   after saying so).
