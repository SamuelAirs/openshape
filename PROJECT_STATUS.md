# Project status

_Last updated: 2026-10-31, the App Store launch day (lead engineer +
agents). New here? Read "Blockers" and the latest session report first._

## Blockers: waiting on the owner

Decided on 2026-10-31: the product is **FoldForm** (code, repository,
executable and file format keep the name OpenShape, TD-154), version
**0.3.0**, contact **ayers177@gmail.com**, and the App Store app is **free
for one project** with a one-time in-app purchase **Unlimited Projects**
(product ID `io.github.samuelairs.openshape.unlimited`, USD 9.99).

**FoldForm's side is ready** (audited on 2026-10-31, read-only, nothing
rebuilt): **build 54** is the build to submit. GitHub's public record of
ipad.yml run 41224892140 (tag `v0.3.0`, commit 5ebed49) says "build 54
from v0.3.0 uploaded" (59 MB, iOS 17+, iPhone and iPad, Xcode 27.1,
license gate PASS 15/15); Apple's processing of it is visible only in App
Store Connect. The tagged code has the display name FoldForm, version
0.3.0, exactly that product ID, Restore purchase on the sheet and in About,
About → Licenses with the source offer, `ITSAppUsesNonExemptEncryption`
NO, and no network code or analytics. The GitHub release v0.3.0 is
published with `OpenShape-0.3.0-ios-sources.tar`; every link in the app
and the listing answers (privacy policy and support page with the email);
the texts fit Apple's limits; the screenshots (iPhone 1320 x 2868, iPad
2752 x 2064) and `docs/appstore/iap-review.png` are at sizes App Store
Connect accepts. The headless suite on `main` (the tag's code at 0.3.1):
844/844.

**Done by the owner (2026-10-31, about 23:05):** the Paid Apps Agreement
is **Active**, the Small Business Program enrollment is sent, and the DSA
status is **not a trader**. Build 54 shows as processed in App Store
Connect and no email from Apple reported a problem.

**Done by the owner (2026-11-01, 04:21 to 04:48 UTC):** the in-app
purchase, Free with the availability (all except China mainland and the
27 EU countries), and the tries on build 54 (the real price and the sandbox
purchase, the share sheet, opening from the Files app: passed, the owner's
word; Restore on the iPad still owed, not blocking).

What is left is the owner's App Store Connect work in
[docs/LAUNCH_CHECKLIST.md](docs/LAUNCH_CHECKLIST.md): the listing (App
Information, App Privacy: steps 5 and 6), then **submit build 54 with the
purchase** (step 9).

**The owner's answers (2026-10-31):** Apple's standard EULA (docs/EULA.md
is not used: a custom EULA would publish the address and phone); the EU
left out, not a trader; 6.3" iPhone screenshots only if App Store Connect
asks, taken by the owner on the iPhone 16 Pro (1206 x 2622; the six scenes
in docs/APP_STORE.md, section 3), not rendered; the written source offer
gives ayers177@gmail.com from 0.3.1 (TD-102, "0.3.1" below).

**Not pushed:** `main` is ahead of origin by the three commits of the
release (5ebed49, which the tag already published, the 0.3.1 bump and a
status commit) and the audit's docs commits. A push of `main` now starts
ci.yml and ipad.yml (a TestFlight upload of 0.3.1), because those commits
change a workflow and `CMakeLists.txt`; it waits for the first 0.3.1 code
change (CLAUDE.md: GitHub minutes are for compiling). The live privacy
policy keeps the date "27 September" until then; its content is current.

**Device testing after submitting:** the Duo steps in docs/IPAD.md ("The
iPhone Duo", 7: selecting in a sketch; 8: send Logs/openshape.log), which
decide the first 0.3.1 fixes ("Next" below).

## Current milestone

**Milestones 0, 1 and 2 complete; Milestone 4 core (editable history) done;
Milestones 3 and 5 mostly done; v0.2.0 published (2026-09-27); v0.3.0 tagged
and released on GitHub (2026-10-31).** The
universal iPhone/iPad app runs on the owner's devices through TestFlight;
since 2026-10-30 it is built with the iOS 27.1 SDK for the iPhone Duo. A
free App Store release with an in-app purchase (FoldForm 0.3.0) waits
on the owner's App Store Connect steps above.

## Handoff: where we left off

### Session report (2026-10-31): FoldForm 0.3.0 for the App Store

**In one paragraph:** the owner chose to launch today as **FoldForm**, free
for one project with an **Unlimited Projects** in-app purchase. Three
tracks ran in parallel and are merged: the purchase (StoreKit 1 in
Objective-C++ behind `ui::Store`, the one-project rule in `ui/Purchase`,
the "Unlimited projects" sheet, Restore on the sheet and in About; iPhone
and iPad only, Windows unlimited), the rename (everything users see says
FoldForm; technical names stay, TD-154; the value box and the keypad are
now placed together, a bug the shorter title exposed), and Shell (the
abandoned 2026-09-27 experiment became tests/test_shell_retries.cpp: its
fallback returned a wrong shape and once crashed the kernel's checker;
results of retries are now measured, the C0 split works, two everyday
bodies that failed now shell, and "Try thinner walls" is said only when
true; TD-152, TD-153). The owner then reported from the Duo: "cannot figure
out how to select a line". With no hover, a tap on a line with a drawing
tool started a shape, and on the inner display the palette scrolled Select
out of sight: a finger's or pen's tap on a curve now selects it (and
switches to Select), and Edit heads the palette.

**Verified:** 844/844 headless tests; the full real-UI run 4356/4356; the
purchase's Objective-C++ compiled on CI (ipad.yml run 41211127830, Xcode
27.1, License gate PASS 15/15; a snapshot of the track, branch
`ci/purchase-ios-check`); Apple Clang caught four unused lambda captures
in the purchase scenario that GCC did not (fixed; run
`scripts/dev/check_lambda_captures.py` before pushing).

**Not verified:** the real StoreKit sandbox (needs the owner's in-app
purchase and the active agreement); the tap-to-select on a device. Only 8 agent worktrees were left (not 30,
as said on 2026-10-30); all are deleted (one held the shell experiment,
now in the tests). Three made later that day are still under
`.claude/worktrees/` (the shell, rename and review-screenshot tracks): all
merged into `main` and clean, one locked by a Claude process still
running, so they were left alone; on GitHub, the branches
`ci/purchase-ios-check` and `duo-ci` (one CI snapshot commit each) and
`worktree-wf_9132cfe3-632-1` (merged) can be deleted on the owner's word.

**Later that day: the submission audit** (read-only; build 54 is final).
What was checked and how is under "Blockers" above. Corrected in the
docs: Apple now wants a DSA status even with the EU left out; the
screenshot upload path and the 6.3" question; the description names the
LGPL libraries (docs/LICENSING.md's release step); the review notes say how
to get back to Home (File, Home); the price is 9.99; the share sheet and
Open-in lines wait for a device check; stale 0.2 / OpenShape / placeholder
lines. docs/LAUNCH_CHECKLIST.md is the owner's one list, in order.

### Session report (2026-10-30): iPhone Duo, Apple Pencil, the stale tracks merged

**In one paragraph:** after a month without changes, the five tracks left
unmerged on 2026-09-27 are on `main` (Home Rename/Duplicate/Delete, kernel
robustness from OpenShape3D, the view cube, phone sketching with face
snaps and a loupe, the keypad check); the real-UI run then found three
regressions between them, fixed. The iOS app is now built with Xcode 27.1
and the iOS 27.1 SDK, which the iPhone Duo needs to fill its inner display
(older builds run there at a phone-like size). The layout was checked and
fixed at every Duo size (outer, inner both ways round with the side bar on
either side, Split View) and keeps its state through folding. The Apple
Pencil gets palm rejection, hover on the iPad and pen/finger separation.
A Fable 5.1 agent reviewed the project; its critique is summarized below.

**What changed (and how it was verified):**

- **Merged tracks** (each built with warnings as errors and tested after its
  merge): Home project cards, kernel robustness (OpenShape3D, MIT), view
  cube, phone sketching, keypad check. Regressions the real-UI run found:
  a phone's live sizes vanished after the first corner (product fix: they
  stay, an unmeasured one shows a dash), the value box avoided the keypad's
  old position (product fix), a Text scenario double-clicked by accident
  (scenario fix).
- **iPhone Duo build:** `ipad.yml` runs on GitHub's `xcode-27` image
  (Xcode 27.1, iOS SDK 27.1, an iPhone Duo simulator); `select-xcode.sh`
  picks the newest released Xcode of a major version by its Info.plist;
  `Info.plist` declares the UIScene life cycle (required by the iOS 27 SDK)
  and every iPhone orientation. Verified: ipad.yml run 41160904873 built
  the 57 MB archive with Xcode 27.1 (branch duo-ci). The license gate
  learned two Qt files the new toolchain links. The SDK's fold API names
  (`UIHingeInteraction`, `UIViewReservedRegion`, `UIVerticalBarEdge`) are
  now known; fold-aware placement is TD-140.
- **Duo layouts** (`duo` scenario: folds and unfolds live mid-sketch,
  mid-edit, with the keypad, the Model panel and Home open): the sketch
  tool bar no longer covers the top bar on the inner display (nor in iPad
  portrait), the palette ends above the hint, unfolding keeps the Model
  panel open, the keypad is re-placed after a size or safe-area change,
  the selection is brought back into view, 44 pt keys on the outer
  display. Every window change logs one line (`window ... pt at 3x ...`).
  Safe areas are estimates until the owner's log (TD-150).
- **Apple Pencil (USB-C: no pressure, no double-tap; hover only on the
  iPad):** palm rejection (`interaction/PalmRejection`: touches beginning
  while the pen is down or within 0.5 s are ignored for their whole life),
  a finger can no longer break a pen stroke, Pencil hover highlights with
  the pen's tolerances, a hint when a finger tap is ignored in pen mode,
  the loupe for fingers only (`pencil` scenario injects Qt tablet events as
  the iOS plugin does). TD-141..143 list what is left.

**Tests:** 820 headless tests and 4050/4050 real-UI checks in 50 scenarios
(new: `pencil` 53, `duo` 265), built with warnings as errors.

**The Fable 5.1 review (2026-10-30), in short:** a capable engine (kernel
checks, crash containment, fuzzing) in an unproven product. Its top
points: the owner's decisions are the critical path (now at the top of
this file); the name collides with OpenShape3D; no automated test runs on
iOS, so touch, Pencil, keyboard and files are verified only with synthetic
events on Windows; recompute still blocks the GUI thread (TD-1); very large
files (`InteractionController.cpp` 5.2k lines, `AppController` with 65
properties); topological naming picks the nearest signature without an
ambiguity check (TD-3); the geometry cache is never read on open (TD-8);
no icons and no autosave (TD-9, TD-51); 30 stale worktrees (36 GB) and
very long docs. Recommended next: owner decisions and a beta tag; a timed
end-to-end test on the devices; commit reusing the preview (TD-1), icons,
autosave.

