# Changelog

What changed in each release of OpenShape. The GitHub release notes show
the section of their version (`.github/workflows/release.yml`).

## 0.1.0 (2026-09-26)

The first public pre-release: direct, precise solid modeling for makers
and 3D printing, for Windows 10 and 11 (64-bit).

**Model by pushing and pulling.** Add a box, click a face and drag its
arrow or type the size you want (`35`, or `+5` to add 5 mm). Round and
bevel edges, shell a body, offset faces (a hole takes its new diameter),
delete holes, fillets and bosses, move, rotate (also about an edge or a
hole), align one body onto another, mirror and pattern (joined or as
separate bodies), duplicate, split separate pieces into bodies, and
combine bodies (union, subtract, intersect).

**Sketch precisely.** Lines, rectangles (also from the center), circles,
3-point and tangent arcs, polygons (sized across flats), slots; trim,
corner fillets, offsets, mirror and pattern in the sketch; constraints
with on-canvas glyphs; typed dimensions, including angles. Extrude
(new body, join or cut; symmetric, up to a face, through all, with draft)
or revolve closed shapes.

**Made for printing.** Heat-set insert pilot holes (M2–M5), counterbores
and countersinks for M2–M6 screws, a Hole tool with ISO clearance and tap
sizes, measuring (distances, wall thickness, angles), and export to STL,
3MF (closed, welded meshes in millimeters) and STEP.

**Bring parts in, find them again.** Import STEP files from other CAD
programs (each solid becomes a body you can push, pull, round and combine;
inches and meters come in at the right size). A Home screen lists recent
projects with previews saved inside each project.

**Change your mind later.** Every step stays editable in the Model panel;
undo and redo everything.

**Work is not lost.** Unsaved work is kept as recovery copies and offered
back after a crash; clear messages say what went wrong and which size
would work.

Known limitations: Windows only for now (the universal iPhone and iPad
app builds and adapts to phone, tablet and Split View windows, but waits
for TestFlight); the installer is not code-signed yet (SmartScreen warns);
no text or splines yet.
