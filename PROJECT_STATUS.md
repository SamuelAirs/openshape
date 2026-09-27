# Project status

_Last updated: 2026-09-27, after the owner's iPhone/iPad feedback was
answered (lead engineer + parallel agents). New here? Read "Handoff" first;
the latest report is at its top._

## Current milestone

**Milestones 0, 1 and 2 complete; Milestone 4 core (editable history) done;
Milestones 3 and 5 mostly done; v0.1.0 published; the universal iPhone/iPad
app runs on the owner's devices through TestFlight.** Since v0.1.0
(2026-09-26): previews on a worker thread, independent copies, construction
axes and planes with Align onto the origin, a clearer look (perspective,
world-anchored light, fading grid, contact shadows), a value box that keeps
clear of what was tapped, a 3D-printing hole allowance and text. Everything
from the owner's first iPhone/iPad test pass is answered; the next step is
their second test pass (docs/MANUAL_TESTS.md 14-17, docs/IPAD.md) and, if
they agree, v0.2.0.

## Handoff: where we left off

### Session report (2026-09-27) — the owner's iPhone/iPad feedback

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

**Questions for you:**
1. Publish these as **v0.2.0** (a new pre-release with the notes in
   CHANGELOG.md), or wait for your test pass first?

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
  "corresponding source"; MSYS2 may delete old versions (TD-46): mirror them
  with the first public release.
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

## Next concrete tasks (owner priorities)

1. The owner's second test pass (docs/MANUAL_TESTS.md 14-17, docs/IPAD.md
   items 10-12 and the iPhone section): act on what they report.
2. With the owner's OK: tag **v0.2.0** (bump `project(VERSION)` in
   CMakeLists.txt, date the CHANGELOG section, tag, push the tag; release.yml
   publishes the notes).
3. When SignPath approves: the owner follows docs/CODE_SIGNING.md, then a
   test signing through release.yml.
4. Follow-ups from the reviews: keep the value box off a fillet's whole new
   surface (TD-58); orbiting should not carry the eye inside a surface;
   clear a refusal message once its cause is fixed ("Type the text first."
   lingers); the enclosure demo's hole-wall click picks a face at 402x874.
5. Sketch: separate sketches on one plane should interact (TD-27);
   center-point arc, editable pattern spacing (TD-28); splines.
6. Hole tool follow-ups (TD-47): edit positions after applying, per-hole
   sizes; draft for Revolve and push/pull (TD-48).
7. Construction geometry: points, more methods, re-picking references
   (TD-64, TD-67); text kerning via HarfBuzz and text on curved faces (TD-60).
8. Reliability follow-ups: report the OCCT crash upstream (TD-41), check
   unions against their inputs (TD-42), suggest sizes for failed history
   rows (TD-43); recompute off the GUI thread.

## Tests currently passing

582/582 headless (`ctest -LE gui`): GTest suites for core,
geometry (including text and construction geometry), profiles, sketch model
and solver, document, commands, project files, recovery copies and recent
files, UI state, camera, picking, bodies and copies, holes and draft,
robustness (stress sessions, file fuzzing, failure messages, kernel faults
on worker threads), async previews, value-box placement, contact shadows,
touch wording, and the interaction scripts. The release build (own
OpenCASCADE): 583/583.

`acceptance_gui`: 1879/1879 checks through the real UI in 33 scenarios
(`OpenShape.exe --acceptance <dir>`; names in the log's summary line and
under `src/app/acceptance/`). Build with `-DOPENSHAPE_WARNINGS_AS_ERRORS=ON`
(as CI does): 0 warnings.

## Platforms verified

| Platform | Build | Tests | Runs |
|---|---|---|---|
| Windows 11 x64 (MSYS2 UCRT64, D3D11), dev build | ✅ | ✅ 582/582 | ✅ 1879/1879 real-UI checks |
| Windows 11 x64, release build (own OCCT) | ✅ | ✅ 583/583 | ✅ packaged app with only System32 on `PATH`; installer tests |
| GitHub Windows runner (`release.yml`) | ✅ | ✅ | ✅ silent installer test (Release #1) |
| Linux | ⬜ | ⬜ | ⬜ |
| macOS 15 (CI, Xcode 26.3, Metal) | ✅ | ✅ | ✅ screenshot; the full acceptance run passes in a 1024x653 window (CI #32) |
| iOS / iPadOS (universal CI archive, arm64) | ✅ | — | ✅ TestFlight on the owner's iPhone 16 Pro and iPad (by hand) |