### Session report (2026-09-27, evening): second test pass, Loft, the App Store

**In one paragraph:** everything you reported from your iPhone and iPad this
afternoon is answered and merged except the lock-on points on a face, which
are still being built:
- pinching or panning in a sketch no longer draws;
- each line stroke starts where your finger goes down;
- you can edit a sketch by dragging (lines, circles, whole shapes) and tap a
  size to type it;
- you cut a pocket as in Shapr3D, by sketching on a face and pushing the
  shape in;
- typing 100 no longer makes the model jump through 1 and 10 mm;
- touch devices get OpenShape's own number keypad, for a finger or the
  Pencil;
- double-tapping two bodies selects both.

**Loft** is in, as you asked. For the App Store:
- a share sheet for exports, and Open in OpenShape from Files and Mail;
- a Licenses view and a source-offer route that make selling the app sound
  under the LGPL;
- a privacy policy and a support page;
- the listing texts and twelve screenshots;
- TestFlight builds can now go to a public beta.

Each feature was built in its own worktree, reviewed twice, fixed and merged
by the lead. Before every push the lead built it with warnings as errors and
ran all headless tests and the full real-UI run.

**What changed (and how it was verified):**

- **Sketching with a finger** (your reports: "when I try to zoom or navigate
  in a sketch, I just make a rectangle"; lines "from the second I touch the
  screen to … the last place my finger touched").
  - A press that the recognizer turns into a pinch or a pan is taken back.
  - A Line stroke starts where it starts.
  - Two new interaction tests feed real recognizer frames.
- **Edit a sketch by dragging** (your report: "really difficult to edit a
  sketch… I have no idea how to resize rectangles").
  - Drag lines (a rectangle's side resizes it), circle rims and centers,
    arcs, whole shapes and selections.
  - Dropped points connect.
  - Tap a selected curve's size to type it.
  - Items that can still move are blue; fully sized ones are dark.
  - Scenario `sketchdrag` (114 checks).
- **Cut a pocket the Shapr3D way** (your description of the flow).
  - Sketching on a face frames that face.
  - A tap inside the shape picks the region, not an edge.
  - Push the shape into the body to cut, or drag the shape itself.
  - The value box shows Cut depth, Height or New body.
  - Cut, Flip and Through all.
  - Scenario `extrudetouch` (106 checks).
- **Typing sizes** (your report: the model snapped to 1 mm, then 10 mm; no
  easy way to type on the iPad).
  - The preview waits for a 0.7 s pause, or for ✓ / Next.
  - On touch, OpenShape's own keypad opens instead of the system keyboard:
    digits, `+ − × ÷`, parentheses, mm/cm/in/°, Next and ✓, large enough
    for the Pencil. It also types sketch sizes and Model panel values.
  - Scenario `numpad` (404 checks).
  - Pencil handwriting (Scribble) is not supported by Qt (TD-90): tap the
    keys with the Pencil.
- **Two or more bodies on touch** (your report: "really difficult or
  impossible to select two or more objects by double tapping").
  - Double-tap adds a body; a double-tap on a selected body takes it out.
  - While bodies are selected, a single tap adds or removes one.
  - A finger's wobble on an arrow no longer moves the body.
  - Scenario `multiselect`.
- **Loft** (your request).
  - Select closed shapes on different planes in order, then Loft.
  - Smooth or Straight; New body, Join or Cut.
  - Parametric: it follows its sketches and planes, and can be suppressed
    and deleted.
  - Exact-volume tests: a frustum is 7000 mm³; circles r=10 to r=5, 30 mm
    apart, give 5497.79 mm³.
  - Scenario `loft`, which clicks the whole flow, including Cut through a
    block and switching it to Join in the Model panel.
  - Limits: TD-106 and TD-107.
- **iOS integration.**
  - Export STL/3MF/STEP opens the share sheet (slicer apps, AirDrop, Mail).
  - File → Share Project….
  - STEP files and projects open from the Files app and Mail.
  - Crash reports are symbolicated (dSYMs).
- **App Store groundwork.**
  - Licensing: About → Licenses lists every library, its license and the
    written source offer. The iOS library sources are pinned and mirrored,
    and a license gate runs in `ipad.yml`. A custom EULA is in
    docs/EULA.md. The analysis is in docs/LICENSING.md; it is not legal
    advice.
  - Listing: docs/PRIVACY.md (no data collected), docs/SUPPORT.md, and
    docs/APP_STORE.md (texts, twelve screenshots, your checklist and a
    timeline).
  - Home no longer sits under the Dynamic Island.
- **CI:** the new iOS sources test needed git and cmp in GitHub's MSYS2
  shells. They are installed now: the runs on 1760178 had failed on it.
  The license gate's first real run, on iPad build 47, reported every Qt
  file as of unknown origin. Qt for iOS ships static frameworks, which the
  gate's rules did not cover. That is fixed and self-tested; the next iPad
  run will confirm it (TD-103). A release tag needs the gate to pass.

**Tests:** 725 headless tests; 2869 real-UI checks in 44 scenarios
(both all passing on the merged main, built with warnings as errors). CI
is green on Windows, macOS, the iPad build and the Release dry run.

**Waiting for you (product decisions; details in docs/APP_STORE.md):**
1. **The name.** OpenShape3D (Laan Labs, open source under MIT) has been a
   free touch CAD app on the App Store since 2026-08-06. A paid "OpenShape"
   next to it invites confusion and possibly a rejection. Recommendation:
   rename before launch, or at least use a distinct store name such as
   "OpenShape: CAD for 3D Printing".
2. **Price.** Recommendation: USD 4.99 one-time, universal, no in-app
   purchases. About 24 sales a year cover the USD 99 developer fee.
3. **EU trader status.** A paid app makes you a trader. The EU shows your
   address, phone and email publicly: use a P.O. box, or leave the EU out.
4. **Contact details.** The email placeholders in docs/PRIVACY.md and
   docs/SUPPORT.md, and the name, address and email in docs/EULA.md.
5. **A beta release tag** (e.g. v0.3.0-beta1). It carries the iOS library
   sources that the TestFlight builds' source offer promises. `ipad.yml`
   warns until a release has them.
6. **App Store Connect** (by hand, docs/APP_STORE.md sections 1-6): the
   public beta group, then App Review, submitted by 2026-10-13 (2026-10-16
   at the latest).

**OpenShape3D, and what we take from it.** Its source is on GitHub under
the MIT license: Swift, SwiftUI and Metal over the same OpenCASCADE kernel.
Tracks port its useful parts (credited in THIRD_PARTY.md as they merge):
- kernel robustness: time limits, a repair attempt, fuzzy booleans, better
  refusals and exact volumes (merged);
- a view cube (merged);
- project Rename, Duplicate and Delete (merged);
- a check of our keypad against its tests (merged).

Queued: a section view and snap refinements. Where OpenShape is ahead:
- a Windows version;
- exact B-rep checks after every operation;
- STEP files;
- printing tools such as hole allowances and text;
- suggested sizes when an operation fails;
- the licensing route for a paid app.

**Still running:** nothing; all five tracks are merged (2026-10-30), their
real-UI scenarios still to be run (viewcube, sketchphone, typedvalues,
homeprojects, openshape3d).

**Try first:** docs/IPAD.md, iPad steps 12-18 and iPhone steps 13-17 (the
value box, two bodies, the keypad, sharing, Open in, cutting a pocket,
editing by dragging, Loft), and docs/MANUAL_TESTS.md 18-19 on Windows.

**Known limits worth knowing** (details in docs/TECHNICAL_DEBT.md):
- Scribble does not work in value fields (TD-90).
- A loft between shapes on planes only is always a new body (TD-106).
- The store screenshots are rendered on Windows (TD-95).
- The Windows installer stays unsigned until SignPath approves.

### Session report (2026-09-27, morning) — the owner's iPhone/iPad feedback

**In one paragraph:** everything you reported after trying the app on your
iPhone and iPad is fixed and merged: the value box no longer covers what
you tapped, mirrored and patterned copies are independent, the view is
easier to read at every angle (perspective, a light fixed to the world, a
grid that fades out, soft shadows), and you can align to the X/Y/Z axes and
origin planes and create your own construction axes and planes. The print
helpers you approved are in too: a +0.2 mm hole allowance and raised or cut
text in Noto Sans. Before these, the same session added STEP import, a Home
screen with project previews, the universal iPhone + iPad app, previews on a
worker thread, the README and user guide, and published v0.1.0. Each feature
was built in its own worktree, reviewed twice, fixed, then merged, built with
warnings as errors and tested by the lead (all headless tests and the full
real-UI run) before each push.

**What changed (and how it was verified):**

- **The value box keeps clear** (your report: it covered what you tapped).
  It avoids one rectangle: the selection, where the tool has taken it (a
  pushed face, a moved body), the arrows and rings, and where you tapped.
  On phones it docks below the top bar or above the hint line, on the side
  away from the selection, and moves to the top while the on-screen keyboard
  is up; on iPad and desktop it sits beside the arrow or just past the
  selection. Sketch sizes shown while drawing move out from under the
  finger. Verified: 23 placement tests and a `chipplacement` scenario that
  checks the box against the selection at phone, iPad and desktop sizes and
  taps its buttons in the phone layouts. Not checkable on Windows: the real
  on-screen keyboard (docs/IPAD.md).
- **Independent copies** (your report: mirrored copies stayed linked).
  Mirror/Pattern copies made as separate bodies and split pieces are now
  independent bodies: editing the source changes none of them (and the
  other way round). Projects saved by 0.1.0 keep their linked copies, so
  they still open as they were.
- **A clearer look** (your report: "confusing and not realistic at
  different angles", "the grid cuts off abruptly"). Perspective by default
  (the button switches; your choice is remembered), a light whose height is
  fixed to the world (tops lightest, undersides darkest) and which turns
  with you around the vertical, so two sides seen at once never share a
  shade; the grid fades out softly, also towards the horizon; a soft
  shadow where a body stands on the ground. Measured over 7 views of 4
  parts: the smallest difference in shade between two faces meeting at an
  edge went from 1 to 20 (of 255), the median from 30 to 47, nothing washed
  out (`shading` and `perspective` scenarios).
- **Axes** (your question: "can you align to an axis? And can you create
  axis?"). Align onto the X, Y or Z axis, the XY, XZ or YZ plane or the
  origin (buttons, or click the drawn axis line). **Construct → Axis**
  (through a hole or shaft, along an edge, through two points, parallel to
  X/Y/Z) and **Construct → Plane** (offset from a face or origin plane, at
  an angle through an edge, midway between two faces): Rotate about them,
  Pattern around or along them, Mirror across them, Align onto them, Sketch
  on them. They follow the faces they were made from, have their own rows
  in the Model panel, and are saved. Verified with exact geometry tests and
  the `construct` and `alignorigin` scenarios (every mode and consumer
  clicked).
- **Print helpers** (your decisions). A hole allowance for 3D printing
  (Preferences; 0.2 mm to start with) widens clearance holes and
  counterbore/countersink seats; tap drills and heat-set insert holes stay
  as they are. **Text**: select a flat face, Text, type; size, depth
  (raised or cut in), angle, bold; editable later in the Model panel.
  Noto Sans (SIL OFL) is bundled, its hashes in THIRD_PARTY.md.
- **Earlier in the same session:** STEP import (Ctrl+I; the exact geometry
  is stored in the project), 256 px project previews and a Home screen, the
  universal iPhone + iPad app (compact layout below 600x500 that adapts live,
  safe areas, touch-worded hints), previews computed on a worker thread
  (TD-1; a kernel lock keeps OpenCASCADE single-threaded), README with
  screenshots and docs/USER_GUIDE.md, SignPath signing steps ready in
  `release.yml` (docs/CODE_SIGNING.md), **v0.1.0 published** as a
  pre-release.
- **CI:** a macOS-only test failure on main (a test kept a pointer into a
  temporary list; libc++ reuses freed memory, libstdc++ happened not to)
  was fixed; CI is green on Windows, macOS, the iPad build and the release
  workflow.

**Tests:** 582 headless tests; 1879 real-UI checks in 33
scenarios; the release build (own OpenCASCADE) passes 583 tests.
`dist/OpenShape` (your desktop shortcut) is the new release build.

**Decisions made for you (all reversible):**
- The light's direction around the vertical follows the camera (a fully
  fixed light makes two sides look the same from some angles, the effect
  you reported); sketching stays in perspective, facing the sketch plane.
- Field of view 35°; perspective is the default for new installs.
- Construction axes and planes follow the step they were made from (like a
  sketch on a face), not steps added later; Rotate/Pattern/Mirror/Align use
  where the axis or plane is when you apply them.
- Text is one line on a flat face, centered where you place it; kerning is
  missing (OpenCASCADE's font reader cannot read Noto Sans's kerning table).
- Copies in old projects stay linked (so those projects look the same).

**Try first:** docs/MANUAL_TESTS.md items 14-17 (print helpers, axes and
planes, documentation, the new look) and docs/IPAD.md (items 10-12 and the
iPhone section: text, axes, the value box, the on-screen keyboard).

**Published:** you said yes, so **v0.2.0** is out as a pre-release
(https://github.com/SamuelAirs/openshape/releases/tag/v0.2.0, 2026-09-27):
installer, zip and checksums, the release notes (CHANGELOG.md, reviewed
against the code by agents before tagging), and, new with this release, the
source code of the bundled LGPL libraries (TD-46). The downloaded installer
matches its published SHA-256.

**Known limits worth knowing** (details in docs/TECHNICAL_DEBT.md): the
value box keeps clear of a fillet's edge but not its whole new surface
(TD-58); no shadows from one body onto another (TD-62); text has no
kerning and works on flat faces only (TD-60); construction methods cover
the common cases (TD-67); orbiting can still carry the eye inside a
surface; the Windows installer stays unsigned until SignPath approves.

### Overnight session report (2026-09-26)

**In one paragraph:** OpenShape now has a GPL-free Windows build with an
installer and a CI release pipeline, never loses work (recovery copies and
restore after a crash), and gained the sketch toolkit part 3, bodies and
copies, holes and print helpers, clearer failure messages, a crash-safe
kernel wrapper and 50x faster picking. Every feature was built in its own
git worktree by an agent, reviewed by two independent reviewer agents,
fixed, then merged, built with warnings as errors and tested (headless +
the full real-UI run) by the lead before each push. CI is green on
Windows, macOS, the iPad build and the new release workflow.

**What changed, by area (and how it was verified):**

- **Installable on Windows (TD-17, TD-6, TD-34 resolved).** OpenCASCADE
  7.9.3 is rebuilt from source without FFmpeg/FreeImage/TBB
  (`scripts/windows/build-occt.sh`, SHA-256-pinned, MSYS2's patches); a
  `msys2-ucrt64-release` preset builds against it; the package has **no GPL
  files** (a license gate traces every file to its package and fails the
  build otherwise): 269 files / 160 MB instead of 364 / 292 MB, 24 MSYS2
  packages instead of 92, packaged in ~1.5 min instead of 5. Per-user NSIS
  installer (no admin; Start menu, uninstaller, `.openshape` association,
  icon, MPL page) 40 MB, portable zip 59 MB, SHA256SUMS. `.ico` and version
  info in the exe; the version comes from CMake everywhere.
  `.github/workflows/release.yml`: tag `v*` → GitHub Release; pushes that
  touch packaging → artifact. Verified: the packaged app passes the full
  real-UI run with `PATH` reduced to System32; installer tests (silent and
  clicking through the dialogs) install, upgrade, uninstall and leave
  nothing behind; **CI Release #1 green** (OCCT built in 44 min, cached now;
  362 tests against it; installer test passed on the runner).
- **Reliable.** Recovery copies of unsaved work (3 s after edits settle, at
  least every minute, and when the app loses focus; ~8 ms each), a restore
  prompt after a crash, a one-line crash log (Windows and POSIX), remembered
  window, recent files, Preferences (units, grid snap, recovery interval).
  Stress tests (random undo/redo and save/open sessions) and a project-file
  fuzzer found and fixed 7 real bugs (a kernel crash, deep-JSON stack
  overflow, id wrap-around, non-transitive recompute, a vanishing body,
  booleans mutating their inputs, a typed-flag exception). Crashes inside
  OpenCASCADE become failed steps; crashes elsewhere are still real crashes
  (logged, and reported by Windows / TestFlight). Failure messages name a
  size that works ("Try 19.5 mm or less."); operations that would change
  nothing say so. Picking uses a BVH: hover 2.5–3.1 ms → 0.04–0.07 ms on a
  249-face enclosure.
- **Modeling.** Sketch: center rectangle (E), polygon (P, across flats),
  tangent arc (G), constraint glyphs you can tap and delete, sketch mirror
  and pattern, angle dimension; the sketch tools moved to a left palette.
  Bodies: Duplicate (Ctrl+D), Split into bodies (parametric), Mirror/Pattern
  as separate bodies (copies follow the source), Rotate about a picked edge,
  hole axis or corner. Holes: counterbore and countersink on a hole's rim
  (M2–M6, ISO 4762/10642), a Hole tool (click points on a face, snapping,
  typed X/Y, ISO 273 close/normal fit, tap drill, through all), extrude with
  draft. All verified with exact volumes and bounding boxes, and clicked in
  the real UI.
- **Test infrastructure.** The real-UI acceptance run is split into
  self-registering scenarios (19 now, 641 checks) with `--scenario`; all
  automated GUI runs take a machine-wide lock; failed checks log what the
  last click hit (this diagnosed the CI Mac's small-window failures: tools
  waiting for a face now pick faces, not edges or sketch profiles).
- **iPad.** Unused Qt Controls styles no longer linked (TD-36): 50 → 47 MB.
  The Mac CI acceptance run improved from 127/144 to 135/144 (TD-35).

**Decisions made for you (all reversible):**
- Installer: per user (no admin), desktop shortcut off by default,
  publisher "OpenShape contributors", version 0.1.0 as a pre-release.
- Recovery: default every minute (Off / 30 s / 1 min / 5 min in
  Preferences); copies omit the geometry cache (8 ms instead of 234 ms).
- Polygon sizes are across flats (a nut's wrench size); pattern counts
  include the original; the sketch tools sit in a left palette.
- Split into bodies is never automatic (a toast suggests it) and keeps the
  largest piece; separate-body copies follow their source; Duplicate is a
  fully independent copy.
- Screw tables: ISO values; the FDM allowance you chose (+0.2 mm) is added
  on top (Preferences), the tables stay ISO.
- The app always uses the light color scheme (dark Windows made menus black).
- The House AI service was paused overnight (with your OK) to free memory
  for parallel builds; it was started again at ~09:40 (running).

**Incident:** an installer test run by an agent deleted your desktop
shortcut (`OneDrive\Desktop\OpenShape.lnk`). It was recreated with the
same target (`dist\OpenShape\OpenShape.exe`) and working folder; if you
had added arguments (e.g. `--touch`), add them again. The test scripts now
refuse to run a setup that could touch the real desktop.

### Earlier notes

- **State:** the six overnight branches are merged into `main` (release
  engineering; app shell and recovery; sketch toolkit 3; bodies and copies;
  robustness and speed; holes and draft), together with the lead's
  integration fixes (`git log --oneline 8a60fe1..HEAD`). `release.yml` ran
  green on GitHub once (Release #1; building OpenCASCADE took 44 min there,
  later runs use the cache). Batch 3 of the overnight plan (onboarding, user
  guide, the `v0.1.0` tag and GitHub Release) had not started when these
  notes were written; the morning report above has anything later. If the
  working tree is not clean, someone changed it after this note.
- **Verify first** (Git Bash, `export PATH=$HOME/msys64/ucrt64/bin:$PATH`):
  - dev build: `cmake --preset msys2-ucrt64 && cmake --build build/msys2-ucrt64 && ctest --test-dir build/msys2-ucrt64 -LE gui`
    (393 pass);
  - real UI, only when nobody is using the mouse:
    `ctest --test-dir build/msys2-ucrt64 -L gui` (641 checks in 19
    scenarios); one scenario:
    `build/msys2-ucrt64/bin/OpenShape.exe --acceptance out --scenario bodies`
    (names under "Tests currently passing");
  - release build with the own OCCT (BUILDING.md section 6):
    `ctest --test-dir build/msys2-ucrt64-release -LE gui` (362 pass); its
    package has 269 files / 160 MB and must end with `License gate: PASS`;
    installer 40 MB, zip 59 MB.
- **Waiting on the owner:** the Apple-side setup in docs/IPAD.md section 1
  (Apple Developer enrollment, pending since 2026-09-25; then App ID, app
  record, API key and four GitHub secrets) before the first TestFlight
  build; the questions in the morning report above.
- **Last session (2026-09-25):** the owner modeled hands-on while the debug
  log was watched; everything they reported was fixed (right-click ends a
  line, sketching on a sketch continues it, separate pieces are flagged and
  joins that miss make new bodies, Model-panel rows highlight their
  geometry, booleans are reachable). Then their priorities were built in
  order: booleans + Align, Rotate, Mirror/Pattern, the sketch toolkit (arcs,
  eight constraints, construction), direct face edits, touch & pen
  groundwork. Details and lessons: docs/DEVLOG.md.
- **Latest (same day, later):** license MPL-2.0 applied; the repository was
  checked for anything sensitive (nothing found) and made public by the
  owner; push/pull shows and sets the part's size; X/Y/Z axes and an
  axis marker; slot, trim, corner fillet and offset in sketches; extrude
  symmetric and up to a face; push/pull takes fillets and chamfers along;
  touch layout with a Pen switch; About box; license list in the package;
  iPad build prepared (docs/IPAD.md).
- **iPad via GitHub + TestFlight (2026-09-25, in progress):** the owner's
  MacBook is too old for current Xcode, so the iPad app is built on GitHub's
  Macs (the repository is public, so they are free) and delivered with
  TestFlight to their iPad Air 11" (M2, iPadOS 26.6.1). Written: the macOS
  CI job (build, tests, Metal screenshot, acceptance run), `ipad.yml` and
  `scripts/ios/*` (iOS libraries, Qt for iOS, unsigned archive, cloud-signed
  TestFlight upload), an App Store icon. Verified on CI: the Mac app
  builds and passes its tests; the iPad app archive builds with its QML
  plugins linked (the first archives had none: see DEVLOG); it is 47 MB
  since only the Basic Controls style is linked (was 50 MB, TD-36).
  Signing/TestFlight waits for the owner's Apple Developer enrollment and
  the secrets (docs/IPAD.md).
- **Owner decisions:** the license is **MPL-2.0** (chosen 2026-09-25; see
  docs/LICENSING.md). Their global git
  `user.email` is malformed but they don't mind; this repository sets its
  own (GitHub noreply address).
- **Owner decisions for the overnight session (2026-09-25, evening):**
  - Permissions granted for the night: push to `main` as often as needed
    (after build + tests, waiting for CI between pushes); download sources
    and packages (OpenCASCADE's source for a GPL-free rebuild, MSYS2
    packages such as NSIS); run the real-UI acceptance tests while they
    sleep (never two at once); a tag-triggered CI release workflow and a
    first tag + GitHub Release (v0.1.0, pre-release).
  - Keeping the PC awake: not needed (their power plan handles it).
  - "An actual application" means, all four: a **Windows installer and
    release**, **reliability** (autosave, crash recovery, speed), **modeling
    features**, **help and onboarding**.
  - Modeling features wanted, all four groups: sketch toolkit part 3; bodies
    and copies (split pieces, pattern/mirror as bodies, rotate about an
    edge/point); holes and print helpers (counterbore/countersink, draft,
    text/emboss); files and settings (STEP import, recent files,
    thumbnails, preferences, shortcut reference).
  - Autosave: **recovery copies** (written periodically and after edits into
    the app's data folder; the user's file changes only on Save; restore is
    offered after a crash).
  - Apple Developer enrollment: **still pending**. Polish the iPad build
    anyway (what can be checked without a device).
  - Bugs: nothing new; they have not used the app since the last session.
  - **Later the same night:** the owner wants OpenShape on their phone —
    their current iPhone and the new foldable **iPhone Duo** (announced
    2026-09-09, ships 2026-10-23 with iOS 27: 7.6" inner display wider than
    tall, 5.4" outer display, Split View) — possibly to sell apps. So the
    iOS app becomes universal (iPhone + iPad) with a layout that adapts to
    any window size (phone portrait/landscape, the Duo folded and open,
    Split View halves). Their current phone is an **iPhone 16 Pro** (6.3",
    402x874 pt, Dynamic Island). The app stays free and open source on
    GitHub; a paid App Store build may follow — they see no licensing issue,
    and indeed the public source and build scripts cover the LGPL relinking
    requirement for the statically linked iOS libraries (notices and source
    links remain; docs/LICENSING.md).
- **Owner decisions and feedback, 2026-09-26 late morning:** Apple
  Developer access approved and the TestFlight secrets set: the app ran on
  their iPhone 16 Pro and iPad. Feedback: (1) the value box (input field
  and its buttons) often covers what they tapped — seen in a screen
  recording on the iPhone, a fillet's Radius box beside the arrow over the
  tapped edge; (2) after mirroring into two objects, the geometry stays
  linked (the copy follows the original) — they expect independent bodies.
  Decisions: publish v0.1.0 (tagged); code signing through **SignPath
  Foundation** (free for open source; the owner applies, then two GitHub
  secrets); add a default **FDM clearance allowance (+0.2 mm)** for screw
  holes; **Noto Sans** (SIL OFL) for text emboss/deboss; real-UI tests may
  run while they are away from the PC. They applied to SignPath Foundation
  the same day (waiting for approval; the repository side is ready:
  docs/CODE_SIGNING.md). SignPath concerns only the Windows download: the
  iPhone/iPad app is signed by Apple (TestFlight / App Store).
- **CI:** every push runs Windows and macOS (`ci.yml`, free since the
  repository is public; documentation-only pushes skip it). The macOS
  acceptance run (`continue-on-error`, a 1024x653 window) passed completely
  for the first time in CI #32 (`aa4b8d3`), after the face-picking fix and
  the wait for camera animations (TD-35). `ipad.yml` builds the iPad
  archive; `release.yml` builds, tests and packages the release (a `v*` tag
  publishes it). Run/job status and annotations can be read through the
  public API without a login (`scripts/dev/ci_status.py`); failed steps
  report their error lines there.
- **Working with the owner:** they test hands-on and report issues while you
  watch the log (`OPENSHAPE_LOG=debug`, `scripts/dev/watch_log.py`). For each
  report: reproduce, fix, add a check that clicks the fixed path, re-package
  so their shortcut runs the fix. Shapr3D is the reference for UX questions.
  Push in batches: a newer push cancels a CI run still in progress.

## Overnight plan (2026-09-25 → 26)

Goal: a shippable application — installable, reliable, complete enough for
real printable parts, discoverable. Executed in batches; after each batch:
merge, build with warnings as errors, all tests (headless + real UI),
commit, push, CI green before the next risky change. Each track ran in its
own git worktree, was reviewed twice and fixed before the merge.

- **Batch 0 (done):** the real-UI acceptance run is split into
  self-registering scenarios (one file per feature area, `--scenario`), and
  automated GUI runs take a machine-wide lock so parallel work never runs
  two at once.
- **Batch 1 (done, all five tracks merged):**
  1. *Release engineering* — **done:** OCCT rebuilt without FFmpeg/FreeImage
     (TD-17), release preset, `.ico` icon and version info in the `.exe`,
     per-user NSIS installer (Start menu, uninstaller, `.openshape`
     association), portable zip, license gate (no GPL files), size (TD-6:
     292 → 158 MB) and packaging speed (TD-34: ~5 min → 45 s),
     `release.yml` (ran green once). Left: code signing (TD-44).
  2. *App shell & reliability* — **done:** recovery copies + restore after a
     crash, remembered window geometry, recent files, preferences; high-DPI
     checked with screenshots at 150 % and 200 %.
  3. *Sketch toolkit part 3* — **done except TD-27:** center rectangle,
     polygon, tangent arc, constraint glyphs, sketch mirror/pattern (TD-28,
     part), angle dimension. Coplanar sketches (TD-27) need their own task.
  4. *Bodies & copies* — **done:** split pieces into bodies (TD-22),
     mirror/pattern as separate bodies (TD-26), duplicate a body, rotate
     about a picked edge or point (TD-24).
  5. *Robustness & speed* — **done:** save/open and undo/redo stress tests,
     a project-file fuzz test, BVH picking (TD-2, TD-20), a 249-face
     benchmark part, failure messages. Seven real bugs found and fixed, a
     kernel crash among them.
  - Also by the lead: iPad links only the Basic style (TD-36, 50 → 47 MB);
    tools waiting for a face pick faces only (the CI Mac acceptance run now passes every check); the
    runner waits for camera animations and logs what a failed check's last
    click hit; OpenCASCADE's signal handlers active only during kernel calls
    (`KernelSignalScope`, TD-41).
- **Batch 2 (partly done):** holes & print helpers — **counterbore/
  countersink, the Hole tool and extrude with draft done; text emboss not
  started** (it needs a font; downloading one waits for the owner's OK in
  chat). **Not started:** STEP import in the UI, thumbnails, start screen
  with recent projects; responsiveness (previews on a worker thread TD-1/
  TD-4, TD-18, TD-19; measured before/after); **iPhone + iPad** (universal
  iOS app; a compact layout for phone-sized and Split View windows that
  adapts live when the iPhone Duo folds or unfolds; safe areas; Files
  open/save; touch-aware hints; TD-35).
- **Batch 3 (not started when this was written):** first-run guidance, help
  card and shortcut reference; README with screenshots and
  `docs/USER_GUIDE.md`; v0.1.0 tag → GitHub Release (pre-release); final
  package; morning report.

## What works (verified)

- Build: Windows 11, MSYS2 UCRT64, GCC 16.2, Qt 6.11.2, OCCT 7.9.3, Direct3D 11
  (RX 7800 XT). Clean configure+build verified in a fresh directory, also with
  `-DOPENSHAPE_WARNINGS_AS_ERRORS=ON` (as CI uses). A release preset
  (`msys2-ucrt64-release`) links OpenShape's own OCCT 7.9.3 build.
- **Windows release** (BUILDING.md section 6): OCCT built from the upstream
  tag with MSYS2's source patches but without FFmpeg, FreeImage, TBB, VTK
  and Draw (`scripts/windows/build-occt.sh`); a license gate that traces
  every packaged file to our build, our OCCT or an MSYS2 package and fails
  on GPL or untraceable files; a per-user NSIS installer (no administrator
  rights; Start-menu entry, optional desktop shortcut, Apps & features
  entry, `.openshape` association with the app icon; upgrades in place;
  the uninstaller keeps projects, settings and the data folder); a portable
  zip and SHA256SUMS (reproducible); icon and version information in the
  `.exe`; `release.yml` (a `v*` tag publishes a GitHub pre-release). The
  packaged release app passed the full acceptance run with only
  `C:\Windows\System32` on `PATH`; the installer passed its silent test
  (42/42) and its dialogs are clicked through by UI Automation
  (`scripts/windows/test-installer-dialogs.ps1`).
- App launches; QRhi viewport with MSAA, lighting, thick edges, adaptive grid,
  X/Y/Z axes (red/green/blue) and an orientation marker above the view buttons.
- Box creation; orbit (about the point under the cursor), pan, zoom-to-cursor,
  animated standard views, fit, ortho/perspective.
- Face and edge hover highlighting and selection; double-click body selection;
  touch/pen taps additive; larger touch tolerances. Only a left click or tap
  selects; right/middle buttons orbit/pan. Picking uses bounding-volume
  hierarchies per mesh, with results identical to the linear scan (TD-2).
  Tools waiting for a face (Mirror, Extrude up to face) pick faces only;
  Align ignores sketch profiles.
- Push/pull of planar faces, fillet and chamfer of edges: arrow manipulator,
  drag with zoom-aware snapping, live preview, typed unit-aware values,
  Enter/click-away commit, Esc cancel. **A clicked flat face shows the part's
  size to the parallel face behind it** (Height/Width/Depth/Thickness, with a
  blue measurement line); dragging or typing sets that size directly, and
  `+5` / `-5` change it by that much (owner request). Without a parallel
  face behind, the value is the distance moved.
- Sketching on the XY/XZ/YZ planes or a flat face: line, rectangle, circle
  and 3-point **arc** tools; endpoint/origin/midpoint snaps; horizontal/
  vertical inference; typed dimensions (incl. arc radius); click-to-edit
  dimension labels; constraints: horizontal, vertical, coincident, length,
  diameter, radius, horizontal/vertical distance, **parallel, perpendicular,
  equal, tangent, concentric, on line, midpoint**; construction toggle;
  point dragging; DOF status; delete. Right-click or Esc ends a line chain.
  Sketching on a sketch (or on a plane that already has one) continues it,
  so new curves split its shapes.
- PlaneGCS solver (vendored, unmodified) with conflict detection.
- Closed-profile detection; profile picking; extrude as new body, join or cut.
  An automatic join that would not touch the body becomes a new body, and so
  does an automatic cut that would remove nothing.
- Undo/redo for all edits (including every sketch step); failed operations
  never enter history.
- Save/open `.openshape` (versioned, validated, atomic save); STEP/STL/3MF export.
- Milestone 1 bracket built through the real UI by the acceptance runner,
  exported, then edited to 8 mm in the history panel with the holes staying through.
- Model panel: edit any step's values after reopening; failures explained in
  place; suppress/delete/hide. Hovering a row highlights it in the view (a
  step's new geometry, e.g. the fillet surface); clicking a body row selects
  the body, Shift adds. Steps that leave a body in several pieces are flagged.
  Changes propagate through chains of dependent bodies (transitive recompute).
- Sketches on faces follow their faces; through-all cuts.
- Shell, Move (X/Y/Z arrows), Rotate (X/Y/Z rings, 15° snaps), Revolve —
  editable history steps.
- **Sketch tools, part 2**: slot (O), trim (T: click a piece, shown red
  first), fillet on selected corner points (editable R; the sharp corner is
  kept as a reference), offset of selected curves (side by pointer, typed
  distance), "On circle" constraint.
- **Sketch tools, part 3**: center rectangle (E; stays centered), polygon (P;
  regular by construction, sized across flats, side count by typing or
  -/+), tangent arc (G; starts on a free end, chains, arcs join smoothly),
  constraint glyphs (H, V, ∥, ⊥, =, T, …) beside the geometry (select one
  and Delete; a click near a line still selects the line), sketch Mirror
  across a line (Symmetric constraint; half a profile becomes one closed
  shape) and Pattern (linear or circular; copies keep an Equal radius),
  angle dimension between two lines. The sketch tools sit in a scrollable
  palette on the left (Draw / Edit).
- **Extrude options**: Symmetric (total thickness, centered), Up to face
  (click a parallel flat face) and **Draft** (typed angle: tapered walls
  with sharp corners, squares become pyramid frustums and circles cone
  frustums; negative widens; refused when the walls would close before the
  full height).
- **Push/pull takes rounded and bevelled edges along**: fillets and chamfers
  around a pushed face keep their size (split, move, fill; exact volume
  check; otherwise the classic push/pull). New steps only (`keepEdges`).
- **Touch layout**: 44 pt controls and a Pen switch once touch is used (from
  the start on a tablet; `--touch` shows it on Windows); the tool palette
  scrolls when short.
- **About box** (File → About OpenShape): license, source link, bundled
  components; the Windows package carries THIRD_PARTY_LICENSES.txt for every
  bundled library with its source archive.
- **Direct face edits**: select holes, fillets, chamfers or bosses and press
  Delete to remove them (the neighbours heal the gap); click a hole or shaft
  wall and type its new diameter (print tolerance); offset other faces with
  their neighbours following (refused when they cannot follow).
- **Mirror** a body across a flat face or an origin plane (joined), and
  **Pattern** it in a row (X/Y/Z or along an edge) or around an axis (X/Y/Z
  or a hole/shaft), count/spacing/angle editable later. **Separate bodies**
  makes each copy its own body that follows the source (up to 100).
- **Bodies**: **Duplicate** (Ctrl+D, the action bar or the body's Model-panel
  row: an independent copy, selected with the Move arrows); **Split into
  bodies** for a body in several pieces (the largest piece stays, every
  other piece becomes a body that follows upstream edits; a cut or Subtract
  that leaves pieces says so); **Rotate about** a clicked straight edge or
  hole/shaft (one ring) or around a clicked corner or circle ("Center pivot"
  goes back). Deleting a body that others are built from hides it instead
  and says why.
- **Align**: a face/edge/circle of one body onto a face/edge/circle of another
  (touching faces, collinear edges, concentric holes/shafts), Flip, offset
  arrow, or "Onto ground" to lay a flat face on the build plate.
- **Booleans reachable in the UI**: Union / Subtract / Intersect for two or
  more bodies from the selection action bar or the Combine palette; Swap
  flips which body is cut; one undo step.
- **Holes**: on a hole's rim, **Counterbore** and **Countersink** next to
  Heat-set insert (M2–M6 screw presets, also on blind holes; refused with a
  reason when the head is not wider than the hole, reaches through the part
  or is deeper than the hole). The **Hole tool** (a flat face's Hole action
  or the Modify palette): click to place holes, snapping to the face's
  center and edge middles and lining up with them, with circles and with
  placed holes; typed X/Y or "From last hole"; M2–M6 × close fit / normal
  fit (ISO 273) / tap; Through all or a depth; Counterbore / Countersink;
  Remove hole; one undoable step whose holes follow the face.
- Modify/Combine tool palette: each tool runs with a fitting selection or says
  what to select.
- **Failure messages say what to try**: refused fillets, chamfers and shells
  name a size that works while previewing ("Try 19.5 mm or less.", in the
  document's unit); operations that would change nothing are refused with a
  reason; a step that turns into a no-op after an upstream edit shows a
  warning and the steps after it still build; a refusal without a value
  chip (Mirror) shows in red in the hint line.
- **Recovery copies**: while a document has unsaved changes, a copy goes to
  the app's data folder 3 s after edits settle, at least every minute
  (Preferences: Off / 30 s / 1 min / 5 min) and when the app goes to the
  background; the user's file changes only on Save. After a crash, or any
  exit with unsaved work the user did not discard, the next start offers
  Restore / Discard / Decide later. An unexpected termination writes one
  log line with module and offset (TD-39).
- **Kernel faults become failed steps**: an access violation inside an
  OpenCASCADE call ends as a failed step with a message; a crash anywhere
  else is a real crash, logged and left to the OS crash reporter (TD-41).
- **App shell**: File → Open Recent (last 10 existing projects, Clear
  Recent); File → Preferences (Ctrl+,: units for new documents, grid
  snapping in sketches, recovery interval); the window comes back where it
  was (clamped to the screens present; a first window fits scaled screens);
  the unsaved-changes question is an overlay (Save / Don't Save / Cancel)
  that blocks the window's shortcuts; light color scheme on any Windows
  theme. `--acceptance`, `--demo` and `--screenshot` use a throw-away
  settings and recovery folder.
- Measure (distance / parallel gap / angle), heat-set insert helper, help overlay (F1).
- `OPENSHAPE_LOG=debug` logs per-operation timings.

## Performance (measured, i5-13500)

21-face filleted part:

| | Before 2026-09-25 fix | Now |
|---|---|---|
| Push/pull drag preview (per pointer move) | 130 ms | 37-40 ms |
| Body tessellation (820 triangles) | 43 ms | 1-4 ms |
| Recompute after an upstream edit | 57 ms | 14-17 ms |

The preview moves the fillets along (split + fuse, ~33 ms of kernel time
on this part; the old prism + boolean against tangent fillets took 38 ms).
The tessellation figure depends on whether the body's faces were already
meshed by earlier previews (0.9 ms warm, ~4 ms cold).

249-face enclosure (shelled, rounded corners, 95 vents with chamfers, screw
bosses with pilot holes; 25k triangles; RelWithDebInfo, machine shared with
other builds):

| Measurement | Result |
|---|---|
| Tessellation | 85-108 ms |
| Building the pick hierarchies | 7-9 ms per mesh |
| Push/pull drag preview on the rim | 171-195 ms per move (about 170 ms kernel, 17-19 ms meshing) |
| Fillet drag preview on a boss edge | 52-69 ms |
| Recompute after the box height edit | 630-1015 ms |
| Hover pick, bodies only | 2.5-3.1 ms before the hierarchies, 0.04-0.07 ms after |
| Hover pick with sketch profiles | 0.10 ms |

On a realistic maker part the GUI-thread previews and recompute are a
visible stall: the case for previews on a worker thread (TD-1). OCCT's
non-destructive booleans cost nothing measurable. A recovery copy of a
21-body, 1528-face model takes 7.6 ms (a full save with the geometry cache:
234 ms). Re-measure with `bench_session` (BUILDING.md, "Developer tools").

## Partially implemented

- iPhone / iPad: the universal app runs on the owner's iPhone 16 Pro and
  iPad through TestFlight; the value box's docking above the on-screen
  keyboard and the Pencil palm check are untried on a device
  (docs/IPAD.md); the iPhone Duo ships 2026-10-23.
- Windows release: works end to end locally and on CI, but the installer
  and exe are unsigned, so SmartScreen warns (TD-44); SignPath Foundation's
  approval is pending (docs/CODE_SIGNING.md). CLAUDE.md's "Package"
  command makes the dev package (not distributable; the script says so).
- Sketch toolkit part 3 without TD-27 (separate sketches on one plane still
  do not split each other's shapes) and TD-28's remainder (no center-point
  arc; a sketch pattern's spacing cannot be edited after Apply).
- Hole tool: positions cannot be changed once the step is applied (TD-47).
- Text: one line on a flat face, no kerning, Noto Sans only (TD-60).
- Construction geometry: axes and planes only (no points), common
  construction methods (TD-67); consumers use a datum where it is when
  applied (TD-65).

## Broken / missing

- Recompute after an edit still runs on the GUI thread (previews do not):
  up to 1 s on the 249-face enclosure.
- CI covers Windows and macOS; Linux is unverified.
- Sketch: no splines, text in sketches or center-point arc.
- No shadows cast by one body onto another (TD-62).
- Push/pull carries fillets only where everything it moves through is
  straight walls; elsewhere it falls back to prism + boolean (TD-21).

## Recent architectural decisions

- Tight bounding boxes are cached per Shape; display paths (mesh resolution,
  camera, signature scale) use `approximateBoundingBox` (microseconds).
- A step's highlight is derived from kernel identity: faces of its output not
  present in its input, preferring those on new surface objects
  (`facesCreatedBy`), falling back to all changed faces (`facesChangedBy`).
- Operations can revise automatic choices after seeing the preview
  (`Operation::reconsider`), used for join -> new body.
- Every user-facing action gets at least one acceptance check that reaches it
  by clicking (booleans were unreachable while headless tests passed).
- Rotate and Align are Move steps with an optional rotation (`MoveFeature` +
  `RigidMotion`): one feature kind, one file-format entry. Align stores the
  resulting motion, not a link to its target.
- Mirror and Pattern join their copies into the body in one General Fuse
  pass; their plane/axis/direction is stored as geometry.
- Kernel results are checked against an invariant of the intent (shell
  removes volume, defeaturing changes the shape, offset face changes the
  volume by about area x distance): OCCT can report success with a wrong
  result.
- Touch gestures are recognized by a Qt-free class (`TouchGestureRecognizer`)
  so they are unit-tested; two fingers do not navigate until they move, which
  makes two-/three-finger taps usable as undo/redo.
- A line and an arc tangent at a shared end are solved as a direction
  (PlaneGCS angle constraint, as FreeCAD does): "line touches circle" plus
  the shared point is degenerate there (false DOF, false conflicts).
- Push/pull keeps edge treatments by splitting the part below them, moving
  the top piece and filling the gap with the cross-section; a new
  `keepEdges` flag keeps older files computing as they did.
- OpenCASCADE's signal handlers are active only while a kernel call runs
  (`KernelSignalScope`), and every kernel try block uses
  `OCC_CATCH_SIGNALS`: faults inside the kernel become failed steps, crashes
  elsewhere stay real crashes. All booleans run in OCCT's non-destructive
  mode (by default they changed their inputs' tolerances in place).
- "Nothing would change" (`ErrorCode::NoEffect`) refuses a new step, but in
  recompute it is a warning that passes the input through, so older files
  and upstream edits never hide the later steps.
- New feature kinds `Split`, `SplitPiece` and `Copy` make pieces and copies
  bodies that follow their source; a body others are built from is hidden
  instead of deleted; Duplicate copies Combine tools and shares Copy/Split
  sources (decided by the kind of reference, not by visibility).
- Recovery copies are ordinary project files without the geometry cache,
  one per running app, marked live by a `QLockFile`.
- Hole-tool positions are stored in the face's frame (as sketches on faces
  are); rim heads extend `HoleFeature` with a type; screw and insert tables
  live in one file (`document/Fasteners`).
- A change that older builds would build wrongly is written so they refuse
  the file ("newer version"): countersinks have no `depth`, a drafted
  extrusion keeps its distance under `draft`.

## Known technical risks

- Kernel crash containment (TD-41): after a caught crash, objects leak and
  OCCT's state is not guaranteed; it relies on the C runtime's signal path
  (a crash handler must chain to the previous one); worker threads (TD-1)
  will need `OSD::SetThreadLocalSignal`. The crash found by the stress test
  (a General Fuse on a filleted two-piece body) is not reported upstream
  yet.
- OCCT's General Fuse on degenerate (tangent) contact gives run-dependent
  results, one of them "success" with lost volume, which our checks miss
  (TD-42).
- Release builds link our own OCCT, while the dev build and `ci.yml` test
  MSYS2's (same version and patches, different options; TD-45). When MSYS2
  moves to a newer OCCT, update `build-occt.sh` with it.
- THIRD_PARTY_LICENSES.txt points to MSYS2's source archives as the LGPL
  "corresponding source"; MSYS2 may delete old versions (TD-46): from 0.2.0
  on, releases attach them (with OpenCASCADE's source and patches); the
  v0.1.0 release still lacks them.
- Unsigned installer and exe: SmartScreen warns (TD-44).
- Topological naming on symmetric parts after large upstream edits (TD-3).
- Previews run on a worker thread under one kernel lock (TD-1, TD-4):
  anything new that calls OpenCASCADE must take it (CONTRIBUTING.md);
  recompute after an edit still runs on the GUI thread.
- QRhi via GuiPrivate ties builds to a Qt minor version (TD-5).
- Smaller limits recorded for later: failed history rows do not suggest a
  working size (TD-43); Hole-tool positions are fixed once applied and all
  holes of a step share one size (TD-47); the draft's "closes up" check is
  analytic per edge, so a nearly closed top is refused (TD-48).

## Next concrete tasks

Nothing here starts before the owner asks; App Review's answer and the Duo
log come first.

### When the owner's Duo log arrives (docs/IPAD.md, "The iPhone Duo", steps 7 and 8)

1. **Real sizes and safe areas.** `ui/WindowMetricsLog` wrote one line per
   change: `window <w>x<h> pt at <n>x (<px>), safe area top .. right ..
   bottom .. left .., usable .., <compact|regular> layout; screen ...`.
   Expect one each for the outer display upright and sideways, the inner
   display with the side bar right and left, the inner display turned, and
   a Split View half. Put the logged sizes and insets into the `Screen`
   constants of `src/app/acceptance/Duo.cpp` (`kOuter` 466x678 {59,0,34,0},
   `kOuterSideways`, `kInnerBarRight` 951x669 {0,84,20,0}, `kInnerBarLeft`,
   `kInnerTurned`, `kSplitHalf`), the table in docs/IPAD.md ("The iPhone
   Duo") and the `--safe-area` examples in BUILDING.md; close TD-150.
2. **What the new numbers break.** Run the `duo` scenario and the phone
   scenarios that place things against the edges (`compact`, `sketchphone`,
   `typedvalues`, `purchase`, `homeprojects`) through the real UI (it moves
   the mouse: when the owner is away), plus the headless suite. A control
   outside the real safe area is a layout fix in `ui/qml/Main.qml`, kept by
   the scenario's existing "inside the safe area" checks; never loosen a
   check to fit.
3. **Tap-to-select in a sketch** (cfe65a6, `SketchSession::pointerRelease`).
   When a finger or pen tap selected a line, the log has `user message: A
   tap on a line selects it, and Select is on now...` (once per sketch).
   If the owner's tap did not select and that line is missing, the causes
   in the code, in order: the tap counted as a stroke (moved more than
   `InputProfile::forDevice(..).dragThreshold`, `dragging_`), the pick
   missed (`pickReach(device)` at the Duo's 3x), or the tap was not one
   that would only have begun a shape (`pressBeganShape_`, e.g. a second
   corner) or it hit a point (which starts a shape there, by design). The
   log does not show which: if the owner's description does
   not settle it, add one Info line per touch tap in a sketch (device,
   distance moved, what was hit) and ask for one more log rather than
   guess. The fix starts as a failing test in
   `tests/test_sketch_phone.cpp` (beside
   `SketchPhone.ATapOnALineSelectsItWhileADrawingToolIsOn`), then the
   `sketchphone` scenario.
4. **Duo screenshots:** with the real sizes known, add the outer (466x678
   pt = 1398 x 2034) and inner (669x951 pt = 2007 x 2853) shots to
   `scripts/dev/appstore_screenshots.sh` and the check script (TD-95).
   Apple shows an "optimized for iPhone Duo" badge with them and requires
   them from April 2027 for apps built with the iOS 27.1 SDK.

### 0.3.1, in this order

`main` already says 0.3.1 (CMakeLists.txt, CHANGELOG.md). Push only with
the first of these code changes (all three local release commits go with
it), after the headless suite, the real-UI run and
`scripts/dev/check_lambda_captures.py`; tag `v0.3.1` when the owner wants
it in the store.

1. **The project limit's leak (TD-155), first.** Confirmed in the code on
   2026-11-03 (the critique's section 4, item 7): `appFolderProjects`
   (`src/ui/Purchase.cpp`) counts only the `.openshape` files at the top
   of the app folder (`QDir::entryInfoList`, not recursive), while a
   project anywhere inside the app folder opens in place without the gate
   (`AppController::openProject`: `stageIncomingFile` returns a file
   inside the folder unchanged, so `stagingAddsProject` is false). Moving
   projects into a folder in the Files app therefore lifts the limit.
   Fix: count recursively, minus `Exports`, the `Inbox`, `.Trash` and
   hidden files; a GTest in `tests/test_purchase.cpp` with projects in a
   subfolder, and the `purchase` scenario. Not worth holding the 0.3.0
   submission: it takes a deliberate move in the Files app, App Review
   does not test for it (guideline 3.1.1 asks that unlocking uses the
   in-app purchase, which it does), and a fix means a new tag and build
   55, restarting the submission.
2. **Fixes from App Review and the Duo** (above). A rejection that needs a
   new build is the owner's call; simplest is to rename the App Store
   version to 0.3.1 and submit the `v0.3.1` tag's build, since the tag
   check in ipad.yml wants the tag to match `CMakeLists.txt`. Also check
   the privacy manifest (checked 2026-11-03): Apple's required-reason
   list for user defaults names only the `UserDefaults` (`NSUserDefaults`)
   API; QSettings' native format on iOS calls Core Foundation's
   `CFPreferences` functions, which the list does not name; Qt 6.11.2
   calls `NSUserDefaults` only under `Q_OS_MACOS` (`qcore_mac.mm`,
   `qlocale_mac.mm`), and neither FoldForm's code nor the Qt iOS plugin
   files checked use it or the active-keyboards API. Build 54 drew no
   ITMS-91053 email (the owner). So nothing is missing for 0.3.0; adding
   `NSPrivacyAccessedAPICategoryUserDefaults` with CA92.1 in 0.3.1 is
   harmless and guards against Apple widening the list (low priority). **The written source offer's contact** (decided 2026-10-31):
   ayers177@gmail.com instead of the issue tracker (TD-102). Not done on
   the 0.3.0 code because it is more than the one line
   (`kSourceContactUrl` in `src/ui/Licenses.h`): `tests/test_uistate.cpp`
   checks a filled-in offer for any `@` to catch a placeholder left over,
   so those checks must look for `@NAME@` placeholders instead, and the
   `licenses` scenario (real UI) should run once after it.
3. **Real autosave** (TD-51: today a recovery copy keeps unsaved work, and
   Home asks Save / Don't Save). A project that has a file saves itself
   after each committed step (debounced) and when the app goes to the
   background; an untitled one keeps the recovery copy and gets a file only
   when the user names it, so autosave never creates a project and never
   meets the one-project limit (`ui/Purchase`). Check first that
   `io::ProjectFile` writes to a temporary file and renames. Tests: GTests
   for when a save happens and that a failed save keeps the recovery copy;
   an acceptance scenario that edits, backgrounds and reads the file back;
   Home opens another project without asking for a saved one.
4. **Icons for the text-only buttons** (TD-9): one original, consistent
   set (SVG in `resources/icons`, MPL like the rest, no copied icon fonts),
   for the tool palette and strip, the top bar, Home's card menu and the
   sketch tool bar. Each button keeps its `objectName` (the acceptance run
   clicks by it) and its text as the accessible name and tooltip; the
   screenshots are retaken afterwards (TD-95).
5. **Reuse the preview on confirm for large parts** (TD-1): the commit
   computes the step again (~190 ms on the 249-face enclosure) although
   the preview worker just made the same shape. Take the preview's shape
   when the document and the operation's inputs are unchanged since it was
   computed, else recompute as now. Measure before and after with
   `tools/bench/bench_session.cpp`; a test compares the reused result with
   a recompute (volume, bounding box, face count).

### Later (from the 2026-09-27 list)

- The owner's device test pass (docs/IPAD.md, iPad 12-18 and iPhone 13-18;
  docs/MANUAL_TESTS.md): act on what they report.
- When SignPath approves: the owner follows docs/CODE_SIGNING.md, then a
  test signing through release.yml.
- Follow-ups: Loft joins or cuts a body it runs into from planes only
  (TD-106) and guide rails (TD-107); Scribble in value fields (TD-90);
  keep the value box off a fillet's whole new surface (TD-58); orbiting
  should not carry the eye inside a surface.
- Sketch: separate sketches on one plane should interact (TD-27);
  center-point arc, editable pattern spacing (TD-28); splines.
- Hole tool follow-ups (TD-47): edit positions after applying, per-hole
  sizes; draft for Revolve and push/pull (TD-48). Construction points and
  more methods (TD-64, TD-67); text kerning and text on curved faces
  (TD-60).
- Reliability follow-ups: report the OCCT crash upstream (TD-41), check
  unions against their inputs (TD-42), suggest sizes for failed history
  rows (TD-43); recompute off the GUI thread.

## Tests currently passing

844/844 headless (`ctest -LE gui`, 2026-10-31, after the purchase, shell, FoldForm and sketch-selection merges; again that evening on `main`, the `v0.3.0` code at version 0.3.1): GTest suites for core, geometry
(including text, construction geometry and loft), profiles, sketch model and
solver (dragging, snapping and connecting), document, commands, project
files, recovery copies and recent files, UI state, camera, picking, bodies
and copies, holes and draft, robustness (stress sessions, file fuzzing,
failure messages, kernel faults on worker threads), async previews,
value-box placement, contact shadows, touch wording, touch selection and
the cut flow, the typing pause and the numeric keypad, typed values and
units, the view cube, phone sketching (face references, loupe placement),
palm rejection and pen hover, window metrics for the Duo,
project rename/duplicate/delete, kernel hardening (time limits, repairs,
BRep fuzzing, inputs left untouched), and the interaction
scripts; the license texts, the license gate's self-test and the iOS
sources scripts.

`acceptance_gui`: 4356/4356 checks through the real UI in 51 scenarios
(2026-10-31, after the merges of 2026-10-30 and 2026-10-31)
(`OpenShape.exe --acceptance <dir>`; names in the log's summary line and
under `src/app/acceptance/`). Build with `-DOPENSHAPE_WARNINGS_AS_ERRORS=ON`
(as CI does): 0 warnings.

## Platforms verified

| Platform | Build | Tests | Runs |
|---|---|---|---|
| Windows 11 x64 (MSYS2 UCRT64, D3D11), dev build | ✅ | ✅ 844/844 | ✅ 4356/4356 real-UI checks |
| Windows 11 x64, release build (own OCCT) | ✅ | ✅ 583/583 | ✅ packaged app with only System32 on `PATH`; installer tests |
| GitHub Windows runner (`release.yml`) | ✅ | ✅ | ✅ silent installer test (Release #1) |
| Linux | ⬜ | ⬜ | ⬜ |
| macOS 15 (CI, Xcode 26.3, Metal) | ✅ | ✅ | ✅ screenshot; the full acceptance run passes in a 1024x653 window (CI #32) |
| iOS / iPadOS (universal CI archive, arm64) | ✅ Xcode 27.1, iOS SDK 27.1 (2026-10-30) | — | ✅ TestFlight on the owner's iPhone 16 Pro and iPad (by hand); the iPhone Duo still to try |
