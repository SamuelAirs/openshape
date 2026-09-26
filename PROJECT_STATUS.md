# Project status

_Last updated: 2026-09-26, after the overnight session (Windows release
pipeline, recovery and robustness, sketch toolkit part 3, body tools, holes
and draft). New here? Read "Handoff" first._

## Current milestone

**Milestones 0, 1 and 2 complete; Milestone 4 core (editable history) done;
Milestones 3 and 5 partial; the Windows build is distributable.** The
overnight session (2026-09-25 → 26) added a GPL-free release build with an
installer, a portable zip and a release workflow; recovery copies with a
restore prompt and kernel-crash containment; stress tests and a project-file
fuzzer; the third part of the sketch toolkit; body tools (duplicate, split
into bodies, mirror/pattern as separate bodies, rotate about an edge or
point); counterbores, countersinks, a Hole tool and extrude with draft.
Still open from the owner's list for the night: text emboss, STEP import in
the UI, thumbnails and a start screen, previews off the GUI thread, the
universal iPhone/iPad layout, first-run guidance and a user guide, and the
first tagged release.

## Handoff: where we left off

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
- **CI:** every push runs Windows and macOS (`ci.yml`, free since the
  repository is public; documentation-only pushes skip it). The macOS
  acceptance run is `continue-on-error` in a 1024x653 window and passed 135
  of 144 core checks after the face-picking fix (TD-35). `ipad.yml` builds
  the iPad archive; `release.yml` builds, tests and packages the release
  (a `v*` tag publishes it). Run/job status and annotations can be read
  through the public API without a login (docs/IPAD.md, "Reading CI
  results"); failed steps report their error lines there.
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
    tools waiting for a face pick faces only (CI Mac 127 → 135/144); the
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

- STEP import: kernel function and tests; not exposed in the UI.
- Touch/pen: gestures (tap, drag, double-tap, two-finger pan/pinch after real
  movement, two-finger tap = undo, three-finger tap = redo), pen mode with a
  Pen switch, the touch layout and constraint-glyph taps are implemented and
  tested with synthetic Qt touch events; not yet tried on real touch
  hardware or an iPad.
- iPad: CI builds the archive on a GitHub Mac (47 MB); the TestFlight upload
  waits for the owner's Apple setup; never run on a device.
- Windows release: works end to end locally and on CI, but the installer
  and exe are unsigned, so SmartScreen warns (TD-44). CLAUDE.md's "Package"
  command still makes the dev package (not distributable; the script says
  so).
- Project thumbnails: the file format and the project writer accept a
  `thumbnail.png`, but nothing renders one yet (TD-11).
- Sketch toolkit part 3 without TD-27 (separate sketches on one plane still
  do not split each other's shapes) and TD-28's remainder (no center-point
  arc; a sketch pattern's spacing cannot be edited after Apply).
- Hole tool: positions cannot be changed once the step is applied (TD-47).
- Text emboss/deboss: planned (font registry, `Font_BRepTextBuilder`, prism,
  join or cut), not started.

## Broken / missing

- No STEP import in the UI, no start screen with recent projects, no
  thumbnails, no text or emboss.
- Previews and recompute run on the GUI thread: about 0.2 s per push/pull
  drag step and up to 1 s per recompute on the 249-face enclosure (TD-1).
- No iPhone build: the iOS app is iPad-only (`TARGETED_DEVICE_FAMILY` 2) and
  has no compact layout yet.
- No first-run guidance or user guide; help is the F1 card.
- CI covers Windows and macOS; Linux is unverified.
- Sketch: no splines, text or center-point arc.
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
- GUI-thread previews and QML binding churn stutter on big models (TD-1,
  TD-18, TD-19): measured 171-195 ms per push/pull drag step on the
  249-face enclosure.
- QRhi via GuiPrivate ties builds to a Qt minor version (TD-5).
- Smaller limits recorded for later: failed history rows do not suggest a
  working size (TD-43); Hole-tool positions are fixed once applied and all
  holes of a step share one size (TD-47); the draft's "closes up" check is
  analytic per edge, so a nearly closed top is refused (TD-48).

## Next concrete tasks (owner priorities)

1. The owner's test pass (docs/MANUAL_TESTS.md, now with bodies, holes and
   the installer): act on what they report.
2. iPad: the first TestFlight build once the Apple setup is done, then the
   owner's test pass on the iPad (docs/IPAD.md, "What to test").
3. The rest of the overnight plan:
   - text emboss/deboss (needs a font; download it only with the owner's OK
     in chat);
   - STEP import in the UI, project thumbnails (TD-11), a start screen with
     recent projects;
   - responsiveness: previews on a worker thread (TD-1, TD-4), split
     `stateChanged` and list models (TD-18), cache sketch/grid geometry
     (TD-19); measure before and after on `bench_session`'s enclosure;
   - iPhone + iPad: a universal iOS app with a compact layout that adapts
     live (phone portrait/landscape, the iPhone Duo folded and open, Split
     View), safe areas, Files open/save, touch-aware hints (TD-35);
   - first-run guidance, help card and shortcut reference, README with
     screenshots and `docs/USER_GUIDE.md`; the `v0.1.0` tag → GitHub
     pre-release.
4. If not done yet: switch the owner's `dist/OpenShape` (their desktop
   shortcut's target) to the release build (BUILDING.md section 6, step 3)
   and update CLAUDE.md's "Package" line to match.
5. Sketch: separate sketches on one plane should interact (TD-27; profiles
   from several sketches' curves); center-point arc, editable pattern
   spacing (TD-28); a tangent arc started from inside the line tool.
6. Hole tool follow-ups (TD-47): change positions after applying ("Edit
   holes"), grid snapping, per-hole sizes; draft for Revolve and push/pull
   (TD-48); an FDM allowance in the screw tables if the owner wants one.
7. Reliability follow-ups: report the OCCT crash upstream (TD-41), check
   unions against their inputs (TD-42), suggest sizes for failed history
   rows (TD-43).
8. Align follow-ups: snap alignment while moving (Shapr3D-style).

## Tests currently passing

393/393 headless (`ctest -LE gui`): GTest suites for core, geometry,
profiles, sketch model and solver, sketch edits (slot, fillet, trim), document,
commands, project files, recovery copies and recent files, UI state
(preferences, window placement, lock files), sketch features, face
attachment, camera, picking (hierarchies checked against the linear scan),
bodies (duplicate, split, copies, rotate about, delete), holes and draft,
robustness (seeded stress sessions, project-file fuzzing, failure messages,
the kernel-crash part), interaction (headless M0 script, sketch workflows and
tools, history editing, highlight, booleans, right-click, align, rotate,
mirror, pattern, sketch constraints, arcs, offsets, face edits, extrude
options, push/pull thickness and kept edges, touch gestures, pen mode, axis
marker). The release build (own OCCT): 362/362, including a check that every
OCCT toolkit loads from the own build.

`acceptance_gui`: 641/641 checks through the real UI in 19 scenarios:
`core`, `views`, `bodies`, `recovery`, `recent`, `preferences`,
`robustness`, `sketch3_centerrect`, `sketch3_polygon`, `sketch3_tangentarc`,
`sketch3_constrainticons`, `sketch3_mirror`, `sketch3_pattern`,
`sketch3_angle`, `holes`, `hole_tool`, `extrude_draft`, `hole_tool_face` and
`release`. Build with `-DOPENSHAPE_WARNINGS_AS_ERRORS=ON` (as CI does): 0
warnings.

## Platforms verified

| Platform | Build | Tests | Runs |
|---|---|---|---|
| Windows 11 x64 (MSYS2 UCRT64, D3D11), dev build | ✅ | ✅ 393/393 | ✅ 641/641 real-UI checks |
| Windows 11 x64, release build (own OCCT) | ✅ | ✅ 362/362 | ✅ packaged app with only System32 on `PATH`; installer tests |
| GitHub Windows runner (`release.yml`) | ✅ | ✅ | ✅ silent installer test (Release #1) |
| Linux | ⬜ | ⬜ | ⬜ |
| macOS 15 (CI, Xcode 26.3, Metal) | ✅ | ✅ | 🟡 screenshot; acceptance 135/144 core checks in a 1024x653 window (TD-35) |
| iPadOS (CI archive, arm64, 17+) | ✅ 47 MB | — | ⬜ waits for TestFlight |
