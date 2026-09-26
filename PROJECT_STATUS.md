# Project status

_Last updated: 2026-09-26 morning, after the first overnight session (lead
engineer + parallel agents). New here? Read "Handoff" first; the morning
report is at its top._

## Current milestone

**Milestones 0, 1 and 2 complete; Milestone 4 core (editable history) done;
Milestones 3 and 5 partial.** The owner's priorities from 2026-09-25 —
booleans and **align tools**, the sketch toolkit, transform & repeat, direct
face edits, the iPad/Pencil workflow ("similar to Shapr3D") — each have a
first, tested version. Their follow-ups are the next tasks below.

## Handoff: where we left off

### Morning report (2026-09-26) — the overnight session

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

**CI status:** CI #31 (Windows, macOS), iPad #20 and Release #1 green for
`cc1838c`; `aa4b8d3` (holes) pushed after that. The macOS acceptance step
is informational (TD-35).

**Still running when this was written** (merged as they finish, this report
updated): STEP import + thumbnails + a start screen; the universal
iPhone/iPad app with a compact layout (your iPhone 16 Pro and the iPhone
Duo); previews off the GUI thread (TD-1); README with screenshots and
docs/USER_GUIDE.md.

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
- Screw tables: ISO values with no FDM allowance (one table,
  `src/document/Fasteners.cpp`, if you want e.g. +0.2 mm).
- The app always uses the light color scheme (dark Windows made menus black).
- The House AI service was paused overnight (with your OK) to free memory
  for parallel builds; it is restarted when the session ends. If it is not
  running, a reboot (it starts automatically) or `Start-Service HouseAI`
  brings it back.

**Incident:** an installer test run by an agent deleted your desktop
shortcut (`OneDrive\Desktop\OpenShape.lnk`). It was recreated with the
same target (`dist\OpenShape\OpenShape.exe`) and working folder; if you
had added arguments (e.g. `--touch`), add them again. The test scripts now
refuse to run a setup that could touch the real desktop.

**Questions for you:**
1. OK to publish **v0.1.0** as a public pre-release now (you approved a
   tag; this only checks the timing suits you)?
