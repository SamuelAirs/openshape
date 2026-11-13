# Development log

## 2026-09-25 — Project start through Milestone 0

**Environment.** Windows 11, i5-13500, 32 GB, Radeon RX 7800 XT. No C++
compiler, CMake, Qt or OCCT; no admin rights. Installed MSYS2 (base tarball
extracted into the user profile) and its UCRT64 packages: GCC 16.2, CMake
4.4.3, Ninja, Qt 6.11.2, OCCT 7.9.3, GTest, nlohmann-json, libzip. Upstream
OCCT is at 8.0.1; MSYS2 lags. Decision: build on 7.9.3, avoid deprecated APIs.

**Strict ISO mode hides `M_PI`.** OCCT headers use `M_PI`; with
`CMAKE_CXX_EXTENSIONS OFF` MinGW does not define it. Fixed with
`_USE_MATH_DEFINES` on the geometry target only.

**Push/pull via prism + boolean + unify.** Sweeping the face along its normal
(`BRepPrimAPI_MakePrism`), fusing or cutting, then
`ShapeUpgrade_UnifySameDomain` keeps a pushed box a clean 6-face box — tests
check face counts, not just volumes. A prism swept against the face normal is
inside-out; `BRepLib::OrientClosedSolid` fixes it before the cut.

**Picking on the CPU, not the GPU.** Decided to pick against our own
tessellation (ray/triangle for faces, projected segments for edges) instead of
an ID buffer. Makes picking testable headlessly and keeps ids ours. Hidden-edge
rejection uses a depth comparison against the visible face hit.

**Highlights as index ranges.** Tessellation emits each face's triangles
contiguously; storing per-face offsets lets the renderer draw a highlighted
face as a `drawIndexed` sub-range of the body's own index buffer: no per-hover
buffer rebuilds, scales to large meshes.

**QRhi headers moved.** In Qt 6.11 `rhi/qrhi.h` is only reachable through
`Qt6::GuiPrivate` (requested explicitly as a component). Accepted and
documented (ties us to the Qt minor version).

**Two-sided lighting without winding.** Backends differ in Y-flip, so
`gl_FrontFacing` is unreliable across D3D/Vulkan/GL; the shader flips the
view-space normal when `n.z < 0` instead.

**Visual verification from the command line.** `--demo <scene> --screenshot`
renders scripted states to PNG. First screenshots revealed an over-tight fit
(preview ran off screen), dark shading, and the value chip covering the arrow;
all fixed (fit margin 1.9, brighter ambient, chip placed beside the arrow tip).

**Real-input acceptance runner.** Direct controller calls could not verify
hover (Qt Quick re-sends synthetic hover events at the real cursor every
frame). `--acceptance` injects input through `QWindowSystemInterface` — the
path the platform plugin uses — so QML key handling and `Shortcut`s are tested
too. It immediately found a real bug: `StandardKey.Redo` already contains
Ctrl+Y on Windows, so listing "Ctrl+Y" as well made the shortcut *ambiguous*
and redo silently did nothing. Now 29/29 checks pass.

**CRLF pitfall.** Python edits on Windows wrote CRLF, which then made later
exact-match edits fail silently. Normalized the tree to LF (matches
`.gitattributes`); edit tools now used with explicit newline handling.

**`enable_testing()` order.** Tests registered from `src/app` were ignored
because `enable_testing()` ran after `add_subdirectory(src)`.

## 2026-09-25 — Milestone 1: sketching

**Solver choice made cheap.** PlaneGCS looked expensive to extract (FreeCAD
`Base/`, Boost.Graph, a precompiled header). In practice every dependency was
either unused or trivially shimmed; the upstream files are vendored byte for
byte and a union-find replaces `boost::connected_components` with identical
component numbering. Upstream now uses C++23 (`std::unreachable`), so the
solver target alone is C++23. Built as a DLL to keep LGPL obligations simple.

**Profiles via General Fuse.** Instead of writing a planar arrangement
(minimal cycle detection, nesting, T-junctions), a large face on the sketch
plane is split by all sketch curves with `BRepAlgoAPI_Splitter`; bounded
pieces are the profiles. Worked first time for nesting, "#" crossings and
dangling lines. Dangling edges end up INTERNAL inside faces and are removed
before extrusion.

**Profiles referenced by an interior point.** A region's centroid can lie
outside it (L shapes, rings), so profile references store a point from the
region's largest triangle plus the area.

**Headless first, again.** The whole sketch workflow (typed 60 × 40 rectangle,
finish, pick profile, extrude, fillet; circle-on-face cut) was driven through
`InteractionController` in unit tests before any rendering existed. Two test
failures were informative: a real bug (profile selection summary looked up a
*body* by the sketch id) and a wrong assumption (grid snapping had rounded the
drawn line).

**QML items found from the window, not its content item.** The acceptance
runner's `findChild` on `contentItem()` found nothing: items declared inside an
`ApplicationWindow` are QObject children of the window itself. A runner crash
(`front()` on an empty vector after earlier failures) was a test bug; the
application itself did not crash.

**Visual review changed behavior.** Screenshots showed consumed sketches
shouting in blue over finished parts, point markers rendering as thin bars,
and extrusion arrows pointing straight at the viewer after finishing a
sketch in top view. Now: consumed sketches recede (thin grey, fills only on
hover), zero-length segments render as squares, and finishing a sketch
restores the previous view orientation (tilting to iso if that was top-down).

## 2026-09-25 — Editable history, and a parametric trap avoided

**The history panel exposed a real modeling bug before it shipped.** Changing
the bracket plate from 5 to 8 mm left the hole sketch on the old plane
(z = 5) and the 5 mm cut then produced blind holes from inside the plate — a
silent wrong result, the worst kind for makers. Two fixes: sketches created on
a face now store an attachment (feature + face signature) and follow the face
through recompute; and cuts can be "through all", which is what a through-hole
means. Both are covered by headless tests and by the GUI acceptance run,
which edits the thickness in the history panel through real clicks and keys.

**Keep-failed edits.** Parameter edits from the history use
`SetParameterCommand` in keep-failed mode: a too-large fillet radius is kept,
marked red with "Unable to create this fillet. Try a smaller radius.", and
undo restores the value. Direct manipulation keeps rejecting failing values.

**Repeater/ListView delegates are not QObject children.** The acceptance
runner now falls back to a visual-tree search to click generated items.

## 2026-09-25 — Maker tools: shell, move, booleans, revolve, measure, 3MF

**Kernel "success" is not success.** `BRepOffsetAPI_MakeThickSolid` returns
the untouched solid (and `IsDone() == true`) when shell walls would meet. A
shell must remove material, so the result's volume is now checked against the
input. General lesson, applied since: verify kernel results against a cheap
invariant of the operation's intent, not only `IsDone()` and `BRepCheck`.

**Multi-handle operations.** Move needs three arrows; rather than a special
manipulator, `Operation` exposes N handles and an active one. The same hook
lets Revolve's arrow ride the swept arc instead of drifting along the initial
tangent (found by looking at a 270° screenshot).

**Out-of-range typed values must be refused, not clamped.** Typing 400° showed
a 360° preview next to "400" — refused now with an explanation.

**3MF by hand.** A ZIP with three small XML parts; vertices are welded on a
1 µm grid. Verified as closed manifolds with a divergence-theorem volume check.

**Tooling pitfall.** Backslash escapes inside shell heredocs were being
collapsed by the tool layer (e.g. `\\xC2\\xB7` arriving as raw bytes). Edits
with escapes now go through written script files or the Edit tool.

## 2026-09-25 — Measure first: a 3.4x recompute win

A throwaway benchmark (120×120×5 plate, 10×10 grid of Ø4 through-holes)
timed each stage before any optimization: cut 685 ms, full recompute after a
thickness edit 625 ms, tessellation 107 ms, profile detection 50 ms, pick
1.9 ms. The outlier was `extrudeFaces`/`revolveFaces` fusing tool solids
pairwise (quadratic). One General Fuse run with the first solid as argument
and the rest as tools: cut 241 ms, recompute 186 ms. Everything else is
already within budget for maker-sized parts, so off-thread work (TD-1) stays
scheduled for M3 rather than being rushed.

