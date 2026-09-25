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
