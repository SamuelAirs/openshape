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