Also: OCCT keeps cylinder **seam** edges; a test clicking a hole rim hit the
seam instead. Seams are now skipped in display and picking (standard CAD
behaviour), which also removes the stray vertical line in every hole.

## 2026-09-25 — First hands-on session with the owner (live log monitoring)

The owner modeled freely while the app ran with `OPENSHAPE_LOG=debug` and a
watcher tailed the log and pinged the window's GUI thread (WM_NULL with a
timeout) every 0.5 s. Cheap, and it turned "feels sluggish" into numbers.

**The bounding box was the hot spot, not meshing.** Tessellating ~3,800
triangles took 140-200 ms and the GUI froze 300-580 ms per drag step on a
small filleted part. An isolated benchmark showed why: `BRepBndLib::AddOptimal`
(used by `geom::boundingBox`) took 42 ms on a 19-face filleted cube versus
0.03 ms for `BRepBndLib::Add`; the mesh itself took 4.7 ms. It ran inside every
tessellation, once per face/edge reference during recompute (topological
signature scale), after every edit and on UI refreshes. Now the tight box is
cached per Shape and display paths use a conservative fast box. Drag preview
130 -> 43 ms, tessellation 43 -> 0.85 ms, recompute 57 -> 14.5 ms (21-face part).
Lesson: time each stage before guessing; the obvious suspect (meshing) was
innocent.

**What remains is the boolean, and it depends on geometry.** Pushing a face
whose boundary is tangent to fillets costs ~38 ms in `BRepAlgoAPI_Fuse`; a
nearby face without tangent contact costs 6 ms. Parallel booleans were slower
at this size. Next steps: asynchronous previews (TD-1) and true "move face"
semantics for push/pull (fillets should follow the face, as in Shapr3D).

**A whole feature was unreachable.** Union/Subtract/Intersect only rendered in
the value chip, which exists only while a manipulator is active; two selected
bodies have none. Headless tests called `triggerAction("union")` directly and
never noticed. The acceptance run now does it through the real UI (Model panel
rows, then the selection action bar). Lesson: every user-facing action needs
one test that reaches it by clicking.

