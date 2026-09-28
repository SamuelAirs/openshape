# Changelog

What changed in each release of OpenShape. The GitHub release notes show
the section of their version (`.github/workflows/release.yml`).

## 0.3.0 (unreleased)

The answers to the second round of testing on iPhone and iPad (sketching
and editing sketches with a finger, cutting pockets, typing sizes,
selecting several bodies), Loft, and the groundwork for the App Store.

**Sketching with a finger.** A pinch or a two-finger pan in a sketch moves
the view and never draws a rectangle, and each stroke with the Line tool
is a line of its own instead of starting from the last point touched.

**Edit a sketch by dragging.** With Select, drag a line to move it (pull a
rectangle's side out and the rectangle grows, its corners staying
square), a circle's rim to resize it or its center to move it, and a
point to move it on its own: dropped on another point, a line, a circle or
a line's middle, it stays joined to it. Tap inside a closed shape, then
drag it to move the whole shape; a double-tap on a curve selects the
whole shape. Select a line or a circle and tap its size to type an exact
length or diameter. Items that can still move are blue, fully sized ones
dark, and each drag is one step to undo.

**Cut a pocket the way you would in Shapr3D.** Select a face, sketch the
shape to cut out, tap inside it and choose Extrude (on a touch screen you
can also drag the shape itself): pulled out of the face it joins the
body, pushed in it cuts. The value box says what you are making
(Height, Cut depth or New body); Cut always goes into the body, Flip turns
the extrusion round, and Through all cuts through the whole body. A
sketch on the ground or on a construction plane pushed into a body cuts
it too. Starting a sketch on a face zooms to that face, so on a phone the
grid and the arrows move in whole millimeters.

**Typing a size.** The model waits until you have typed the whole value:
typing 100 no longer shows it at 1 mm and 10 mm on the way (the preview
follows after a short pause, or at once with Enter, ✓ or Next). On an
iPhone or iPad a value opens OpenShape's own keypad instead of the system
keyboard: digits, `+ − × ÷` and parentheses, mm, cm, in or °, Next and ✓,
with keys large enough for a finger or the Apple Pencil. It also types a
sketch's sizes and the numbers in the Model panel.

**Several bodies on touch.** Double-tap a body, then double-tap the next:
both stay selected, and Union, Subtract and Intersect appear. While bodies
are selected, a single tap on another body adds it and a tap on a selected
one takes it out.

**Loft.** Select closed shapes sketched on different planes, in order (a
rectangle on the ground and a circle on a construction plane above it,
say), then Loft: a solid that passes through them, Smooth or Straight, as
a new body, or joined to or cut from the body one of the shapes was
sketched on. It follows its sketches and planes when they change later,
for example when you type a plane's new distance in the Model panel.

