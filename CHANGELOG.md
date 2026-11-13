# Changelog

What changed in each release of OpenShape. The GitHub release notes show
the section of their version (`.github/workflows/release.yml`).

## 0.4.0 (unreleased)

Everything in 0.3.2 below, and:

**Measure: the phone as a measuring tool, one tool to measure and design.**
A ruler button in the top bar (and Measure on Home) opens a workspace of
instruments that measure real things, and what they measure goes straight
into the design (docs/MEASURE.md, docs/USER_GUIDE.md "Measuring real
things").

- **Ruler**: a scale at true size along the screen, two jaws for a thing
  laid on the glass, and a ring for round things (Make sketch draws that
  circle). The scale comes from a table of iPhones and iPads; **Check with a
  card** matches it to a poker-size playing card (a Bicycle card, 63.5 x
  88.9 mm: nothing to photograph that you'd rather keep private), and is
  kept for each screen (the iPhone Duo's two displays each get their own). It runs along the window's
  long side, against the screen's edge so a part too long for the phone
  can lie beside it: on the Duo unfolded, 141 mm along the top edge of the
  inner display, its fold marked, with **Open flat** when a length crosses
  the fold while the phone is bent.
- **Fold** (iPhone Duo): the angle between the two halves from the hinge,
  and the bend; held once the phone is still for a second, with a tap you
  feel, so it can be read after measuring over an edge. HELD or LIVE over
  the number, a pill that fills while you keep still, and a line that says
  the rule show when it holds. **Gap**: something pinched between the free
  edges, calibrated with a playing card's side.
- **Level**: a bubble and the slope lying down, an edge's angle standing
  up, and the angle between two surfaces; held like the fold. **Calibrate**
  (end for end: two readings on any steady surface, the second with the
  phone turned half a turn) finds the motion sensors' offset and takes it
  off from then on, lying down and on each edge apart; **Check** takes the
  same two readings and says how far apart they are, so the level can be
  tested without a level surface.
- **Photo**: a playing card or a sheet of A4 or Letter paper (or any
  rectangle) lying flat beside a part straightens the photo, as if seen from
  straight above; then distances, outlines with their sides and area, and
  circles, in millimeters. Make sketch puts the outline or the circle on the
  ground plane at real size. The reference is found automatically on iPhone
  and iPad (Vision); four handles with a magnifier fix its corners.
  Guides on the straightened photo: millimeter rulers along its top and left
  that follow zoom and pan, a grid (10 mm, finer zoomed in), and a
  distance's angle to the card's edges while it is placed and once placed:
  within 1.5° of straight it snaps straight and turns green (Snap off for a
  slanted line). Every distance, outline and circle stays on the photo as a
  dimension (A, B, C...), listed under it, to select, rename, delete, or
  **Keep all** ("Top A"). **Views**: + View keeps a photo and its
  dimensions in a set (Top, Front, Side) and starts the next; **Place
  views** puts each straightened photo at true size on its plane with a
  tapped origin point at the design's origin, and its outlines as sketches
  on that plane.
  **Find outline**: a part lying on paper of a contrasting colour beside the
  card is traced for you as an outline dimension, its edge as straight sides, arcs and circles and
  its holes as inner loops (each point can still be dragged); the readout
  gives its width x height, area, holes and sides, and Make sketch (and Place views) draw it
  at real size, holes included, ready to extrude into the part.
- **FoldForm's own camera** for Take photo: a straight-down level (two
  crosses that meet and turn green within 1°, "0.6° off"), and for Scan the
  camera's angle against the 35–55° band; **Last photo** shows the previous
  photo faintly over the view (keep the card still, move the part, match
  it); 1×, 2× and the telephoto; the flash off. The photo that comes out is
  the same upright JPEG with its focal length (the zoom counted).
- **Camera** (ARKit): distances in the room, an outline on a surface, or a
  box's width, depth and height, to keep, sketch or make into a box.
- **Into any value box**: a ruler button in the value box (and beside a
  sketch dimension) opens Measure for that value; **Use 42.3 mm** types the
  measurement there. **Keep** saves readings as named measurements, listed
  beside the instruments, to use, rename or delete later. **?** beside them
  shows the three ways a measurement reaches the design (into a value box,
  by name, as a shape), with **Add all to the project**.
- **1:1**: the View buttons' Actual size shows the model at the screen's
  true scale: lay a real part on the glass to compare it with its model.
- **Trace** sketch tool (D): draw freehand with the Pencil or a finger and
  the stroke becomes lines, arcs or a circle; at 1:1, trace around a part
  lying on the screen.
- **Scan: FoldForm's own photogrammetry.** Stand an object on a sheet of
  A4, Letter or Legal paper and take 8 to 16 photos walking around it: the paper
  gives each photo's camera (no LiDAR needed) and the scale, the object's
  outline comes from Apple's subject cutout (Vision) or the paper's colour,
  a voxel volume is carved from the silhouettes, and **multi-view stereo**
  (depth maps matched between neighbouring photos, fused inside the carved
  shape) recovers what silhouettes cannot see, such as a pocket, a channel or
  a flat top. The result is a closed mesh, brought into the design as a body
  you can measure and model around. Silhouettes alone overestimate flat and
  hollow shapes; stereo needs some texture on the object (plain, shiny or very
  dark surfaces keep the silhouette shape) (docs/SCAN.md). FoldForm's own
  paper finder (used where Vision finds nothing, and on a computer) copes
  with a yellow legal pad's sheet and its ruled lines.
- **File → Import STL…**: STL meshes (binary or ASCII) as bodies, each closed
  piece one body, for remixing.
- **Reference photos in the design**: a photo placed at true size on the
  ground, Front, Right or a construction plane, under and behind the model
  (bodies hide it), semi-transparent, to model or sketch over; saved in the
  project. From Measure's straightened photo (**Place in design**, exact
  scale) or File → Add Reference Photo… on the desktop; in the Model panel:
  width, opacity, rotation, position, plane, hide, and **Set scale** by two
  points of known distance.
- **Measurements kept in the project, typed by name**: the Model panel's
  Measurements (or **Add to project** on a kept measurement) gives a value a
  name, and any value box takes it: `shelf_depth - 2`, `2 * wall`,
  `corner_angle / 2`. The keypad shows the names as keys. A name is turned
  into its number when typed (no live link yet). A kept measurement in the
  project is marked, and its card says the name to type.
- Everything stays on the device: iOS asks once for the camera; the photo
  library is read through the system picker; FoldForm still collects no
  data (docs/PRIVACY.md).

## 0.3.2 (2026-11-13)

**Selecting with a finger, and a view that turns all the way round.** From
an evening's design work on an iPhone Duo with 0.3.1 ("It's hard to select
multiple faces at once, it either locks on to an edge (and you can't tap it
again to unselect it), or it deselects everything"; "There are locks to
where you can rotate your image").

- **Small faces can be tapped.** A finger takes an edge within 20 px, which
  on a phone covered the whole of a small face from both sides: every tap on
  a pad's top, a letter or a pocket's wall took an edge. On a face narrower
  than that reach the middle half is now the face's and the outer quarters
  the edges'.
- **Edges hidden under a face are no longer picked.** The edge where a pad
  or a raised letter meets the face it stands on lies just behind its top,
  and a tap on the top could take it.
- **Faces stay faces.** Once a face is selected, a tap near an edge, or
  right on it, adds or removes the face under the finger: an edge never
  replaces the faces collected so far. To pick an edge after a face, tap
  empty space first (a hint says so). With edges selected, a tap takes the
  nearest edge within a finger's reach.
- **Several faces or edges are never lost to a stray tap.** A tap just off
  the part, or on an open face while edges are collected, changes nothing
  and says how to go on. Only empty space, or Clear, starts again.
- **A second tap takes a selected face or edge out**, also under its
  arrow: the arrow's finger zone covered a small face whole (and the Fillet
  arrow starts on its edge), and a tap there did nothing. A tap beside the
  arrow on another face adds it; a drag still moves the arrow.