**Owner feedback, all acted on:** right-click should end a line chain (it
also selected and even committed in 3D: fixed); history rows gave no hint of
what they are (now highlighted in the view: a step's *new* geometry, found by
comparing kernel identity and surface objects of its input and output); an
extrusion from a sketch on a body that missed the body silently "joined" a
separate piece (now a new body; any step that leaves a body in several pieces
is flagged); booleans were not discoverable (tool palette plus action bar).
Still open from the session: curves of different sketches on one plane do not
split each other, and drawing on a plane with a sketch does not continue it.
(Continuing was added the same day; separate sketches still don't interact.)

**Tooling pitfall, again.** Python heredocs in the Bash tool turn `\n` inside
string literals into real newlines. Write such files with the Write/Edit tools.

## 2026-09-25 — The owner's priorities: align, transform & repeat, sketch toolkit, face edits, touch

Built in the owner's order, each with headless tests and, for the body
tools, acceptance steps that click through the real UI.

**One feature kind for Move, Rotate and Align.** A Move step gained an
optional rotation applied before its translation (`RigidMotion`). Rotate and
Align are Move steps with different operations in front, so the file format,
history editing and undo needed almost nothing new. Align stores the
resulting motion; it does not follow its target later (TD-23).

**Align frames.** Every alignable thing reduces to a point, a direction and
whether it is "sided": flat faces are (they end up touching, facing each
other), edges and axes are not (parallel, with the smaller rotation). Found
on the way: a full cylinder's area centroid lies on its axis, off the face,
so normals taken "at the centroid" were meaningless; `FaceInfo` now carries a
point on the face.

**Rotate rings** keep a constant screen size, unwrap the angle past half a
turn, and fall back to screen-space angles when a ring is seen edge-on.
Tests that grab a ring must avoid the points where two rings cross (they
grab at 45°).

**Mirror and Pattern join copies in one boolean pass** (the lesson from the
extrude speedup). Bugs fixed on the way: typing digits while no value
editor was visible (Mirror has none) edited a hidden value chip; the body
lost its selection after applying; the circular pattern's default axis is
now Z; and the default spacing came out 1 mm too large because the fast
bounding box carries a tolerance (ceil of 25.0000002 is 26).

**Sketch toolkit.** Arcs run counterclockwise from start to end (PlaneGCS
`Arc` plus its arc rules). The arc tool takes start, end and a bend point;
a typed radius locks it. Eight constraints were added as PlaneGCS mappings
(midpoint = the line's endpoints symmetric about the point; tangent keeps
the side the curves are on). Sketching where a visible sketch already lies
now continues it, so new curves split its shapes.

**Face edits: kernel success is still not success.** `BRepOffset_MakeOffset`
with one offset face returns a valid-looking solid even when the neighbours
cannot follow (tangent fillets); the volume must change by about area ×
distance or the edit is refused. Defeaturing can report success while
leaving the faces in place, so the shape must change. The per-face offset in
skin mode yields a shell that has to be closed into a solid. A used
sketch's circle lying over the hole it cut hid the hole's wall from clicks;
consumed sketches now win only where they lie on the surface hit.

**Touch.** Gestures are recognized by a Qt-free class so they can be unit
tested. Two fingers do nothing until they move past a threshold, which is
what makes a quick two-finger tap usable as undo (three fingers: redo). The
first pen press switches to pen mode: the pen selects and draws, fingers
only navigate. The acceptance run sends real Qt multi-touch events.

**Tooling.** A script that moved a struct between headers by regex cut it at
the `};` inside `axis{0, 0, 1};` — move code with an editor, not regexes on
braces. CI builds with warnings as errors; `-Wswitch` for a new enum value
and `-Wmissing-field-initializers` for a new struct field only surfaced with
the same flag locally, which the local build now uses. The input driver,
log watcher and benchmark used in this session are now in the repository
(`scripts/dev/`, `tools/bench/`) so the next session can use them.

## 2026-09-25 — License: MPL-2.0; CI confirmed green

The owner chose MPL-2.0 (file-level copyleft; allows store distribution,
unlike GPL). `LICENSE` is the official text (identical to the copies Eigen
and Qt ship), every one of the 130 source files outside `third_party/` got
the Exhibit A notice (after a shebang or GLSL `#version` line), and the
package ships `LICENSE.txt`. Distribution obligations are collected in
docs/LICENSING.md.

The owner also checked the private repository's Actions page: runs #3–#8
passed (#9 was still running), at 6–11 minutes each (billed at 2x for
Windows on a private repository). Pushes that only change documentation now skip CI, a newer
push cancels a run still in progress, and CI also builds the developer
tools so the benchmark keeps compiling.

## 2026-09-25 — Owner requests: X/Y/Z axes; resize by the size, not the difference

The owner wanted a Z axis next to X and Y, and, when clicking a face, to see
the distance to the other side and drag or type that size directly
("without doing addition and subtraction").

**Size instead of offset.** Push/pull now measures the part behind the face:
a line from a point on the face straight into the material must leave
through a parallel flat face (`IntCurvesFace_ShapeIntersector`). The value
chip then reads "Height 20 mm" (Width/Depth for side faces along X/Y), a blue
line shows what is measured, and typing 35 makes it 35. The stored step is
unchanged (the distance moved), so files and history editing did not change.
A leading `+` or `-` keeps the old relative meaning, which also kept most
existing tests' intent (`-5` still takes 5 mm off). The hole-diameter edit
was the model: value = what the user sees, feature = the difference.

**Measure on the face, not at the centroid.** A washer's centroid lies in its
hole, so the line from there never touches material. `pointOnFace` keeps the
centroid when it is on the face and otherwise takes the most interior point
of a sample grid. First version picked a point at the outer edge: samples only
cover the face's bounding box, so the box edges have to count as boundary.

**Pitfalls.** `near` is an empty macro in the Windows headers: a parameter
named `near` compiled into `toPnt()`. In QML, children with a negative `z`
are drawn behind their parent's fill — the axis pointing away from the
viewer vanished inside the marker's panel. A gtest `EXPECT_*` inside an
unbraced `if` trips `-Werror=dangling-else`.

**Packaging incident.** Re-packaging while the owner had the packaged app
open deleted its files from under it (MSYS2's `rm` can remove files that are
in use on Windows). The folder was restored within minutes, and the package
script now refuses to run while OpenShape runs from the output folder.

## 2026-09-25 — Sketch tools, extrude options, edges that follow, iPad groundwork

**A degenerate tangency.** A slot solved with the wrong number of free
parameters (7 instead of 3) and a filleted rectangle reported "conflicting
constraints" as soon as its radius changed. The cause: tangency between a
line and an arc sharing an end was "line touches circle" plus the shared
point; at the touching point those equations lose rank. FreeCAD solves it as
a direction (the line leaves in the arc's direction at that end); the
solver now does the same.

**Push/pull that keeps fillets without re-filleting.** Removing the fillets,
pushing and re-filleting is fragile (corner blends). Instead: split the part
on a plane just below the face's fillets and chamfers, move the top piece,
fill the gap with the extruded cross-section (or remove a slab), fuse. It
only runs where everything the moving band crosses is straight walls, and
must change the volume by exactly cross-section x distance; otherwise the
classic push/pull runs. A `keepEdges` flag keeps older files unchanged.
First version called the tight bounding box per face (tens of ms each on
curved faces): 82 ms per preview. With the mesh-based box: 37 ms, faster
than the old boolean against tangent fillets.

**Acceptance lessons.** A click placed below a rectangle landed on its
dimension label; freshly created action buttons were clicked before Qt laid
them out, so the click hit Delete (clickItem now lays rows out first); in the
iso view one click point projected exactly onto a body edge (edges win
picking); at low zoom a click 1 mm under a hole's rim picked the rim.
Pick points away from labels and edges, and zoom in for small targets.

**Packaging.** The license list needed one `pacman -Qo` for all DLLs (one
database scan) and no `pacman` inside a `while read` loop (it swallowed the
loop's input: 8 of 92 packages listed).

**Small C++ pitfalls.** `std::vector<double> low(std::size_t(count))` is a
function declaration (most vexing parse); a gtest macro inside an unbraced
`if` trips `-Werror=dangling-else`; GCC's `-Wshadow=local` is rejected by
Clang (now GCC-only, for the Mac build).

## 2026-09-25 — The Mac and iPad builds move to GitHub's Macs

**No usable Mac.** The owner's MacBook is too old for current Xcode, and
Xcode only runs on macOS. The repository is public now, so GitHub-hosted
Macs are free: `ci.yml` gained a macOS job and `ipad.yml` builds the iPad
app there and (with the owner's App Store Connect API key as secrets)
uploads it to TestFlight. Apple requires the iOS 26 SDK for uploads, so both
jobs pick the newest released Xcode 26 (26.3 on `macos-15`), never a beta.

**Reading CI without a login.** Job logs of a public repository still need
a signed-in user (the log API answers 403 without a token), but run/job
status and check-run annotations are public. `scripts/ci/run-logged.sh`
therefore turns a failed step's error lines and last lines into
annotations. A first idea, force-pushing logs to a branch, was refused by
the permission guard as destructive — and annotations need no write access.

**Apple Clang / libc++ differences.** Found on the first macOS run:
`std::from_chars` for `double` does not exist in Apple's libc++ (number
parsing now uses a classic-locale `istringstream`, Qt sets the C locale from
the environment on Unix); libc++ includes less transitively (PlaneGCS used
`std::inserter` without `<iterator>`, fixed in its force-included header
list, not in the vendored source). Clang's follow-up error ("variable 'err'
cannot be implicitly captured") was only a consequence of the missing
`std::inserter`. After that: 0 warnings with `-Werror`, 239/239 tests pass.

**OpenCASCADE for iOS** built on the first try with the flags of its own
`adm/scripts/ios_build.sh` (static, no Draw; OCCT forces GLES2 on for iOS,
harmless): 19 minutes on a 3-core `macos-15` runner, then cached.

**A small screen finds layout bugs.** The runner's screen made the window
1024x653: the acceptance run clicked Pattern and hit a button of the
selection bar lying over the palette; the same overlap happens on an iPad
in landscape (820 pt tall, 44 pt buttons), and in portrait the bar covered
the axis marker. The palette now ends above the bottom-left column, which
moves above the view buttons in narrow windows. `--size WxH` reproduces
iPad layouts on Windows (e.g. `--touch --size 820x1180`).

**Static QML plugins on iOS.** Qt 6.11 links the QML plugins of a static
build from a configure-time `qmlimportscanner` run over the *app target's*
source dir (no `*_qml_plugin_imports.cpp` any more). OpenShape's QML lives
in `src/ui`, so the scan found no imports: the iPad app linked and archived
fine but would have started without QtQuick, Controls, Dialogs or Layouts.
Only a build-time check caught it (`scripts/ios/build-app.sh` now fails on
an empty scan and lists the modules). Fixed with the target property
`QT_QML_IMPORT_SCANNER_EXTRA_ROOT_PATHS`. Other iOS-only link details:
OpenCASCADE's static targets name FreeType plainly (`-L` needed), and Qt
for iOS needs `QT_HOST_PATH` (the macOS Qt beside it).

## 2026-09-26 — Overnight: release pipeline, reliability, sketch toolkit 3, bodies, holes

The owner asked for "an actual application": an installer, reliability,
modeling features, onboarding. Five tracks ran at once in separate git
worktrees (release, app shell, sketch toolkit 3, bodies, robustness), the
holes track after them; each branch was reviewed twice and fixed before
the merge. Headless tests went from 239 to 393, real-UI checks from 147 in
one run to 641 in 19 scenarios.

**A GPL-free Windows release.** MSYS2's OpenCASCADE pulls in FFmpeg,
x264/x265 and FreeImage. `scripts/windows/build-occt.sh` builds OCCT 7.9.3
from the upstream tag with MSYS2's own source patches (so geometry behaves
as in the dev build and CI), only the toolkits OpenShape links: ~26 min
here, 44 min on CI, then cached. The package went from 364 files / 292 MB
to 269 / 158 MB (24 MSYS2 packages instead of 92). Pitfalls: `ntldd`
resolves a DLL next to the scanned file first, then in its own folder
(MSYS2's `ucrt64/bin`), and only then on `PATH`, so putting our OCCT first
on `PATH` does nothing; the package script copies it in before scanning
and compares every OCCT DLL byte for byte with the own build (that caught
a mis-resolved `libTKBO.dll`). The release tests silently loaded MSYS2's
OCCT too (same version, so nothing failed); a `TEST_LAUNCHER` now puts the
own build first and a test checks where each toolkit was loaded from.
CMake target compile options also reach `windres`, so warning flags are
now C++-only.

**A license gate instead of a license review.** MSYS2's license fields
describe whole packages, tools included: Qt, xz and gettext list GPL terms
for their tools, so a naive "contains GPL" check fails Qt.
`scripts/windows/license-gate.sh` parses the SPDX AND/OR expressions, keeps
a short reviewed list, traces every packaged file by content to our build,
our OCCT or an MSYS2 package, and fails on GPL-only or untraceable files.
The old dev package fails it with 27 problems; a release build cannot ship
if it fails.

**The installer, and an incident.** NSIS: `/D=` must come last and
unquoted; `Uninstall.exe /S` copies itself to `%TEMP%` and returns at once
(poll for the end); installer and zip are reproducible. UI Automation from
PowerShell sees NSIS's Win32 controls as plain panes until the client-side
providers are registered (after the first UIA call). While testing the
dialogs, a setup that was meant to be a test build was not one (a variable
set as `VAR=x bash script` did not reach MSYS2's bash from the agent's
shell; `env VAR=x bash script` does). Its finish page replaced the owner's
desktop `OpenShape.lnk` and the upgrade step's untick deleted it. Test
setups now name their desktop folder in the version resource, and both
installer tests refuse any other setup. Tests that touch per-user shell
state must prove they run a test build.

**Kernel crashes on MinGW.** A random stress session mirrored a filleted
two-piece body; OCCT's General Fuse dereferenced a null curve and the app
died. MSYS2's OCCT is compiled with `-DNo_Exception` (range checks become
crashes) and `OCC_CONVERT_SIGNALS`, which its CMake config exports for
Release builds only (our RelWithDebInfo build adds it when the package has
it). With `OSD::SetSignal` and
`OCC_CATCH_SIGNALS` in every kernel try block the access violation becomes
a failed step. Integration found the other side: the MinGW runtime calls C
signal handlers from an SEH handler around `main`, before any top-level
filter, so with OCCT's handlers always installed every crash *outside* the
kernel became `exit(1)`: no crash-log line, no crash report. OCCT's
handlers are now active only while a kernel call runs (`KernelSignalScope`,
counted across threads; TD-41). The crash reads garbage memory, so whether
it happens varies from run to run (the part is kept as a regression test).

**OCCT booleans change their inputs.** By default they widen the
tolerances of the input shapes in place, so cached outputs of earlier steps
changed under later ones (an interleaved undo/redo seed showed it). All
booleans now run in non-destructive mode, at no measurable cost. The same
hunt found a 200k-deep JSON file overflowing the stack (nesting is now
limited to 256), a wrongly typed flag escaping as an exception, sketch ids
near 2^32 wrapping, a body that vanished when a step after a failed one was
edited, and dependents recomputed only one level deep (the bodies track
found that one too).

**Two more crashes to steer around.** `BRepClass3d_SolidClassifier`
segfaulted inside `Extrema_ExtCC` on a plain plate with a Ø3.4 hole, and
`BRepOffsetAPI_MakeOffset` in its medial-axis code on a square face with a
1 mm hole. Both are crashes, not exceptions. Hole depths are now measured
with rays (the first face hit), and the draft's "closes up" check is
analytic: a line shortens by inset × tan(turn/2) at each corner, circles
and arcs change radius. The sketch Offset action also uses `MakeOffset`
and may hit the same crash with arcs or circles.

**Draft via DraftAngle.** An extrusion with draft is the straight prism
with its side faces tilted by `BRepOffsetAPI_DraftAngle`: planes stay
planes, circles become exact cones, corners stay sharp, and frustum volumes
match to 1e-6. A top that nearly closes (~0.2 mm radius) fails `BRepCheck`
and is refused (TD-48). Older builds must not build straight walls from a
drafted file, so its distance is written under `draft`, which they reject
(countersinks the same way: no `depth`).

**Changing nothing is a failure, for new steps only.** A cut that misses or
a mirror of a symmetric body now fails with a reason instead of adding an
empty step. The first version failed such steps in recompute as well,
which would have hidden every later step in older files and after upstream
edits that make a cut miss; in recompute it is now a warning. Working sizes
("Try 19.5 mm or less.") are searched by bisection only while previewing
(at most 10 attempts, 600 ms); the first version started at 2 % of the
refused value, so a slip like 2000 mm answered "at any size".

**A regular polygon needs its corners on a circle.** Equal sides around an
inner circle let an even polygon flex like a rhombus; corners on an outer
construction circle plus equal sides hold it regular. The inner circle only
carries the across-flats size, but it touched every side at its midpoint
and Trim cut sides in half: construction curves that only touch a curve no
longer cut it.

**Arc-arc tangency and signed angles.** Two arcs tangent at a shared end
lose rank the way line-arc tangency did (circle-circle tangency plus a
shared point). Tie the arcs' end angles instead: equal when one continues
the other, half a turn apart for an S-bend, keeping the whole-turn offset
of the unwrapped angles. An angle dimension stores the signed angle between
the lines' own directions (PlaneGCS `L2LAngle`) and shows the corner angle
only for display; derived from rays at solve time it flipped to the
supplement when the intersection passed a line's middle.

**Canvas overlays steal clicks.** Constraint glyphs with their own mouse
areas took clicks meant for nearby lines and points (the core scenario
caught one on a small slot). The sketch session now resolves taps itself:
geometry within pick reach wins, a glyph only otherwise. On iOS the
touch-mode default lived in `AppController` and never reached the
interaction core; the flag now lives only in `InteractionController`.

**Recovery and the window.** `QLockFile`'s 30 s stale time is safe for
long-lived locks: a live owner keeps the file open (no delete sharing on
Windows, `flock` on macOS), so an old but live lock cannot be taken. An exit
that is not the user's close (iPadOS unwinds out of `exec()`) must keep the
copy of unsaved work; only Don't Save discards it (a review caught the
first version deleting it). Before a window exists, `setFramePosition` is
taken as the client position on Windows, so a restored window crept up one
title bar per run: save and restore the client rectangle, recorded in
`closing` (after closing, the frame geometry equals the client geometry).
On a dark Windows theme the Basic style's menus took the system palette and
turned black: the app sets the light color scheme. A native `MessageDialog`
left the window without keyboard focus in a long acceptance run, so the
unsaved-changes question is an overlay. A menu whose item disappears in its
own `triggered` handler stays open with focus on a hidden separator: defer
with `Qt.callLater`.

**Acceptance pitfalls.** Two clicks on the same spot less than ~500 ms
apart (three 160 ms steps) arrive as a double-click and swallow the second:
put empty steps between them. `clickItem("historyRow_<id>")` clicked the
row's center, where an expanded row has its buttons; a new Duplicate button
moved Delete there and "select the body" deleted it (the name now means the
title line). `QStringLiteral("\xC2\xB0")` is two UTF-16 characters, not a
degree sign: use `QString::fromUtf8`. In `--demo --screenshot` the real
mouse position can deliver a hover. On the CI Mac's 1024x653 window, clicks
meant for faces picked edges or sketch profiles: tools waiting for a face
now pick faces only (127 → 135 of 144 checks there), the runner waits for
camera animations, and a failed check logs what the last click hit.

**Working in parallel.** Automated GUI runs take a machine-wide lock, so two
never drive the mouse at once. Agents leave PROJECT_STATUS, ROADMAP and this
log to the lead; TD numbers chosen in parallel collided and were renumbered
at the merges (sketch glyphs TD-40, release TD-44–46, Hole tool TD-47, draft
TD-48): give each track its own range next time. Every branch's two reviews
found real bugs (glyph spacing never on for the iPad, deleting a split
parent destroying its pieces, no-op steps blocking old files, recovery
copies deleted by a system-initiated exit). The worktree sandbox refuses
complex shell lines (heredocs with escapes, globs): write scripts with the
Write tool and run them. The scratchpad is shared between agents: use a
subfolder.

## 2026-09-26/27 — Owner feedback from iPhone and iPad: value box, copies, look, axes; print helpers

The owner ran the TestFlight build on an iPhone 16 Pro and an iPad and
reported: the value box covers what was tapped; mirrored copies stay
linked; "the geometry becomes confusing and not realistic at different
angles" and "the grid cuts off abruptly"; "can you align to an axis, and
create axes?". Four tracks answered them, plus the print helpers they had
approved (a +0.2 mm FDM allowance, Noto Sans for text). Each ran in its own
worktree with its own TD range (58, 60, 62, 64), two reviews and a fix pass.

**Keeping the value box clear.** Placement is a pure function
(`interaction/OverlayPlacement`) fed with one keep-clear rectangle: the
selection, where the operation has carried it (a pushed face, a moved
body), the arrows and rings, and the last press. On phones the box docks
below the top bar or above the hint, on the side away from the selection,
and moves to the top while the on-screen keyboard is up. What an operation
carries was first shifted on screen by the arrow's screen travel; with
perspective the default that is wrong (far points move less), so the
selection's points are now shifted in the world and projected. Presses
that do nothing (a resting palm in pen mode, right clicks) are not kept
clear. A Move on two axes needed its own "how the selection is carried"
(the arrows' bases already include the other axes' travel).

**A world-anchored light that still separates sides.** Lighting was in view
space with a 0.60 ambient, so a face's shade followed the view, and
orthographic boxes flipped in the mind (Necker cube). A light fixed fully
in the world has views where two visible sides get the same shade, the very
complaint. The key light's height is fixed (50° up, sky/ground hemisphere
ambient), its direction around the vertical follows the camera, 50° to the
viewer's left, like a photographer's turntable light. Measured with
`--face-contrast` over 7 views x 4 demos: the smallest shade difference
between faces meeting at an edge went from 1 to 20 (median 30 → 47), no
face washed out. Perspective is the default (35° field of view, the choice
remembered); the grid moved to a shader and fades with distance from its
center and, in perspective, from the eye; a soft contact shadow sits under
faces that rest on the ground. Zoom toward the cursor could take the eye
past the near plane and clip the face zoomed to: the distance now stops at
the minimum.