2. Code signing (removes SmartScreen's warning) needs a paid certificate —
   worth it now, or later?
3. Screw holes: add an FDM clearance allowance by default (e.g. +0.2 mm)?
4. Text emboss/deboss needs a bundled font: OK to use Noto Sans (SIL OFL)?
5. When Apple approves your enrollment: docs/IPAD.md section 1 (about 10
   minutes) switches on TestFlight for iPad and iPhone.

**Try first:** docs/MANUAL_TESTS.md (new: holes, bodies, recovery,
Preferences, the installer).

**What's next:** see "Next concrete tasks" below.

### Earlier notes

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
- **CI:** `ci.yml` (Windows, macOS), `ipad.yml` and `release.yml`. The
  repository is public: run/job status and annotations can be read through
  the public API without a login (`scripts/dev/ci_status.py`); failed steps
  report their error lines there.
- **Working with the owner:** they test hands-on and report issues while you
  watch the log (`OPENSHAPE_LOG=debug`, `scripts/dev/watch_log.py`). For each
  report: reproduce, fix, add a check that clicks the fixed path, re-package
  so their shortcut runs the fix. Shapr3D is the reference for UX questions.
  Push in batches (CI minutes on a private repo bill at 2x).

## Overnight plan (2026-09-25 → 26)

Goal: a shippable application — installable, reliable, complete enough for
real printable parts, discoverable. Executed in batches; after each batch:
merge, build with warnings as errors, all tests (headless + real UI),
commit, push, CI green before the next risky change.

- **Batch 0 (done):** the real-UI acceptance run is split into
  self-registering scenarios (one file per feature area, `--scenario`), and
  automated GUI runs take a machine-wide lock so parallel work never runs
  two at once.
- **Batch 1 (parallel worktrees):**
  1. *Release engineering:* OCCT rebuilt without FFmpeg/FreeImage (TD-17),
     release preset, `.ico` icon and version info in the `.exe`, per-user
     NSIS installer (Start menu, uninstaller, `.openshape` association),
     portable zip, license gate (no GPL DLLs), size (TD-6) and packaging
     speed (TD-34), `release.yml` (tag → GitHub Release).
  2. *App shell & reliability:* recovery autosave + restore after a crash,
     remembered window geometry, recent files, preferences, high-DPI check.
  3. *Sketch toolkit part 3:* center rectangle, polygon, tangent arc,
     constraint icons, sketch mirror/pattern (TD-28), coplanar sketches
     (TD-27).
  4. *Bodies & copies:* split pieces into bodies (TD-22), mirror/pattern as
     separate bodies (TD-26), duplicate a body, rotate about a picked edge
     or point (TD-24).
  5. *Robustness & speed:* save/open and undo/redo stress tests, a project
     file fuzz test, BVH picking (TD-2, TD-20), a bigger benchmark part,
     kernel-failure messages audit.
- **Batch 2:** holes & print helpers (counterbore/countersink, extrude with
  draft, text emboss); STEP import in the UI, thumbnails, start screen with
  recent projects; responsiveness (previews on a worker thread TD-1/TD-4,
  TD-18, TD-19; measured before/after); **iPhone + iPad** (universal iOS
  app; a compact layout for phone-sized and Split View windows that adapts
  live when the iPhone Duo folds or unfolds; safe areas; Files open/save;
  touch-aware hints; TD-35).
- **Batch 3:** first-run guidance, help card and shortcut reference; README
  with screenshots and `docs/USER_GUIDE.md`; v0.1.0 tag → GitHub Release
  (pre-release); final package; morning report.

## What works (verified)

- Build: Windows 11, MSYS2 UCRT64, GCC 16.2, Qt 6.11.2, OCCT 7.9.3, Direct3D 11
  (RX 7800 XT). Clean configure+build verified in a fresh directory, also with
  `-DOPENSHAPE_WARNINGS_AS_ERRORS=ON` (as CI uses).
- App launches; QRhi viewport with MSAA, lighting, thick edges, adaptive grid,
  X/Y/Z axes (red/green/blue) and an orientation marker above the view buttons.
- Box creation; orbit (about the point under the cursor), pan, zoom-to-cursor,
  animated standard views, fit, ortho/perspective.
- Face and edge hover highlighting and selection; double-click body selection;
  touch/pen taps additive; larger touch tolerances. Only a left click or tap
  selects; right/middle buttons orbit/pan.
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
  An automatic join that would not touch the body becomes a new body.
- Undo/redo for all edits (including every sketch step); failed operations
  never enter history.
- Save/open `.openshape` (versioned, validated, atomic save); STEP/STL/3MF export.
- Milestone 1 bracket built through the real UI by the acceptance runner,
  exported, then edited to 8 mm in the history panel with the holes staying through.
- Model panel: edit any step's values after reopening; failures explained in
  place; suppress/delete/hide. Hovering a row highlights it in the view (a
  step's new geometry, e.g. the fillet surface); clicking a body row selects
  the body, Shift adds. Steps that leave a body in several pieces are flagged.
- Sketches on faces follow their faces; through-all cuts.
- Shell, Move (X/Y/Z arrows), Rotate (X/Y/Z rings, 15° snaps), Revolve —
  editable history steps.
- **Sketch tools, part 2**: slot (O), trim (T: click a piece, shown red
  first), fillet on selected corner points (editable R; the sharp corner is
  kept as a reference), offset of selected curves (side by pointer, typed
  distance), "On circle" constraint.
- **Extrude options**: Symmetric (total thickness, centered) and Up to face
  (click a parallel flat face).
- **Push/pull takes rounded and bevelled edges along**: fillets and chamfers
  around a pushed face keep their size (split, move, fill; exact volume
  check; otherwise the classic push/pull). New steps only (`keepEdges`).
- **Touch layout**: 44 pt controls and a Pen switch once touch is used (from
  the start on a tablet; `--touch` shows it on Windows); the tool palette
  scrolls when short.
- **About box** (File → About OpenShape): license, source link, bundled
  components; the package carries THIRD_PARTY_LICENSES.txt for all 92
  bundled MSYS2 packages.
- **Direct face edits**: select holes, fillets, chamfers or bosses and press
  Delete to remove them (the neighbours heal the gap); click a hole or shaft
  wall and type its new diameter (print tolerance); offset other faces with
  their neighbours following (refused when they cannot follow).
- **Mirror** a body across a flat face or an origin plane (joined), and
  **Pattern** it in a row (X/Y/Z or along an edge) or around an axis (X/Y/Z
  or a hole/shaft), count/spacing/angle editable later.
- **Align**: a face/edge/circle of one body onto a face/edge/circle of another
  (touching faces, collinear edges, concentric holes/shafts), Flip, offset
  arrow, or "Onto ground" to lay a flat face on the build plate.
- **Booleans reachable in the UI**: Union / Subtract / Intersect for two or
  more bodies from the selection action bar or the Combine palette; Swap
  flips which body is cut; one undo step.
- Modify/Combine tool palette: each tool runs with a fitting selection or says
  what to select.
- Measure (distance / parallel gap / angle), heat-set insert helper, help overlay (F1).
- `OPENSHAPE_LOG=debug` logs per-operation timings.

## Performance (measured, 21-face filleted part, i5-13500)

| | Before 2026-09-25 fix | Now |
|---|---|---|
| Push/pull drag preview (per pointer move) | 130 ms | 37 ms |
| Body tessellation (820 triangles) | 43 ms | 1-4 ms |
| Recompute after an upstream edit | 57 ms | 15-17 ms |

The preview now moves the fillets along (split + fuse, ~33 ms of kernel time
on this part; the old prism + boolean against tangent fillets took 38 ms).
The tessellation figure depends on whether the body's faces were already
meshed by earlier previews (0.9 ms warm, ~4 ms cold). Previews still run on
the GUI thread (TD-1). Re-measure with `bench_session` (BUILDING.md,
"Developer tools").

## Partially implemented

- STEP import: kernel function and tests; not exposed in the UI.
- Touch/pen: gestures (tap, drag, double-tap, two-finger pan/pinch after real
  movement, two-finger tap = undo, three-finger tap = redo), pen mode with a
  Pen switch, and the touch layout are implemented and tested with synthetic
  Qt touch events; not yet tried on real touch hardware or an iPad.
- iPad: build settings, Info.plist, icon, CI workflows that build it on a
  GitHub Mac and upload it to TestFlight (docs/IPAD.md); not run yet.
- Disconnected pieces: flagged in the Model panel, not yet split into bodies (TD-22).

## Broken / missing

- Packaging: a verified self-contained folder (`scripts/package-windows.sh`);
  no installer, large (~290 MB, see TD-6); **not distributable** (TD-17).
- No thumbnails in project files.
- CI covers Windows only; Linux is unverified.
- Sketch: no polygon, center rectangle, tangent arc, spline or text yet; no
  sketch-level patterns/mirror; constraints have no on-canvas icons yet.
  Separate (hidden or consumed) sketches on one plane still do not interact.
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

## Known technical risks

- **Do not distribute the Windows package yet:** it contains GPL FFmpeg/x264/x265
  DLLs and GPL-2.0 jbigkit, pulled in by MSYS2's OCCT (TD-17; see
  THIRD_PARTY_LICENSES.txt in the package). Fix: build OCCT without
  FFmpeg and FreeImage (needs the owner's OK to download its source).
- Topological naming on symmetric parts after large upstream edits (TD-3).
- GUI-thread previews and QML binding churn will stutter on big models (TD-1, TD-18, TD-19).
- QRhi via GuiPrivate ties builds to a Qt minor version (TD-5).
- Distributing binaries also needs the source to be public (the About box
  links to the GitHub repository). See docs/LICENSING.md.

## Next concrete tasks (owner priorities)

1. The owner's test pass (docs/MANUAL_TESTS.md): act on what they report.
2. iPad: get `ci.yml`'s macOS job and `ipad.yml` green (fix what they
   report), then the first TestFlight build on the owner's iPad and their
   test pass (docs/IPAD.md, "What to test").
3. Distributable Windows package: rebuild OCCT without FFmpeg/FreeImage
   (TD-17, after the owner's OK to download it), installer or zip, then a
   GitHub release.
4. Sketch toolkit, part 3: tangent arc, polygon, center rectangle,
   constraint icons, sketch patterns; separate sketches on one plane should
   interact (TD-27, TD-28).
5. Responsiveness: asynchronous previews (TD-1), split `stateChanged` and list
   models (TD-18), cache sketch/grid geometry (TD-19), BVH picking (TD-2, TD-20).
6. Split disconnected pieces into separate bodies (TD-22); pattern/mirror as
   separate bodies (copies) as an option (TD-26).
7. Align follow-ups: snap alignment while moving (Shapr3D-style); rotate
   about a picked edge or point (TD-24). Extrude with draft.

## Tests currently passing

239/239 (`ctest -LE gui`): GTest suites for core, geometry, profiles, sketch
model and solver, sketch edits (slot, fillet, trim), document, commands,
project files, sketch features, face attachment, camera, picking,
interaction (headless M0 script, sketch workflows and tools, history
editing, highlight, booleans, right-click, align, rotate, mirror, pattern,
sketch constraints, arcs, offsets, face edits, extrude options, push/pull
thickness and kept edges, touch gestures, pen mode, axis marker), plus
`acceptance_gui`: 147 end-to-end checks through the real UI (including
multi-finger taps, the touch layout, the About box, every sketch tool and
the face edits). Build with
`-DOPENSHAPE_WARNINGS_AS_ERRORS=ON` (as CI does): 0 warnings.

## Platforms verified

| Platform | Build | Tests | Runs |
|---|---|---|---|
| Windows 11 x64 (MSYS2 UCRT64, D3D11) | ✅ | ✅ | ✅ |
| Linux | ⬜ | ⬜ | ⬜ |
| macOS 15 (CI, Xcode 26.3, Metal) | ✅ | ✅ 239/239 | 🟡 screenshot; acceptance 127/144 in a 1024x653 window |
| iPadOS (CI archive, arm64, 17+) | ✅ | — | ⬜ waits for TestFlight |
