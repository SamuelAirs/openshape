# FoldForm: handover (2026-11-03, evening)

For whoever continues. Read `CLAUDE.md` first (the lead-engineer rules: GitHub only compiles,
run the local suites before every push, the real-UI suite moves the pointer on this PC for
about 15 minutes), then `PROJECT_STATUS.md`, then `docs/LAUNCH_CHECKLIST.md` (the owner's list)
and `docs/APP_STORE.md` (every listing text, marked in blocks the listing tool reads), then
this. The owner is Sam; he tests on an iPhone 16 Pro, an iPhone Duo and an iPad.

## Where it stands

- **Now (2026-11-03, evening):**
  - **At Apple:** 0.3.0 (build 54) is **Waiting for Review**, with the in-app purchase
    Unlimited Projects (`io.github.samuelairs.openshape.unlimited`, USD 9.99) in the same
    submission. It was submitted on or before 2026-11-02; release is manual.
  - **Territories:** free in 147 of 175. The 27 EU countries and China mainland are off on
    purpose: Sam is not a trader under the EU Digital Services Act, and China needs an ICP
    filing.
  - **0.3.1 is ready:**
    - Build 60 (main `1be3626`) is on TestFlight, and Sam ticked all five of its checks on his
      Duo.
    - The listing texts are ready (`docs/APP_STORE.md`, the blocks marked "changed
      2026-11-03").
    - The screenshots are ready: the captioned set and the six Duo shots
      (`docs/appstore/duo/`, section 3b).
  - **The steps to ship:**
    1. Apple approves 0.3.0, and Sam presses Release.
    2. On Sam's "tag 0.3.1": the local suites, the `v0.3.1` tag and its push, and the GitHub
       release with the iOS sources (BUILDING.md, "Before tagging a release").
    3. The tag build's number goes to the App Store chat (vault Inbox), which fills 0.3.1's
       page and submits it on Sam's word.
  - **Not pushed:** the commits after `1be3626`, docs and screenshots only, so no build. They go up
    with the tag.
  - **The vault:** `DuoApps/Board.md` has Sam's tasks, and `DuoApps/FoldForm/FoldForm handover.md` keeps
    the short version.
- **Earlier (the dates from here to 2026-11-02 came from a wrong clock):**
  - **TestFlight build 55** was 0.3.1 from main `e6576ff`:
    - the paywall leak closed (projects in subfolders count);
    - several faces selected at once, with a long press selecting all of a text;
    - push-to-subtract;
    - the pinch jitter fixed;
    - the value box docked at an edge on the Duo.
- **2026-11-02 (the PC's real date; the dates in this file before it ran ahead):** Sam's
  answers are notes on the Duo Apps Run Sheet artifact
  (https://claude.ai/artifact/63RCBUKpzLdk2tsEzCcLrF; read with the ArtifactData tool,
  collections `notes` and `ticks`, FoldForm's ids start with `f-`), and his Duo log came in.
  **File → Open closed the app on the Duo** (54 and 55; fine on the iPad; cause unknown,
  TD-158): on iOS File → Open is now Home, which lists the app folder's projects and its
  folders' (build 56). The black bars on the Duo: Qt kept the maximized window inside the safe
  areas; main.cpp sets `Qt::ExpandedClientAreaHint` on iOS (build 57, TD-150). The log's other
  findings (pen mode on, Shell failures) are in `PROJECT_STATUS.md`. The vault's
  `DuoApps/FoldForm/FoldForm handover.md` keeps the short version.
  `scripts/dev/check_lambda_captures.py` flags `src/app/acceptance/Duo.cpp:400` (`st`, a
  `shared_ptr`): Apple Clang doesn't warn for it, and CI #64 compiled it.
- **2026-11-02, evening (for build 58, not pushed when written):** build 55's five checks
  passed (Sam's Board ticks); Home's Open… still found no files on the Duo, so there Open… and
  Import STEP… explain the Files app's Share, as Dualtop does (TD-158). Five sample projects on
  Home, at Sam's request ("complex, aesthetic, and a good intro"): `src/samples/Samples.cpp`
  builds them, `OpenShape --write-samples resources/samples` writes the shipped files,
  `test_samples` fails when the two differ. They never count toward the free project.
- **2026-11-03, night (for build 59, not pushed when written):** build 58 passed on Sam's Duo
  ("Everything is working!"). He asked for a loading screen and a polish pass ("especially
  the file menu ... This should look premium"), and, in the vault Inbox before going to bed,
  to keep going and make marketing material ("Download blender or whatever tools you need":
  not installed, since downloads need his word in the chat). Done: opens run in the background
  behind a loading card (TD-164); the lamp shade sample rebuilt so it opens in 2.3 s instead
  of 17 s on the PC (its window cut met an offset surface); menus, palette, top bar, Home's
  buttons and cards restyled with an original line-icon set (TD-9); `--hide-ui` and
  `scripts/dev/marketing_samples.py` (`docs/marketing/samples/`). The lifecycle logging for
  TD-163 (`2d69a58`) goes up with it.
- **2026-11-03, morning:** pushed on Sam's word (main `ab57b4b`); CI 68 and iPad run 59
  passed, **build 59 is on TestFlight**, untested on a device (a Board task in the vault asks
  Sam to try it). Blender 4.5 is in `~/Tools` (installed by the Inkfold chat; Sam: "It's
  already installed"): `tools/export_bodies` and `scripts/dev/marketing_blender.py` made
  `docs/marketing/blender/` (stills, the five together, turntables). Committed after
  `ab57b4b`, not pushed: a push of main makes a TestFlight build, so it waits for the next
  app change.
- **2026-11-03, afternoon (build 60: pushed on Sam's word, main `1be3626`; CI 69 and iPad run 60
  passed, on TestFlight):** Sam's round on 59 (his log
  and 22 screenshots in iCloud Drive/Foldform, which this PC syncs at `~/iCloudDrive`): no crash
  through about 14 folds; he uses his Apple Pencil on the Duo ("It works great"). Fixed what the
  shots showed (turned tall, the top bar into the Model panel; the value box over the top bar
  with the panel open; marks and the view cube over the panel), his "perpendicular to the face
  going outward" for every face and edge arrow (`geom::edgeOutward`), and his scroll arrow on the
  palettes (`ScrollHint.qml`). TD-165: the keypad still covers the view buttons turned tall.
  `docs/APP_STORE.md` 3b: the six Duo screenshots, all in `docs/appstore/duo/` (two from 59,
  four from Sam's eleven on 60); his Board ticks pass build 60's five checks.
- **Docs and marketing on main:** the independent critique (`docs/CRITIQUE_2026-11-03.md`), the
  marketing plan (`docs/MARKETING_RENDERS.md`, with two recording briefs for Sam), the website
  hero still and the turning-part loop (`docs/marketing/`, rebuilt by
  `scripts/dev/marketing_hero.py`), the captioned App Store screenshot set for 0.3.1
  (`docs/appstore/captioned/`, rebuilt by `scripts/dev/appstore_captions.py`).

## Open, in Sam's words and the leads' findings

- Sam's video on the Duo (2026-11-03, frames in `docs/feedback/2026-11-03-duo-select/`): "Fold
  form is a bit clunky"; he could not select several faces, and wanted to "select all the faces
  of the text then drag the faces towards the object in order to subtract". Built in 0.3.1,
  untested on his Duo. "I tried to zoom in and it was very jittery": fixed in 0.3.1, untested.
- Owed by Sam, now in: the six Duo screenshots (`docs/appstore/duo/`, `APP_STORE.md` 3b; they
  go up with 0.3.1 and earn the "optimized for iPhone Duo" badge), his "Yep" on the
  description's Duo line, Restore purchase (his Duo already shows Unlimited Projects), and his
  Duo logs (59's gave the measured safe areas, TD-150).
- Technical debt worth knowing: TD-150 (Duo safe areas are estimates), TD-155 (resolved, the
  paywall leak), TD-156 (a cut into another body does not follow the pushed body if it moves
  later), TD-157 (the value box docks top or bottom, not along the Duo's side bar; the 720-pt
  threshold is a guess). `CLAUDE.md` still says the real-UI run takes about 30 s; it takes about
  15 minutes.
- The critique's remaining ideas, ranked: a guided first part with six sample projects; a
  half-folded "workbench" layout on the Duo (model above the crease, keypad below); icons and a
  denser phone tool strip; STL and 3MF import for remixing. Nothing of it is started.

## Rules that stand

- One tag per App Store version; App Store Connect takes no more builds of a tagged version.
  Main is 0.3.1; the next tag is `v0.3.1` when Sam says.
- Every push of main uploads a TestFlight build (ipad.yml), so push deliberately. Never push
  `[testflight]`-style markers here; the workflow needs none.
- Run the headless suite (861) and the real-UI suite (4,525 checks, the pointer moves) before a
  push; never weaken a test to pass.
- The written source offer in the Licenses screen gives ayers177@gmail.com from 0.3.1 (two
  tests that treated "@" as a placeholder were changed for it).
- The Apple Pencil works on the Duo (Sam, 2026-11-03: "It does support Apple Pencil on the iPhone
  duo"), so the listing names it there since 0.3.1's texts; no "Made for iPhone Duo" wording (that
  is Apple's badge).