- **Two quick taps on two faces add both.** They counted as a double-tap,
  which selected the whole body and dropped the faces. A double-tap on one
  spot still selects the body.
- **No stop at the top or the bottom view.** Dragging up or down turns the
  view on over the top and down the other side, upside down, and round
  again; the horizon stays level, a sideways drag carries the model with
  the finger whichever way up it is, and the light follows without a jump.
  Iso or the view cube puts the view the right way up.
- With the mouse: plain clicks select as before. `Shift`+click with several
  faces selected adds the face under the pointer where it used to replace
  them with an edge.

**With the Apple Pencil** ("on the iPhone Duo, I was using the Apple
Pencil with it"). The Pencil reaches FoldForm by another path than a
finger (finer tolerances, the system's own double-tap, fingers only moving
the view while it is in use), and everything above holds for it: it
collects small faces, never switches to an edge, unselects with a second
tap under the arrow, and keeps several faces through a near miss. Two
things were the Pencil's alone:

- **Two quick Pencil taps on two faces add both.** The second tap arrived
  as a double-tap; once that no longer selected the whole body, it did
  nothing at all.
- **One finger turns the view inside a sketch while the Pencil is in use.**
  A finger's drag in a sketch pans, because without a pen the finger draws
  there; with the Pencil drawing, that left no way to turn the view in a
  sketch but the view cube. **Face on** looks straight at the sketch again.

**Several faces that face different ways move together** ("if I have a
square shape cut out of a plane, and I try to make the hole bigger by
selecting all of the faces and expanding them all outward, I can't do
that"). Faces facing different ways had no arrow at all. They now get one
Push/Pull arrow, on the face selected last: drag it, drag any of the faces,
or type a distance, and every face moves by it along its own direction and
meets its neighbours again at the corners. The four walls of a square hole
pushed back 2 mm make it 4 mm larger each way, corners square; two sides of
a block pulled out grow it on both. Holding a finger on a wall of a hole
that was cut through selects all its walls for this (a step that has a top
or a floor still moves that, also when its walls are larger than it). The
step is a Push/Pull in the Model panel, its distance editable. A distance
at which the faces would run into the rest of the part (a hole's walls
meeting in its middle, a wall pushed out of the part's side) is refused
with the room there is: "These faces would meet another face of the part
before that. Try 4.9 mm or less."

## 0.3.1 (2026-11-05)

The answers to the first session on the iPhone Duo.

**Several faces at once, and pushing them through.** A tap on another
face of the same body adds it, and Push/Pull stays Push/Pull: one arrow (or
a finger on any of the faces) moves faces that face the same way
together. A tap on a selected face takes it out; tap empty space, or Clear,
to start again; the selection says how many faces it holds. Holding a
finger on a face for half a second selects every face of the step that
made it: all of a text, or an extrusion's top and walls. Pushed in, the
faces cut: push raised text down past the face it stands on and it is
engraved, and faces pushed into another body cut their shape out of it
too, shown in the preview and undone as one step. Faces facing different
ways get no arrow (Shell and Delete faces are offered instead), and Shell,
chosen from the bar, adds a face to open with each tap as before. On an
iPad keyboard, Cmd-click adds a face.

**Zooming and panning with a face selected no longer jump.** On a larger
touch screen, each step of a pinch moved the value box, and the view moved
back to make room for the selection, in the middle of the pinch. The view
now stays the user's while they move it. A pinch zooms steadily about the
fingers, also as they drift over nearer and farther faces.

**The value box on the iPhone Duo's inner display** docks at the top or
bottom edge, away from the selection, instead of floating beside the arrow
over the part, and the view no longer moves when it appears (unless the
part fills the screen and no spot keeps clear of the selection).

**The free version's one project** is counted in folders too: a project
moved into a folder in the Files app still counts.

**FoldForm fills the screen** on the iPhone Duo (and every iPhone and
iPad): the model runs under the status bar, the home indicator and the
Duo's side bar, with the controls kept clear of them. Before, the window
stopped short of them, leaving black bars at the Duo's right and bottom
edges (above and below the app on other iPhones).

**File → Open on an iPhone or iPad shows Home**, which lists every project
in FoldForm's folder, also those in folders made in the Files app (their
cards say which folder), newest first. On the iPhone Duo, File → Open's
system file picker closed the app. Home's **Open…** still finds a project
elsewhere (iCloud Drive) with that picker, except on the Duo (below).

**On the iPhone Duo, Open… and Import STEP… explain the Files app's
Share** instead of opening the system file picker, which shows no files
there (Apple's picker, in every app: Dualtop found the same). In the Files
app, touch and hold the file, tap Share, then FoldForm; **Open Files**
switches there. A project opens and is kept in FoldForm's folder; a STEP
file opens as a new project.

**Five sample projects on Home**: a lamp shade (a fully sized wall
revolved, with 16 windows patterned around it in one sketch), a twisted vase (a loft
through four turned hexagons), an electronics box with standoffs, M3
countersunk holes, vents, a USB-C opening and its lid, a phone stand (a
fully sized side sketch, rounded, mirrored, with the name engraved) and a
honeycomb pen holder (seven cells from sketch patterns, all pockets in one
cut). Each opens as an unsaved copy with every step in the Model panel, to
change and learn from; the sample itself stays as it is. Samples do not
count toward the free version's one project; saving a changed one keeps it
as a project of your own.

**Opening a project no longer looks frozen.** A project is read and
rebuilt in the background while a card shows its preview, its name and a
moving bar; the rest of the app waits for it. The lamp shade sample, which
took 17 seconds to open on a PC (its windows were cut out of a shelled
wall), is built so that it opens in about 2. A sample's unsaved copy that
comes back after FoldForm closed keeps its name (Save offers it).

**A more finished look.** Menus are white cards with a soft shadow, an
icon on every row and the keyboard shortcut at the right (none on touch);
Delete… is red. Each tool has its own icon (on a phone the sketch tools
show the icon alone, so more of them fit beside Select and Trim), Undo,
Redo and Help are icons, and the panels and Home's cards float above the
model on a soft shadow.

## 0.3.0 (2026-10-31)

OpenShape is now called FoldForm (the source code and repository keep the name openshape).

The answers to the second round of testing on iPhone and iPad (sketching
and editing sketches with a finger, cutting pockets, typing sizes,
selecting several bodies), Loft, and the groundwork for the App Store.

**Free for one project; unlimited projects with a one-time purchase
(iPhone and iPad).** FoldForm from the App Store is free with every tool,
for one project. Adding a second one (New project, Import STEP, Duplicate,
saving under a new name, a file opened from another app) offers
"Unlimited projects", a one-time in-app purchase for your Apple Account;
once bought, what you were doing goes ahead. Restore purchase is in the
same sheet and in File → About. Opening, editing, saving, renaming,
deleting, exporting and sharing are never limited, and anyone who already
has several projects keeps all of them. The Windows version stays free
with no limit.

**Sketching with a finger.** A pinch or a two-finger pan in a sketch moves
the view and never draws a rectangle, and each stroke with the Line tool
is a line of its own instead of starting from the last point touched.
With a finger or the Pencil, a tap on a line, circle or arc selects it even
while a drawing tool is on (drag from it to draw), and Select and Trim head
the tool list, so they stay in sight on the iPhone Duo's inner display.

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

**Made for the iPhone Duo.** The app fills the Duo's inner display (it is
built with the iOS 27.1 SDK now) and lays itself out for the outer
display, the inner display either way round and Split View. Folding or
unfolding keeps the tool, the values typed, the selection and an open
Model panel, and brings the selection back into view.

**Apple Pencil.** The hand holding the Pencil no longer moves the view or
breaks a stroke: touches that begin while the Pencil draws, or just after,
are ignored. On an iPad the Pencil's hover highlights what it would pick,
as a mouse does, and in a sketch shows where the next point lands. In pen
mode a finger tap says that fingers only move the view, and the
magnifier shows only under a finger.

**Sketching on a phone, and Home.** Sketching on a face snaps to its
corners, edge middles, center and holes; a magnifier shows under your
finger; one finger pans the sketch. On Home, each project card can be
renamed, duplicated and deleted. A view cube shows how the view is turned:
tap a face or corner to look from there, drag it to turn the view. The
keypad takes feet and inches and checks units, and a dimension confirmed
without a change keeps its exact value.

Fixed: a finger's small wobble on an arrow no longer moves the body;
Esc or ✕ on an extrusion forgets a New body / Join / Cut chosen for it, so
it does not carry over to the next one. Shell hollows bodies it used to
refuse (a box with a round boss half outside it, a box with a rounded bump
on top, faces with creases as STEP files bring them), checks the walls of
what it builds instead of trusting the kernel, and no longer tells you to
try thinner walls when no thickness would work.

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
