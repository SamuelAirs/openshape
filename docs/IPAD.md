# Running OpenShape on an iPad

**Status (2026-09-25):** the owner's MacBook is too old for current Xcode,
so the iPad app is built on GitHub's Macs (free for a public repository)
and delivered through **TestFlight**; no Mac is needed. The owner's iPad:
iPad Air 11-inch (M2), iPadOS 26.6.1.

| Step | State |
|---|---|
| Mac app (Xcode 26.3, Apple Clang 17, `-Werror`) | ✅ builds; 239/239 tests; Metal screenshot renders |
| Mac real-UI acceptance run (1024x653 window) | 🟡 127/144 checks (small-window failures being fixed) |
| iOS libraries (`build-deps.sh`) | ✅ 19 min on a 3-core runner, then cached |
| Qt 6.11.2 for iOS (`install-qt.sh`) | ✅ 2.4 min, then cached |
| iPad app archive (`build-app.sh`) | ✅ 43 MB, arm64, iPadOS 17+, icon and privacy manifest |
| Signing + TestFlight upload (`testflight.sh`) | ⬜ waits for the owner's Apple Developer enrollment and the secrets |
| Running on the iPad | ⬜ |

What is ready:

- Touch and pen: gestures (one finger, two-finger pan/pinch, two-/three-finger
  tap = undo/redo), pen mode (pen selects and draws, fingers only navigate)
  with a Pen switch, touch-sized controls (44 pt) that are on from the start
  on a tablet, a tool palette that scrolls. Try it on Windows with
  `OpenShape.exe --touch`.
- Build settings for Apple platforms: warning flags Clang accepts, the
  PlaneGCS solver built as a static library on iOS, an app bundle with an
  iPad `Info.plist` (`src/app/ios/Info.plist.in`: iPad only, all
  orientations, projects visible in the Files app, no export-compliance
  question), an App Store icon (`src/app/ios/Assets.xcassets`, rendered
  from `resources/icons/openshape.svg`), archive-friendly install settings.
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
6. **iPad:** install **TestFlight** from the App Store and sign in with the
   developer account's Apple ID.
7. **After the first upload:** App Store Connect → OpenShape → TestFlight →
   Internal Testing → **+** → a group (e.g. `Me`) with automatic
   distribution → add yourself. Later builds then arrive by themselves.

## 2. Getting a new build onto the iPad

Every push to `main` that changes code runs `ipad.yml` (so does a commit
whose message contains `[testflight]` on another branch). With the secrets
set, it uploads build number = the workflow's run number. Apple processes
it (usually 5–30 minutes; an e-mail says when), then TestFlight on the
iPad offers **Install** / **Update**. TestFlight builds expire after 90
days. The first run builds OpenCASCADE for iOS (about half an hour); later
runs take its cached copy.

## 3. Reading CI results (anyone, no sign-in)

GitHub shows job logs only to signed-in users, but for a public
repository the run and job status and the annotations are public. Failed
steps put their error lines and last lines into annotations
(`scripts/ci/run-logged.sh`); successful ones add notices (test counts,
screenshot numbers, app size, minimum iPadOS).

```bash
curl -s "https://api.github.com/repos/SamuelAirs/openshape/actions/runs?head_sha=$(git rev-parse HEAD)"
curl -s https://api.github.com/repos/SamuelAirs/openshape/actions/runs/<run id>/jobs
curl -s https://api.github.com/repos/SamuelAirs/openshape/check-runs/<job id>/annotations
```

Anonymous API calls are limited to 60 per hour per IP address.

## 4. Alternative: building on a Mac with Xcode

For a Mac with a current Xcode (26 or newer), e.g. to run the app on a
connected iPad from Xcode or to debug it there. The commands are the ones
CI runs; see `.github/workflows/` for the exact sequence.

The desktop app (checks the code on Apple's compiler):

```bash
brew install ninja opencascade qtbase qtdeclarative qtshadertools qtsvg nlohmann-json libzip eigen googletest
cmake --preset macos
cmake --build build/macos
ctest --test-dir build/macos -LE gui
open build/macos/bin/OpenShape.app
```

The iPad app:

```bash
scripts/ios/build-deps.sh ~/openshape-ios-deps       # static arm64 libraries, ~30+ min
scripts/ios/install-qt.sh 6.11.2 ~/Qt                # or the Qt Online Installer (macOS + iOS)
scripts/ios/build-app.sh ~/Qt/6.11.2/ios ~/openshape-ios-deps
open build/ios/OpenShape.xcodeproj
```

In Xcode: the `openshape` scheme, your iPad as the destination, Signing &
Capabilities → your team → **Run**. On the iPad, the first time: Settings
→ Privacy & Security → **Developer Mode** (on, restart).

## Expected rough edges

- Open and Save: projects live in the app's Documents folder, which the
  Files app shows under "On My iPad → OpenShape". How Qt's file dialogs
  behave on iPadOS (especially saving) needs testing.
- No hover highlight (touch has no hover; Apple Pencil hover is not used yet).
- Typing values: tap the value field for the on-screen keyboard (typing
  without tapping needs a hardware keyboard).
- Not measured yet: speed and memory with bigger parts.

The app's log is in the Files app: On My iPad → OpenShape → Logs →
`openshape.log` (share it with the Share button when reporting a problem).
TestFlight also collects crash reports and screenshot feedback in App Store
Connect.

## Licenses

For your own iPad there is nothing to do. For the App Store see
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
7. Rotate the iPad: the layout follows; the tool palette scrolls when short.
8. Note anything slow, hard to hit, or missing — with a screenshot
   (TestFlight: take a screenshot and share it as feedback, or send it).