**Construction geometry.** Axes and planes are document objects (`Datum`)
that name the step whose output held their face or edge, like a sketch's
attachment, so they follow edits of that step. Picking: a plane outline or
axis at the same pixel distance as a body edge lost to the edge where they
crossed; the line wins when it is in front and within 2 px. Main's
independent copies duplicated a sketch but kept its link to the source's
construction plane, so editing the source moved every copy: copies now take
their own plane when it was made from the copied body. A typed plane
distance above the file format's 1e6 mm limit was accepted and made the
saved project unopenable: the tool checks the same limit as the file.
The origin's X/Y/Z lines are Align targets; the renderer and the picker
share one function for where they are drawn (`originAxisSegment`).

**Text.** `Font_BRepTextBuilder` with the bundled Noto Sans. OpenCASCADE's
font reader asks FreeType for kerning from the legacy `kern` table, and
Noto Sans keeps its kerning in GPOS, so pairs such as "AV" sit a little
apart (TD-60). FreeType is not thread-safe per face: it runs under the
kernel lock, taken before the font registry's mutex. A remembered text was
applied by a stray click before the user typed anything: a click only
applies once something changed in this use of the tool. Angles are stored
as directions in [0, 360°), so files with many turns still open.

**Integration pitfalls.** A test kept a pointer into the temporary vector
`historyRows()` returns; libstdc++ left the freed memory intact, libc++ on
the CI Mac did not (a deleted rvalue overload now prevents it). Apple Clang
also rejected four lambda captures that were never used (GCC does not warn):
`scripts/dev/check_lambda_captures.py` finds them on Windows. Branches
merged against an older main changed shared vocabulary (a selection kind
renamed, a grid function replaced, the default projection) without text
conflicts: the lead's merge build and full UI run caught each one. This
PC's second monitor is at 150 %: the acceptance driver now sends native
pixels. While the owner was gaming, an agent closed the offscreen process
that held the automation lock and UI runs took over their screen: when the
owner uses the PC, stop the agents, don't rely on the lock.