**Share and open on iPhone and iPad.** Export STL, 3MF and STEP open the
share sheet: your slicer app, AirDrop, Save to Files or Mail (the file
also stays in OpenShape's Exports folder). File → Share Project… sends
the project itself. STEP files and projects open in OpenShape from the
Files app and from Mail.

**Licenses and privacy.** File → About OpenShape → Licenses lists every
library in the app with its license and says where to get its source
code, and About links to the privacy policy: OpenShape collects no data
about you. On an iPhone, Home no longer sits under the clock and the
Dynamic Island, and its project cards say *OpenShape (Files app)* instead
of a long path.

Fixed: a finger's small wobble on an arrow no longer moves the body;
Esc or ✕ on an extrusion forgets a New body / Join / Cut chosen for it, so
it does not carry over to the next one.

Known limitations: Apple Pencil handwriting (Scribble) does not work in
value fields (tap the keypad's keys with the Pencil instead); Loft has no
guide curves and matches the shapes' corners itself (a turned square
lofts with a slight twist), and a loft whose shapes are all on the ground
or on construction planes is always a new body (use Union or Subtract
afterwards); the iPhone and iPad app is still in TestFlight testing.

## 0.2.0 (2026-09-27)

The answers to the first round of testing on iPhone and iPad, a clearer
view, construction axes and planes, text, and holes sized for printing.

**The value box stays out of the way.** It no longer covers the edge,
face or body you clicked, the arrow, or where you are moving or turning
the body, whenever there is room: it sits beside the arrow, or in the
nearest free corner of the window when there is no room there. In small
windows (a phone, an iPad in Split View) it docks below the top bar or
above the hint line, on the side away from the selection. It overlaps
them only when there is no free corner left (zoomed in close, or a pattern
or move that spans the window), and in a small window while you drag the
arrow: the box keeps its side until you let go.

**iPhone and iPad (in testing, not available to install yet).** The
universal iPhone and iPad app is being tested through TestFlight on the
developer's own devices; there is no public TestFlight link or App Store
release yet. On a phone the value box moves to the top of the screen
while the on-screen keyboard is up, and while you sketch with a finger or
a pen the live sizes move above it instead of hiding under it.

**Easier to read at every angle.** The view opens in perspective (the
Perspective / Orthographic button remembers your choice), and zooming
with the wheel or a pinch heads for the spot under the pointer, stopping
just short of it. The light always comes from above and turns with you
as you orbit, staying over your left shoulder: tops are lighter than the
sides, undersides darkest, and two sides of a box seen at once get
clearly different shades. The grid fades out softly instead of ending at
an edge, also towards the horizon, and a body resting flat on the grid
casts a soft shadow beneath it.

Fixed: a click at the very center of a face could select the face behind
it; in perspective, hidden edges could show through and some visible
edges could not be clicked.

**Construction axes and planes.** Construct → Axis (through a hole, a
shaft or a circle, along a straight edge, through two corners or circle
centers, or parallel to X, Y or Z through a corner) and Construct → Plane
(offset from a flat face or an origin plane, at an angle through an edge,
midway between two parallel faces). Rotate about an axis, pattern around
or along one, mirror across a plane, sketch on a plane, and align onto
either (click it while the tool waits). Each has a row in the Model
panel: type a new distance or angle there (a sketch on the plane, and
what you extruded from it, moves along), hide it or delete it. When you
change an earlier step in the Model panel (say, make the box taller),
they move with the faces and edges they were made from. Align can also
put a hole's axis on the X, Y or Z axis, a face on the XY, XZ or YZ
plane, or a circle's center or an edge's middle on the origin: click the
X, Y or Z line through the origin, or choose the axis, plane or Origin
among Align's buttons.

**Text.** Select a flat face, then Text (also in the Modify tools), to
raise words from it or cut them into it, in Noto Sans (regular or bold,
built in). Size (the height of the capital letters, in mm), depth and
angle can be typed, and the words, size, depth and angle can be changed
later in the Model panel.

**Holes that fit printed parts.** The screw sizes of the Hole tool,
Counterbore and Countersink now add a 3D-printing allowance to clearance
holes and head seats: +0.2 mm by default, also after updating from 0.1.0
(an M3 close-fit hole is 3.4 mm instead of ISO's 3.2 mm), and the Model
panel says so ("M3 close fit +0.2 mm"). Set it between 0 (the standard
sizes) and 1 mm in File → Preferences to suit your printer. Tap-drill and
heat-set insert sizes stay as they are, diameters you type are used
exactly, and holes already in your projects keep their sizes.

**Copies you can change on their own.** Mirror and Pattern now make
separate bodies by themselves when the copies would touch neither the
original nor each other (a message says so; the Separate bodies button
still decides either way). These copies, and pieces made with Split into
bodies, are independent: editing, moving or deleting the original no
longer changes them, and a copy can be subtracted from its original. A
pattern made as separate bodies cannot change its count or spacing
afterwards; turn Separate bodies off for a pattern you want to edit
later. In projects made with 0.1.0, copies and pieces made back then
still follow their original; new ones are independent.

**Smoother on large parts.** Previews are computed in the background, so
the window, the arrows and the view keep up with your pointer while you
drag; the shape follows as each preview is ready. Applying a change and
undo still take a moment on large parts.

Known limitations: Windows is the only download for now (the iPhone and
iPad app is in private TestFlight testing, and there is no macOS
download); text is one line on a flat face, in Latin, Greek or Cyrillic
letters (Noto Sans only), without kerning (pairs such as AV sit a little
apart), and once applied it cannot be moved or switched between regular
and bold; letters that hang over the face's edge are not flagged;
construction axes and planes stay where they are when you push, pull,
move or align the part after making them, and Rotate, Pattern, Mirror and
Align use an axis or plane where it is when you apply them (only a sketch
on a plane moves with it); the value box can still cover part of a very
large selection or of a fillet's new surface; each separate copy or split
piece adds hidden copies of the sketches, construction planes and tool
bodies it was made from to the Model panel; projects saved with 0.2.0 may not open in 0.1.0 (for
example one with a mirror made as a separate body, or with text); no
splines yet.

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
