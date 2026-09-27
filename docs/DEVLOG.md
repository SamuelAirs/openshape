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