## 2026-09-27 — Second iPhone/iPad test pass, Loft, App Store groundwork

**Touch in a sketch.** The gesture recognizer sends a finger's press before
it knows whether a second finger follows, so a pinch began as a one-finger
drag and the Rectangle tool started a shape. When the recognizer turns the
press into a pinch or pan, `SketchSession::cancelPress` now puts back
whatever that press started. A Line stroke used to start at the previous
press's anchor. A stroke that starts away from the anchor now restarts the
shape where the finger went down.

**Editing by dragging.**
- Dragged points snap to the grid like drawn ones.
- When a length is added to a line, the solver splits the change between
  both ends, so corners can leave the grid. Acceptance checks therefore read
  positions at runtime.
- Qt Quick holds a finger's moves until the next frame. Synthetic drags need
  several steps, or `QML_NO_TOUCH_COMPRESSION=1`.
- Two taps in the same step become a double-tap.
- Point, curve and constraint ids share one counter per sketch, so a size
  label can name a curve.

**Cutting on a phone.** The report "a tap on the pocket floor picks an edge"
was partly a real edge: in an isometric view, the wall's vertical corner edge
was 4 px from the tap. A finger now takes the region unless an edge is within
6 px. Joining every body an extrusion merely touches broke existing flows.
For a sketch that is not on a body, an automatic Join now needs the profile
to lie on the body and be pulled away from it.

**Selecting several bodies.**
- The recognizer delivers both taps' press and release before the DoubleTap
  intent. The first tap had therefore already selected a face of the second
  body, and the double-tap then replaced the selection instead of adding to
  it. The selection is now remembered at each press, and the toggle is
  replayed on it.
- A pending value made a double-tap do nothing.
- Qt's mouse double-click arrives in two different orders.
- On a phone, an arrow's finger zone (36.5 px) can cover a whole small body.

**Typing.** On iOS, a read-only Qt Quick field gets a responder without
UIKeyInput, so no system keyboard shows. That is how OpenShape's keypad
takes the keyboard's place. Qt 6.11 has no Scribble support (TD-90).
Acceptance steps that type a value and check it at once must wait for the
0.7 s pause.

**Loft.**
- `BRepOffsetAPI_ThruSections` puts every side on a B-spline surface, even
  flat ones. Planar sides are rebuilt as planes, and the solid is sewn again
  only if it stays valid with the same volume.
- Inputs are marked `SetMutableInput(false)`, because region faces are shared
  with cached step inputs and previews.
- Seen from afar, a small profile on a construction plane is within a
  finger's reach of the plane's outline all round. `pickAt` now gives such a
  tap to the profile unless the outline is within 6 px.

**iOS files.**
- Xcode's archive leaves out the dSYM of a CMake-generated project, so
  `build-app.sh` copies it in and checks its UUID.
- Qt turns `openURLContexts` into a `QFileOpenEvent`.
- Security-scoped files work only through QFile. Outside files are therefore
  staged into the sandbox before OpenCASCADE or the project reader opens
  them.
- A hidden QML MenuItem kept arrow-key focus until it was also disabled.

**The App Store.** Selling the statically linked app is compatible with the
LGPL and the MPL without a commercial Qt license, provided that:
- the source and build scripts are public at each tag;
- the notices and a written source offer are in the app;
- the pinned library sources are attached to releases;
- every App Store version is a tag.

This is not legal advice (docs/LICENSING.md). Other findings:
- Qt Network is linked through Qt Qml although OpenShape never uses it. The
  privacy texts therefore say the app "makes no connections", not that the
  library is "not linked".
- Home ignored the safe area on a real iPhone.
- A framed Windows window cannot be taller than the desktop, so the
  2868-pixel store screenshot uses a frameless window.

**Integration.**
- GitHub's MSYS2 shells have no git or cmp unless they are installed. The
  iOS sources test failed in CI and in the Release dry run until `git` and
  `diffutils` were installed there.
- Branches added the same helpers independently (touch helpers in the
  acceptance runner, a remembered profile tap in the controller), and their
  technical-debt numbers collided. Each track now gets its own TD range.
- QML's `top` is a FINAL property of Item.
- Eight agent tracks at once used up the owner's usage limit, and they all
  died together. Now at most five run at a time, at high effort for code and
  medium for reviews.

## 2026-10-30 — Integration: three real-UI regressions after the October merges

