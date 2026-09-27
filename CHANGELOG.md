# Changelog

What changed in each release of OpenShape. The GitHub release notes show
the section of their version (`.github/workflows/release.yml`).

## 0.2.0 (unreleased)

**On iPhone and iPad.** OpenShape runs on iPhone and iPad (TestFlight).
The layout adapts to phones, tablets, Split View and foldables, and the
value box no longer covers what you tapped: on phones it docks at the top
or bottom, away from the selection (and above the on-screen keyboard while
you type); on larger screens it sits beside the arrow, never over the
edge, face or body you are working on. While you draw a sketch with a
finger, the live sizes show beside the finger, not under it.

**Easier to read at every angle.** The view opens in perspective (the
Perspective / Orthographic button remembers your choice), the light stays
fixed to the world (tops are lightest, undersides darkest, and two sides
seen at once are never the same shade), the grid fades out softly instead
of ending at an edge, and bodies cast a soft shadow where they stand on
the ground.

**Construction axes and planes.** Construct → Axis (through a hole or a
shaft, along an edge, through two points, parallel to X, Y or Z) and
Construct → Plane (offset from a face or an origin plane, at an angle
through an edge, midway between two faces). Rotate about them, pattern
around or along them, mirror across them, align onto them and sketch on
them; they follow the faces they were made from. Align can also put a
hole's axis on the Z axis, a face on the XY, XZ or YZ plane, or a corner
on the origin.

**Text.** Raise or cut words into a flat face (Noto Sans, regular or
bold): size, depth and angle can be typed and changed later.

**Holes that fit printed parts.** Clearance holes and counterbore or
countersink seats get a 3D-printing allowance (+0.2 mm to start with,
File → Preferences); tap and heat-set insert sizes stay as they are.

**Copies you can change on their own.** Mirror and Pattern copies made as
separate bodies, and pieces split into bodies, are independent: editing
the original no longer changes them (projects from 0.1.0 keep their
linked copies).

**Smoother on large parts.** Previews are computed in the background and
shown as soon as they are ready, so dragging stays smooth.

Known limitations: the Windows installer is not code-signed yet
(SmartScreen warns); text is one line on a flat face, without kerning;
construction axes and planes follow the step they were made from, not
steps added later; no splines yet.

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