Headless tests passed after Home cards, kernel robustness, the view cube,
phone sketching and the keypad check were merged; the real-UI run did not.
- **Live values on a phone.** Phone sketching hid a shape's live values
  while they measure 0 and dropped the cursor when a finger lifts, so after
  the first corner's tap there was nothing to tap: the keypad flow ("tap the
  first corner, then a live size", the hint and the help card) was gone on
  iPhone and iPad. Now, once a finger lifts with a shape under way, the live
  values stay (`SketchSession::liftedEnd_`): unmeasured ones show a dash (also
  on the keypad's display), and values typed on the keypad are previewed. No
  "0" values under a finger, and still no cursor where it lifted.
- **The Text tool and AltGr.** Not a key handler: the scenario clicked the
  same spot twice within Qt's 500 ms double-click interval. A double-click
  applies the words and selects the body, and the next Backspace deleted it.
  Whether the clicks were 490 or 550 ms apart depended on how long previews
  took. The scenario now waits past the interval, as others do.
- **The keypad beside the value box (iPad).** At the instant the keypad
  opened, the value chip kept off its old rectangle (where it was last shown).
  When that happened to cover the chip's spot, the chip moved to a corner
  and the keypad was placed far from it. The chip now ignores the keypad
  until it is placed for its client (`placed`).

## 2026-10-30 — Apple Pencil: palm rejection, hover, pen and fingers apart

- Pen and finger shared one drag state: a palm touching down during a pen
  stroke reset it (`pointerPress` starts with `drag_ = {}`), breaking the
  stroke. Palm rejection (`interaction/PalmRejection`, timing only, 0.5 s
  grace) now drops the hand's touches before the gesture recognizer, and
  the controller ignores a finger while the pen drags.
- Qt Quick turns an unhandled pen hover into a hover event of the primary
  pointing device (the mouse): the pen hovered with mouse tolerances.
  Measured by disabling the fix: the `pencil` scenario's "the hover is the
  pen's" check fails. `ViewportItem` now matches the window's last tablet
  hover position.
- Injecting tablet events: the acceptance runner clears
  `QWindowSystemInterfacePrivate::TabletEvent::platformSynthesizesMouse`
  around each injected event, so Qt itself makes the mouse events from the
  pen events no item takes: the path Qt's iOS
  plugin relies on.
- The `numpad` scenario fails from "tap its live width" on (122 checks)
  on main at 329a5bb as well (a build of main's sources, the same first
  failure): not caused by this track.

## 2026-10-31 — `compact,duo`: a measurement that counted the origin

- `duo` failed only after `compact` ("40 x 25" measured 40 x 30). Tracing
  the sketch session showed the product right: width 40 and height 25 both
  typed and locked, the rectangle 40 x 25. The difference: `compact` leaves
  the real mouse resting where the Duo inner display's 951-wide window
  later grows over it, so Qt sends hover events, the free end follows the
  mouse and the rectangle is drawn down from the tapped corner (-10, -5)
  instead of up. The scenario's extent counted every sketch point,
  including the always-present origin (`sketch::kOriginId`, at 0, 0): from
  y -30 to 0. Fixed in the scenario (skip the origin). A user cannot hit it:
  nothing in the sketch was wrong; a resting mouse taking the free end is
  the desktop's normal hover. Lesson: measure a shape by its own points,
  never the sketch's (Numpad's `sketchExtent` still counts the origin; it
  passes today).

## 2026-10-31 — Shell: the retries tested, wrong shapes refused

Shell's retries (split at C0 faces, then intersection joins, adapted from
OpenShape3D) had never run in a test: OCCT 7.9.3 never refused one of our
bodies with `BRepOffset_C0Geometry`. A test hook
(`detail::setShellFirstAttemptRefusedForTesting`) forces them;
`tests/test_shell_retries.cpp` runs 21 bodies (domes with pipes, a pipe at
a rounded corner, a rounded T, a rounded box, a boss box, a bump box)
normally and forced, and asserts.

- **A wrong shape reported as success.** Forced, a dome with a rounded
  pipe came back at 3314 mm^3 where the shell has 1881. Intersection joins
  had built a solid of two shells (the outside left open, the inner skin
  closed apart); `finishSolid`'s repair (ShapeFix_Shape) turned that into
  5 faces of inner skin. Now every retry's result is measured
  (`wallProblem`): the outside must be kept whole, every new face must lie
  `thickness` from the closed faces (5 sample points), and the volume must
  match the band between outside area A and inside area a,
  `thickness * (A + a) / 2`. That band is exact for flat walls and was
  within 0.1% on every right result (0.02% on the bench enclosure). It
  also caught a natural wrong shape: intersection joins on a box with a
  half-buried cylinder whose end chords are rounded keep 5492 mm^3 where
  1 mm walls hold 3939. The old dome body is rebuilt in the test and
  refused ("the outside lost faces"); the forced dome itself is now
  stopped earlier by the shell count below, and shells through the new
  complete-intersection retry.
- **The access violation** ("dome 5 r 1", forced) was not in the offset:
  `BRepCheck_Analyzer`'s solid check classifies one shell against the
  other and crashed in `Extrema_ExtCC` on the same kind of two-shell
  solid. Shells and free edges are now counted before anything classifies
  (`unjoinedWalls`), on every attempt: the boss box opened at its top gets
  a two-shell solid from the *first* attempt, and with the count disabled
  for it, the test process died once (no gdb), so the count runs always
  (0.02 ms). TD-153.
- **Split at C0 had a bug.** A real C0 face (a B-spline profile with a
  full-multiplicity knot, extruded, as STEP files bring them) is refused
  with error 3, so the split runs for real. It built a two-shell solid:
  the open floor, which only borders the split face, was looked up with
  the reshape context's `Value` (still the old face, not in the split
  body), so the offset opened nothing. `Apply` gives the rebuilt face. The
  shelled volume now equals that of the same block built of two C1 faces
  (to 1e-6).
- **Two maker parts shell now.** Arc joins refuse the boss box (error 1 at
  1 mm; 0.79 mm works) and the bump box (error 1 at every thickness). New
  retries before intersection joins: the body with faces of one surface
  merged (`ShapeUpgrade_UnifySameDomain`; the boss box: 4508 mm^3 at
  1 mm), and arc joins in OCCT's "complete intersection" mode (the bump
  box: 3983 mm^3 at 1 mm, 7574 at 2). Tolerance changes (1e-4 to 1e-2) did
  nothing. Rounded box, 12 mm walls: arc joins fail, the complete mode
  gives exactly the plain inner box (16 x 6 x 8 mm).
- **Honest refusals.** Without a size that works, "Try thinner walls" is
  said only when the walls meet for sure (half the narrowest side or
  more); otherwise "Unable to hollow this body with this wall thickness."
  With the size search, a failed smallest size (0.1 mm) now leads to "Try
  opening a different face" even when the search runs out of time before
  trying every half (the bump box opened at its side fails at 0.1 mm).
- **Areas across a C0 knot.** BRepGProp's adaptive rule (eps 1e-2) took
  0.2 ms on an offset fillet where the default rule took 8 ms, but on a C0
  face it was off by 3% (664 mm^2 for 644; 1e-3 and 1e-5 were off too);
  the default rule is used on C0 surfaces (0.05%).

## 2026-11-04 — Measure after build 62: a hinge that reports only changes

- The owner, on build 62: "I'm not sure at what point a measurement is
  held." Reading `SteadyReading` and the hinge together showed more than a
  missing cue: `UIHingeInteraction` calls its handler when the angle
  changes, and a reading counts as steady only once a sample at least 0.8 s
  old exists with everything since within 0.4 degrees. A fold that stops
  moving sends no more samples, so the steady test never ran again: a hold
  came late (with the next twitch of the hinge) or not at all. The motion
  sensors stream at a fixed rate, which is why the Level never showed it,
  and the acceptance run's stand-in pushed 25 readings in a row, which is
  why no test did. Fixed with `MeasureController::tickSensors` (a 100 ms
  timer feeding the last angle again while it keeps still); the scenario
  now pushes one hinge reading and lets time pass. Lesson: an event-driven
  sensor needs time fed in; a stand-in should report the way the device
  does, not the way that is easiest to drive.
- The hold is now shown, not only felt: HELD / LIVE over the number, a pill
  that fills over the 0.8 s (`SteadyReading::State::progress`), a line with
  the rule. **Measure again** used to re-hold at once on a still phone (the
  window kept its old samples); `clearHold` now clears them, so it waits
  the whole second again.
- The card became a Bicycle-size playing card (the owner's choice). Build 63
  kept the ID-1 card as a second choice, on screen as "bank card": the owner
  read that as something else until he asked ("Did you mean like a debit or
  credit card?"), and wants no payment card asked for at all, least of all
  in a photo. The next build has the playing card only; the Photo tool's
  intro had still said "Lay a bank card" (an earlier sweep for the phrase
  missed it). Lessons: name things as people say them (debit or credit card,
  not "bank card"), and grep every file type after a wording change. TD-176
  keeps the tolerance question; a real ruler along the screen checks it.

## 2026-11-04 — Photo after build 63: guides, dimensions that stay, views

- The owner liked Photo best and asked for guides ("see if you are measuring
  in a straight line"), several measurements at once and side views for a
  drawing. Straight means straight on the straightened photo, whose axes are
  the reference's edges, so the angle and the snap need no extra input; it
  holds only when the card lies square to the part, which the hint words as
  "to the playing card's edges".
- The angle is folded to 0..90 (a line sloping down reads 4.6°, not
  175.4°): the first draft measured 0..180 counter-clockwise and a nearly
  straight line read 175°, which says nothing to someone checking an edge.
- Several dimensions without breaking the one-measurement flow (and the
  outline-finding track): Photo keeps `points` and `closed` as the selected
  dimension's, writable as before, and a sync puts them into `dims`
  (writing points with none selected starts a dimension of the current
  mode). The existing `measure` scenario's checks on `points` held unchanged.
- A tap used to be ignored once the finger moved 8 px; now a press, slide
  and lift places the point where the finger lifts, with the magnifier and
  the live angle: on a phone the second end of a distance can be placed by
  sight, not only by aim.
- Placing a view on the Front plane needed no new frame: reference photos
  and sketches share the origin planes' frames (`referencePlaneFrame`), so
  the straightened photo's pixel (x, y) is (x - origin) * mm/px across and
  (origin - y) * mm/px up on any of them, and the outline sketch lies on its
  photo (the scenario reads both back: 0..80 x 0..40 mm on Front).
- The sheets (a dimension, a view, placing views) first centered on the
  window: on the Duo's outer display (an 84 pt side bar at the right) they
  overflowed it; centered in the safe area now. The Measure screen's own
  cards (a kept measurement, the guide) did the same; the lead centered
  them in the safe area too, with a check at every size.
## 2026-11-04 — Measure: FoldForm's own camera screen

- Sam asked for "the level bubble inside our own camera screen"; the system
  camera (UIImagePickerController) cannot draw on its preview, so Take photo
  now opens `CameraScreenIos.mm` (AVFoundation), with the system camera kept
  behind `kOwnCameraScreen`. Written on Windows for the Mac CI: TD-180 lists
  what only the device can confirm.
- Gravity was turned into the window's frame by the Qt view's scene. A
  full-screen presentation takes the presenting view out of the window, so
  over the camera screen its scene is nil and every reading would be taken
  as portrait: the camera screen watches gravity through its own view
  (`measureios::watchGravityIn`).
- The overlay matches the preview only because both are computed, not
  guessed: the preview shows the whole 4:3 frame (resize aspect) and the
  overlay is drawn in `AVMakeRectWithAspectRatioInsideRect` of the same
  size and rotation; the photo is turned by the interface orientation (not
  the device's), so it is upright as the preview shows it even lying flat.
- With a digital zoom, whether EXIF's FocalLenIn35mmFilm counts it is not
  documented; the camera screen keeps whichever of the value and the value
  times the zoom is nearer the lens's field of view at that zoom (Scan's
  focal length), and logs all three.
## 2026-11-04 — Calibrating the Level, and Scan on a legal pad (after build 63)

- Sam: "it's very difficult to test/calibrate level". The end-for-end (flip) method needs no
  level surface: two held readings, the phone turned half a turn in between, offset = half
  the sum, slope = half the difference. Surprise: gravity reaches the controller already
  turned into the window's frame, so an offset kept from portrait would be wrong (rotated)
  in landscape; `GravityReading::turn` now says how the window is turned and the lying
  offset is kept in the device's upright frame. Another: on a level surface turning the
  phone changes nothing, so the hold's "moved away re-arms" rule never fires; the second
  reading needs **Turned** (or a move). And Check reports half the readings' vector sum, not
  the difference of their sizes: on a level surface an offset gives two equal sizes.
- Sam: "Scan an object kind of struggled to find the paper each time ... I used a lined legal
  pad." On a synthetic yellow lined legal pad on wood, the old finder found no sheet at all
  (the yellow's saturation, about 0.47, failed its colourless test); a hue band for pale
  yellow and a 5 x 5 closing over ruled lines find every corner within 1.4 px. Legal is a
  third paper. Vision (on the phone) looks first and takes any aspect ratio from 0.3, so a
  sheet seen side-on from low down falls through to FoldForm's finder (TD-179).
- The base commit did not compile (`cardReference(card)` after the card's parameter went);
  fixed as the lead did. Lesson: build before the commit, even for a one-line wording change.

## 2026-11-04 — Find outline: a part on contrasting paper, traced

- Sam, after build 63: could a part on high-contrast paper, photographed
  beside a playing card, be outlined perfectly and made into a sketch?
  `measure/ObjectOutline` does it on the straightened photo (docs/MEASURE.md,
  "Find outline"); the acceptance run's `measureautooutline` drives it.
- Shadows were the trap, twice. First the edge: placing it where the part's
  share of the colour is one half put it inside a soft shadow (a 35 %
  shadow beside a dark-grey part reads as 0.47 of the way to the part), 0.35
  mm out on the shadowed sides. Halfway between the levels just outside and
  just inside the edge fixed that, until the mask took in some shadow: then
  the "inside" window sat on the edge itself. The edge is now the rise along
  the normal that climbs into the part's own colour (a shadow's edge only
  climbs to the shadow's level), the one nearest the crack, and then the
  halfway point between the levels around that rise. Second the threshold:
  Otsu's threshold on a photo of a large sheet with a soft shadow came out
  at 0.30, under the shadow's 0.35; the table around the paper defeated a
  "half the median of what stands out" rule (the table outnumbers the part),
  so the threshold is now raised to half the chosen part's own median and
  the part picked again.
- The synthetic photos say: within 0.07 mm of the truth seen through the
  same straightening (sub-pixel at 0.23 mm per pixel); the sizes in mm are
  0.3 % large, all of it the card's corners placed 0.3 px off on purpose.
  The reference is the limit, not the tracing: Vision's corners on a real
  photo will be off by more than that (TD-176 for the card's own size).

## 2026-11-13 — Collecting faces on a phone; the view over the top

Sam, after an evening's design work on his Duo with 0.3.1 (the App Store
version): "It's hard to select multiple faces at once, it either locks on to
an edge (and you can't tap it again to unselect it), or it deselects
everything. Navigation while designing is tough. There are locks to where
you can rotate your image." A new lead (this session) read the selection
path first and found five separate causes, none of them a bug in the sense
of code doing what it was not written to do: each rule was reasonable for a
mouse or a large face, and `test_multi_face` (0.3.1's several faces) taps
the middles of large faces in a 1200 x 800 window, where none of them
shows. `tests/test_touch_faces.cpp` is the same story on a phone-sized
window with a part's small faces; all 13 of its tests failed on the old
code.

- **A finger's edge zone covers a small face whole.** Edges win within 20 px
  on touch (a 40 px band). A 10 mm pad on a phone is about 30 px wide, so
  every tap on it took an edge. `sel::PickOptions::narrowFaceShare` (0.25
  for a finger and a pen): the picker walks from the tap straight away from
  the nearest edge until the face ends; an edge only wins within a quarter
  of that width. Wide faces never reach the test's limit (the edge keeps its
  whole 20 px). Cost: up to about 40 face picks a tap, each a BVH query.
- **Hidden edges were pickable.** The occlusion test compares the edge's
  depth with the face under the pointer plus a slack of (tolerance + 2) px
  worth of depth: for a finger several millimetres. The edge where a pad
  meets the plate, hidden under the pad's top, passed, and lies inside the
  top's outline on screen: with the first fix alone, the second pad still
  gave "edges 36". An edge must now be in sight along its own line of sight
  (`kEdgeCoverPx`). This also holds for the mouse, where the slack was
  about 8 px of depth.
- **A different kind replaced the selection.** With faces selected, a tap
  that picked an edge called `selection_.set(edge)`: three faces became one
  edge. `InteractionController::addingPick` keeps an adding tap to the kind
  being collected, and never drops several items for a tap on the model.
  A first version let a single face give way to an edge tapped within 8 px;
  that is exactly Sam's "locks on to an edge" on the second face, so an edge
  never takes over from a face now, and the hint tells the way (empty space
  first). The other direction stays: one selected edge gives way to a face
  tapped in the open, since a tap 20 px from every edge cannot mean an edge.
- **The arrow swallowed the tap.** A finger's grab zone around an arrow is
  28 px. Push/Pull's arrow stands on the selected face and Fillet's starts
  on the selected edge, so the tap meant to unselect landed on the arrow: a
  press on a handle without a drag did nothing (`tapBesideHandle` let it
  through only for bodies). It now also lets through a tap on what is
  selected, and a tap beside the arrow on another face or edge, for the
  five operations a plain selection arms.
- **Two quick taps were a double-tap.** The recognizer pairs taps within
  0.35 s and 32 px, and its comment says why the distance is generous: a
  double-tap's second tap often lands on another face. But two quick taps
  on two small neighbouring faces are the same event, and the double-tap
  replaced the faces with the body. While faces or edges are selected, two
  taps that picked different things are two taps (`PressMemo::subShapes`).
- **The view stopped at the poles.** `Camera::orbit` clamped the pitch to
  +-90 degrees. The pitch now runs on (kept within +-180), `backward()` and
  `up()` need no change (the formulas are those of a rotation, continuous
  through the pole), and three things had to follow: a sideways drag turns
  the other way about the vertical while upside down, taken once where the
  drag starts (`Camera::orbitSense`; Blender's turntable does the same:
  switching at the pole reverses the spin under the finger); the key light
  took the horizontal direction from the yaw alone, which is the far side
  of the model once upside down, so it swings over the top during the first
  30 degrees past the pole (views up to the pole are lit exactly as before:
  `test_lighting` holds the old values); and `Camera::interpolate` takes
  the short way in pitch. What Shapr3D does was checked before choosing: a
  level horizon with one finger, free rotation only as a two-finger option
  (its forum, "One finger navigation, odd behaviour"). A two-finger twist
  to roll the view is not built (TD-181).
- Lesson for the tests: a selection rule checked at desktop size with taps
  in the middles of faces says nothing about a phone. New selection tests
  run at 402 x 874 and at the Duo's sizes, through the gesture recognizer,
  on faces narrower than 40 px.

## 2026-11-13 (later) — Faces facing different ways move together

Sam, the same day: "if I have a square shape cut out of a plane, and I try
to make the hole bigger by selecting all of the faces and expanding them all
outward, I can't do that."

- Why not: 0.3.1's several-faces Push/Pull sweeps each face as a prism along
  one common normal (`geom::pushPullFaces`), so `MultiPushPullOperation::
  create` returned nothing for faces facing different ways, and two walls of
  a hole had no arrow. A long press on a wall made it odder: it took all
  four walls and moved the one facing "the way most of the step's flat area
  faces".
- Prisms cannot do it in any case: four walls each swept back 2 mm leave the
  corners (a plus-shaped hole). `geom::offsetFaces` uses
  `BRepOffset_MakeOffset` with an offset per face and intersection joins, as
  `offsetFace` does for one: faces that share an edge meet again where their
  moved surfaces cross. Exact on the tests: the 10 mm hole 14 mm square, 10
  faces, a closed STL of the right volume.
- The kernel's word is checked three ways (its "done" is not enough, as
  `offsetFace` found): the volume moves the way the distance says and by
  0.2 to 5 times area x distance (the corners add up to a factor of 2 for a
  square doubled), and every flat face must lie in its moved plane.
- **The kernel crashes on a hole closed to nothing.** The four walls pulled
  out 5 mm (a 10 mm hole) is an access violation inside
  `BRepOffset_MakeOffset::MakeMissingWalls` (a solid classifier running an
  extrema on what is left). The crash guard turned it into a failed step in
  one run and the process died in the next: the test passed alone, and
  `OffsetFaces*` together segfaulted. A drag does this in a second, so it
  must not reach the kernel: `offsetRoom` works out how far the faces can go
  before one meets another face of the body (flat faces in the way of the
  motion, not neighbours, their boxes overlapping once there; half the gap
  when both move towards each other; a round face's radius), and
  `offsetFaces` refuses at or past it with the room named ("Try 4.9 mm or
  less."). Conservative and for flat and round faces only; `offsetFace`
  (one face, since September) has no such check yet (TD-182). Lesson:
  the crash guard is a last line, not a plan. When a degenerate input can
  be named, refuse it by geometry first.
- No new step kind and no file format change: `PushPullFacesFeature` already
  stores faces and a distance; it now picks the straight sweep when the
  faces face one way and the per-face offset otherwise, from the faces as
  they are when it is computed. A file with such a step opens in 0.3.1 with
  that step failed (its message: faces must face the same way).
- The long press's rule changed with it: a step's cap is the direction one
  of its flat faces looks that all its other flat faces are square to (a
  top among walls, a pocket's floor), whatever the areas; a step with no
  cap whose faces are flat or round is all walls and moves each its own
  way. The old area rule would also have moved a wall of any pad taller
  than it is wide.
- A finger dragging one of the moving faces drags along that face's own
  normal (`MultiPushPullOperation::directionOn`), not along the arrow's.
  In a hole of a centimeter the arrow's finger zone (28 px) reaches the
  walls, so there a drag that starts on a wall is the arrow's: the real-UI
  scenario drags the arrow, the headless test zooms in for the wall.
- The first whole real-UI run of the day failed 110 checks in two old
  scenarios (`numpad`, `duo`), all one cause: after the check mark the face
  stays selected, and the scenario tapped it "again" to go on. That tap
  used to land in the arrow's zone and do nothing; it now takes the face
  out, as Sam asked. The scenarios tap only when the face is not selected.
  The same run reached ctest's 25-minute limit one scenario short (about
  6,000 checks now): the limit is 45 minutes. A reminder that came with
  it: read a run's failures from its summary, not from the tail of its log.

## 2026-11-13 (noon) — "I was using the Apple Pencil with it"

Sam, on seeing the morning's fixes. The morning's tests were all a finger's,
and the Pencil is not a finger here: its taps come as mouse events with the
pen as their device (9 px for edges, 16 px around an arrow), Qt decides
what a double-tap is, and from its first tap fingers only move the view.

- Most of the morning's rules sit below the device split (`addingPick`,
  `tapBesideHandle`, the picker) and held for the pen unchanged: the new
  `PenFaces` tests for collecting and for unselecting under the arrow passed
  on the morning's commit.
- Two did not. **Two quick taps:** Qt turns the Pencil's second tap into a
  double-click, sometimes in place of its press. The morning's "two taps on
  two faces are two taps" returned early for it, and with no press there
  was no click: the second face was not added, the tap just vanished. The
  same had been true since September of two quick pen taps on two bodies.
  `pointerDoubleClick` now does that tap's click when the double-click took
  its press's place (`secondTapClicks`), and compares the faces under the
  two taps, not the raw picks (beside an edge they differ in kind).
- **A finger in a sketch:** it panned, by a rule made for people who draw
  with a finger (an orbit by accident tilts the view off the plane). With
  the Pencil drawing, the finger only navigates, and that rule left no way
  to turn the view inside a sketch but the view cube: a lock exactly where
  he works. In pen mode one finger now orbits in a sketch as anywhere, and
  Face on comes back. Probably the "locks" of his first message, with the
  stop at the poles.
- The `multifaces` scenario has a Pencil pass with real tablet events at
  the Duo's inner size (Qt's own double-click detection included).
- Lesson, again: ask which input the owner was using before choosing the
  tests. Touch, pen and mouse are three paths here.

## 2026-11-13 (afternoon) — 0.3.2 from the 0.3.1 tag

Sam: "Ship 0.3.2". The day's fixes were written on `polish` (0.4.0, with
Measure); 0.3.2 is `v0.3.1` plus those commits. The code carried over with
one conflict (the test list in tests/CMakeLists.txt, which names Measure's
tests on `polish`): nothing in the fixes leans on Measure. The docs did not
carry: on `polish` they sit among Measure's sections, so this branch has
its own changelog section, manual tests and status. `main` had one local,
never-pushed commit raising its version to 0.4.0 after the 0.3.1 tag; it is
kept as the branch `backup/main-0.4.0-bump` and `main` goes on from the tag.
