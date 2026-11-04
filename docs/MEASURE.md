# Measure: the phone as a measuring tool (FoldForm 0.4)

Sam's ask (2026-11-03): "add tools to use the iPhone as a real life measurement tool. So
rulers, photogrammetry, angles using the fold, anything you can think of. It should be one
tool to measure and design. Be ambitious."

FoldForm's answer is **Measure**: one workspace, opened from the top bar (a ruler icon) or from
any value box, with five instruments that measure real things, and a list of what was measured
that feeds straight into the design. Everything runs on the device; no photo or reading leaves
it.

## The idea in one paragraph

Every value box (a push/pull's height, a fillet's radius, a rotation's angle, a sketch line's
length) gets a small ruler button. Tap it, measure the real thing (the shelf's depth with the
camera, the corner's angle with the fold, the coin's diameter on the screen) and the number lands
in the box. Measurements made on their own are kept in a list (named, newest first) and can be
typed into any box later. And measuring can *make* geometry: an outline tapped on a straightened
photo or placed with the camera becomes a sketch at real size; a box measured in AR becomes a
body; a part laid on the screen and traced with the Pencil at 1:1 becomes a profile to extrude.

## Instruments

| Instrument | Hardware | Measures | Makes |
|---|---|---|---|
| **Ruler** | the screen (any device) | lengths and diameters of things laid on the glass | a value, a circle sketch |
| **Fold** | the iPhone Duo's hinge (`UIHingeInteraction`, iOS 27.1) | angles between two faces; a gap pinched between the free edges (caliper) | a value |
| **Level** | motion sensors (CoreMotion gravity) | slope, an edge's angle from level, the angle between two resting positions | a value |
| **Photo** | the camera or the photo library; Vision finds the reference | lengths, outlines, areas and circles on any flat surface in a photo with a card or a sheet of paper on it | a value, an outline sketch, a circle sketch |
| **Camera (AR)** | ARKit (LiDAR where there is one) | distances in the room, an outline on a surface, a box's width, depth and height | a value, an outline sketch, a box body |

Plus, in the design itself:

- **1:1 (Actual size)** in the View buttons: the orthographic view at the screen's true scale,
  so a part laid on the glass can be compared with its model, or traced.
- **Trace** in the sketch tools: draw freehand (Pencil or finger); the stroke becomes lines,
  arcs or a circle within a tolerance (`measure/StrokeFit`). At 1:1, trace around a real part
  lying on the screen.

### Ruler

- A scale along the screen's long edge at true size (mm with cm numbers, or inches in 1/16
  when the display unit is in), two jaws to drag to an object's ends, and the distance between
  them. A **Circle** gauge: a ring dragged to fit a round thing (a coin, a washer, a cap) gives
  its diameter.
- **The window's long side** (Sam asked for it on 2026-11-04, after build 62: the ruler on
  the long side when the Duo is opened): across a wide window (the Duo unfolded, an iPad or
  the desktop held wide) the scale spans the whole width, and the readout and the kept measurements sit in a row below it; down a
  tall window with room beside it (the Duo unfolded and turned, an iPad held tall) it spans the
  whole height, and they and the Ruler's card sit in a column at its right. On the open Duo the
  scale grows from about 85 mm (down the short side, the readout's column beside it) to 141 mm
  (113 mm turned). A phone (and the Duo's outer display) keeps the stacked layout, with the scale
  along its longer free side.
- **At the screen's edge** (Sam, after build 63: "the ruler should be as close to the bevel as
  possible. This will help measure things that don't fit on the phone"): the scale lies against
  the safe area's edge (`MeasureRuler.atEdge`): the top edge of a wide window (the open Duo's top
  has no system bar: the scale runs from its left edge to the system bar on the right, 867 pt),
  the left edge of a tall one (the phone, the Duo turned), with the ticks starting at that edge,
  the numbers inside them and the jaws' lines reaching the edge, so a part lying beside the phone
  lines up with the ticks. Across a wide window the header (the tabs, Close) goes to the bottom,
  under the readout. The Duo's outer display (466 pt wide) keeps its scale across, edge to edge
  below the header: it is too narrow for the readout beside a scale down its long side. Zero is
  14 pt in from the edge; a display's rounded corner may clip the top of its tick.
- **The fold**: on the Duo's inner display, whole (`MeasureController.foldCrease`: the window is
  951 x 669 points, not a Split View half), the scale crosses the hinge at the middle of the
  long side. The crease is marked on the scale ("Fold"); while the hinge reports the halves
  below 170 degrees, a length (or a ring) across it turns the distance amber, the tag says
  **Open flat**, and the readout says "fold not flat". Within 10 degrees of flat a
  100 mm span reads under 0.4 % short, so no warning there; a length on one half never warns.
- **Scale**: iOS doesn't say how big its screens are, so FoldForm knows the iPhones' and iPads'
  pixel densities (`measure/ScreenScale`: 460 ppi 3x iPhones, 458 for the Duo's displays (an
  estimate from the diagonals), 264 iPads, 326 iPad mini) and offers **Check with a card**: a
  card's outline drawn at the current scale; lay a real card on it and stretch the outline
  until they match. The calibration is kept per screen (`screenKey`: "951x669@3"; the Duo's
  two displays get one each). Until then the readout says the scale is estimated.
- **The card** (Sam, 2026-11-04: measurements "built around a bicycle playing card instead of
  a business card"): a poker-size playing card, 63.5 x 88.9 mm (2.5 x 3.5 in,
  `measure::kPlayingCard`), is the only card: the scale check, the Fold's caliper and the
  photo's **Card** reference. Until build 63 it was a debit or credit card (ISO/IEC 7810 ID-1,
  85.60 x 53.98 mm, called a "bank card" on screen, which confused); build 63 still offered it
  as a second choice in the card check, and the Photo tool asked for one in the picture. Sam:
  "having people take pictures of their debit card is an excellent way to lose trust lol let's
  go with the playing card". So the app never asks for a payment card, on screen or in a photo.
  The cost: ID-1 cards are cut to an ISO tolerance of about a tenth of a millimetre; playing
  cards have no published standard (makers cut 2.5 x 3.5 in, cards of one maker are alike, and
  the sizes quoted differ by up to half a millimetre, 63 x 88 to 64 x 89 mm, about 0.6 %,
  TD-176). Sam's check (docs/MANUAL_TESTS.md, 14): after the card check, a real ruler laid along
  the screen's; if the marks part at 100 mm, `kPlayingCard` takes the measured size.
  Calibrations already kept stay valid (a scale is a scale, whatever card found it).

### Fold (iPhone Duo)

- UIKit's `UIHinge.angle` (radians, 0 closed, pi flat) read through a `UIHingeInteraction` on
  the app's view. Shown: the angle between the halves and the bend (180 minus it), with a side
  view of the phone at that angle.
- **Inside a corner** (the backs on the faces) or **over an edge** (the screens on the faces:
  the reading can't be seen while measuring), so readings are smoothed and **held when steady**
  (`measure/SteadyReading`: still within 0.4 degrees for 0.8 s), with a tap felt in the hand.
- **The hold, shown** (Sam, after build 62: "I'm not sure at what point a measurement is
  held"): a tag over the number says **HELD** (green) or **LIVE**; the pill under it fills
  while the phone keeps still (`foldProgress`, 0 to 1 over the 0.8 s) and turns green, "Held ·
  92.5°", with the tap; a line under it says the rule ("Rest the halves on the part and keep
  still: the angle is held after a second, with a tap" / "Held: lift the phone and read it.
  Keep still at a new angle to measure again"). Moved more than 1.2 degrees from a hold, the
  next still second replaces it (the pill fills again; "Now 120.3°" under the number shows the
  live angle meanwhile). **Measure again** drops the hold and waits the whole second again,
  even with the phone lying still.
- **A still hinge**: `UIHingeInteraction` reports only changes, so a fold that stops moving
  sent nothing more and the 0.8 s never passed: build 62 could hold late or not at all.
  `MeasureController::tickSensors` (a 100 ms timer while the sensors run) feeds the last angle
  again as time passes; the acceptance run drives it with its own clock.
- **Gap** (caliper): something pinched between the two free edges is 2 L sin(angle / 2) wide,
  L being a half's length from the hinge to its edge (estimated from the screen, then
  calibrated by pinching a card's side). The readout shows how far one degree moves it.
- Apple says the angle's rate and precision are system policy: the log records what the Duo
  gives (TD-166).

### Level

- Lying down: a bubble and the surface's slope. Standing on an edge: the edge's angle from
  level. **Keep** stores the current position; then the angle between it and the next resting
  position is the angle between the two surfaces. Held when steady, like the fold, and shown
  the same way (the HELD / LIVE tag, the filling pill, the line, "Now" beside a hold). Gravity
  streams at a steady rate, so the level needs no tick. A hand that keeps the phone within 0.4
  degrees for 0.8 s after a hold would replace it; what the Duo's sensors give in a hand is
  for Sam's log to say (TD-166).

### Photo

1. **Take** or **choose** a photo (the desktop: open an image file) with a reference lying flat
   on the surface: a playing card, A4 or Letter paper, or any rectangle whose sides are typed.
2. **Corners**: Vision finds the reference where it can (`VNDetectRectanglesRequest`); four
   handles (with a magnifier while dragged) fix it.
3. **Straighten**: `measure/PlaneRectifier` computes the photo-to-surface map and
   `warpToView` draws the photo as seen from straight above, in millimeters.
4. **Measure** on it: **Distance** (two taps), **Outline** (taps around a shape, closed on the
   first point: each side, the area), **Circle** (three taps on its rim: the diameter). **Make
   sketch** puts the outline or the circle on the ground plane at real size.
- Only points on the reference's surface measure true; a reference's error scales everything
  (a card gives about 0.5 % from the corners' placement, a sheet of paper better).

Sam, after build 63: "Photo is really good and my favorite so far, but you should be able to
measure and see if you are measuring in a straight line or not ... Maybe a ruler or guides to
help measure the object, not just the card. Additionally, building multiple measurements in one
bunch would help so we can get side views and what not in order to help construct a drawing."
So the measuring step has guides, keeps every dimension, and a photo can be one view of a set:

- **Rulers**: millimetre scales along the straightened view's top and left (`PhotoRuler` in
  `MeasurePhoto.qml`), drawn from the view's own scale (`pxPerMm` = the view's fit / mm per
  pixel), so they follow zoom and pan. Zero is the reference's first corner, or the view's
  origin point once one is tapped; right and up count positive, as on the design's planes.
  Numbers every 5 to 1000 mm, whichever leaves 46 px between them; ticks no closer than 6 px.
- **Grid** (a toggle under the photo): 10 mm lines from the same zero, 1 or 5 mm lines between
  them once those are 8 px apart (zoomed in), 50 and 100 mm when the photo is small on screen.
  Drawn over the photo only.
- **Straight or not**: a distance shows its angle to the reference's edges (0 across, 90 up and
  down, whichever way it slopes) while its second end is placed (a finger can press, slide and
  lift: the magnifier and the angle follow it; the mouse shows it while hovering) and on its
  label once placed. Within 1.5 degrees of 0 or 90 the second end **snaps straight** (it moves
  onto the line through the first end: the length along it is kept); the line turns green, the
  tag says "Straight · 0°" while placing and the label "✓ straight". Dashed guides through the
  first end (across and down the photo) show while the second is placed. **Snap** (under the
  photo, on by default) turns snapping off for a deliberately slanted line near straight; the
  hint while placing says so. A dragged end snaps the same way.
- **Several dimensions**: every distance, circle and outline stays on the photo as a dimension
  lettered A, B, C... with its value (a distance's angle too); they are listed under the photo
  (the hint's place once there is one). A tap on a label or in the list selects one (the
  readout shows it, Use takes it when Measure was opened for a value box, Keep keeps it); a tap
  on the selected one opens it: rename (kept as "Top width"), Keep, Delete. **Keep all** keeps
  every finished one as a kept measurement named after the view and its letter or name ("Top
  A", "Top Width"): a distance's length, a circle's diameter, an outline's area. Undo takes
  back the last point (of the selected one, else the last dimension); Clear takes them all.
  A tap on any dimension's point measures from exactly that point; a drag moves it. Back to
  corners and a new photo clear them (the straightened pixels change).
- **Views** (a set of photos of one part: top, front, side): the strip beside the modes shows
  the set and the photo being measured; **+ View** keeps the straightened photo (its photo,
  corners, reference, straightened view, dimensions and origin point) in the set under its
  name and starts the next photo (Top, then Front, then Side, then "View 4"). A tap on another
  view measures it again (the one being measured is kept first); a tap on the current one
  opens it: its name (Top, Front, Side or typed) and plane (Ground, Front, Right: Top goes on
  the ground, Front on the Front plane, Side on the Right plane), Clear origin, Back to corners
  (on a phone it lives there), Remove view. The set lasts for the app session
  (`MeasureController::photoViews`, TD-177).
- **Origin** (under the photo): the next tap marks the point that goes to the design's origin
  (e.g. the part's bottom-left corner); a red cross shows it, the rulers count from it.
- **Place views** (the readout's action once there is a set; **Place in design** for a single
  photo, which now also honours the origin point and the view's plane): a sheet lists the
  views, each with its plane and what goes to the origin; **Photos and N outlines as
  sketches** or **Photos only**. Each straightened photo becomes a reference photo at true size
  on its plane (`AppController::addReferenceImage` with the photo's center in the plane's
  frame), placed so its origin point sits at the plane's origin, without one the middle of its
  closed outlines, else the photo's middle; each closed outline becomes a sketch on that plane
  (`createOutlineSketch` with the plane: the frames reference photos use, so the sketch lies on
  the photo). The design then shows them from the iso view, framed. One undo step per photo
  and per sketch (the undo stack has no grouping).
**The camera screen** (Sam: "the level bubble inside our own camera screen would actually be
elite"). Take photo opens FoldForm's own camera (`src/ui/ios/CameraScreenIos.mm`, AVFoundation)
instead of the system's:

- **A straight-down level**: a white cross marks the frame's middle, a yellow one the spot
  straight below the lens (from CoreMotion's gravity: the angle between the camera's axis and
  straight down, 8 points per degree toward gravity's part in the screen's plane). They meet
  and turn green, with a tap felt, within 1 degree ("0.6° off"). The line under it says why:
  "Hold it flat above the card". A photo taken at a tilt and from close up measures parts that
  stand above the card less truly.
- **Scan** (Take photos): the same screen shows the angle from straight down against Scan's
  band, 35 to 55 degrees in green, and 70 to 80 degrees ("a low photo", docs/SCAN.md) fainter.
- **Last photo** (Sam's idea: keep the card still, move the object, match the angle and the
  height): a toggle, offered from the second photo, draws the previous photo taken here at 40 %
  over the preview. The preview shows the whole 4:3 frame (`AVLayerVideoGravityResizeAspect`),
  and the overlay is drawn in the same rectangle, computed from the photo's size and the
  rotation (`AVMakeRectWithAspectRatioInsideRect`), so the two match. The toggle is remembered
  while the app runs. MeasureController keeps the camera's last photo file for it
  (`CameraOptions::overlayFile`) until a newer one replaces it or the devices change.
- **1x and 2x** (digital zoom on the wide lens, or the telephoto where it is 2x) and the
  telephoto at its own factor (3x, 5x) where there is one: zooming in from further away cuts
  the perspective error. A shutter, Cancel, the flash off. Portrait and landscape (the buttons
  along the bottom of a tall screen, down the side of a wide one), inside the safe area.
- **What comes out is unchanged**: the photo is turned as the preview is shown (the screen's
  interface orientation sets the connection's `videoRotationAngle`, iOS 17), drawn upright at
  most 4096 px and written as a JPEG with its 35 mm focal length, as before. With a digital zoom
  EXIF's FocalLenIn35mmFilm may or may not count it: of the value and the value times the zoom,
  the one nearer the lens's field of view at that zoom is kept (Scan uses it).
- The permission is asked as before; refused, the same message. The log has a line per session
  (`Measure: camera: showing ...`, `session running ...`, `zoom 2.0x on the wide lens`) and one
  per photo: `Measure: camera: captured at 1.2° from straight down, 2× (photo), 35 mm focal
  length 52 (EXIF ..., digital zoom ...)`.
- The system camera is one switch away (`kOwnCameraScreen` in `PhotoPickerIos.mm`), should the
  own screen misbehave on a device. It is checked only on a device (TD-180); the acceptance
  scenario `measurecamerascreen` checks what the Qt side asks it for.

#### Find outline

Sam, after build 63: "If we could place the object on high contrast paper then photograph it
next to a playing card or a ruler, could we then use tools to perfectly outline it and then
generate a sketch in the modeling software from the figures?" Yes: in **Outline**, **Find
outline** finds the part in the straightened photo and makes it one closed outline dimension
(lettered like the others) whose points are its edge,
fitted with lines, arcs and circles; holes (a washer's, a bracket's) are found too. The points
can still be dragged; the readout gives the width x height (along the part's own longest side),
the area (holes taken out), each hole's diameter and each side (a rounded side's radius); **Make
sketch** makes a sketch of those lines, arcs and circles, holes as inner loops, on the ground
plane at true size with the longest side along X: it extrudes into the part with its holes, and
it is one undo step.

- **Best result** (a line on the photo says it until there is an outline): the part on plain
  paper of a contrasting colour (a dark part on white paper, a light one on dark paper), the
  card beside it on the same paper, the photo from straight above, soft light. Leave a margin of
  paper around the part and a gap between it and the card.
- **How** (`measure/ObjectOutline`, Qt-free; on the photo worker thread behind a "Finding the
  outline" overlay; `MeasureController::findOutline`): the straightened photo (its transparent
  corners left out) and its mm per pixel; the card's rectangle with a 2.5 mm margin is left out
  (a sheet of A4 or Letter as the reference is instead where to look: the part lies on it).
  1. The paper's colour: the most common colour in a ring around the card (it lies on the
     paper), then the photo's other common colours if that finds nothing. Fitted as a plane per
     channel over the photo (the light falls off across a sheet), twice, each time from pixels
     near the last fit (not the part, not its shadow).
  2. Each pixel's difference from the paper there: how much darker (shading: 0 for the paper, 1
     for black), how much lighter (1 - 1/s), and twice the change of colour that shading can't
     explain. Smoothed once, then Otsu's threshold (at least 0.18).
  3. Morphology: a 3 x 3 opening drops specks, a closing of about 0.25 mm joins small gaps.
     Pieces (8-connected): the largest that doesn't touch the photo's edge (or the sheet's, a
     sheet as the reference) and is at least 25 mm² is the part (a point can pick one instead:
     `findOutline(x, y)`, not yet offered on screen). Holes under 3 mm² are filled (glints,
     dust).
  4. The edge: the part's pixel cracks as closed loops (the outer edge one way, holes the other),
     each point moved along its normal to where the part's share of the colour (projected on
     the line from the paper's colour to the part's median colour) is one half: sub-pixel.
  5. Fitting (`fitClosedEdge`, tolerance 0.2 mm): one circle if it fits; else Douglas-Peucker,
     then arcs over runs of its vertices that one circle fits (`fitCircle` from StrokeFit),
     lines elsewhere; each refitted by least squares away from its blurred ends; arcs tighter
     than three tolerances between lines become corners, neighbours that one piece fits merge,
     pieces under two tolerances go; neighbouring lines meet at their intersection (sharp
     corners), a line meets an arc where it touches it. The fitting error is the farthest edge
     point from the pieces.
- **Shadows** are the usual trap. A soft shadow only darkens the paper; the difference counts
  darkening for itself, so a shadow a third darker counts a third as much as black would, and
  Otsu's threshold sits above it; the edge is placed half way from the local, shadowed paper to
  the part. A hard shadow darker than about half the paper's brightness beside a dark part joins
  it (the outline grows by the shadow): light from above or diffuse light (daylight, a ceiling
  light, several lamps), not one low lamp; or a light part on dark paper, where a shadow hardly
  shows. A change of colour (a red part on white) counts twice, so coloured parts stand out even
  when their brightness is close to the paper's.
- **As points to drag** (`measure/OutlineProfile`): a loop of points where a point marked
  `through` is an arc's middle (three points fix an arc, so a dragged point keeps a clean arc;
  drawn smaller), the others corners joined by lines; a circle is two half arcs. The report,
  the drawn path and the sketch are made from those points, so a drag shows in all three. Undo,
  Clear or a new tapped outline drop the found outline (the points stay a plain outline).
- **Among the dimensions**: the found data (which points are arc middles, the holes) stays
  with that dimension, so selecting another and coming back keeps it, Find outline again
  replaces it while it is selected, and the set of views keeps it: Keep all keeps its area,
  and **Place views** with sketches makes it a sketch with its arcs and holes on the view's
  plane (`createProfileSketch` on that plane). Opening another view clears the controller's
  last result (`foundOutline`): it belongs to the photo it was found in.
- **The sketch** (`AppController::createProfileSketch`, then
  `InteractionController::createProfileSketch`, one `CreateSketchCommand`): lines and arcs
  sharing their end points, a loop of arcs on one circle as a circle with its diameter set;
  opened facing it with Select, as Make sketch for a tapped outline.
- **Measured** on synthetic photos (a drawn scene through a perspective map, then straightened
  as the app does; a soft shadow, light falling off by 12 %, 0.7 px blur, noise; the card's
  corners 0.3 px off), about 0.23 mm per pixel (`tests/test_measure.cpp`, FindOutline): an L bracket's
  corners within 0.07 mm of the truth seen through the same straightening, its sides within
  0.25 mm of their true size (all but 0.05 mm of it the card's corners, 0.3 % large), its 10
  mm hole 9.99 mm; a washer 50.09 / 20.05 mm on dark paper; a rounded rectangle at 20 degrees
  to the card within 0.11 mm of the truth seen, its 8 mm corners 8.0 to 8.2 mm. Shadows a
  third darker than the paper, soft or sharp, and half as dark when soft (2.5 mm) keep the
  corners within 0.2 mm; half as dark with a sharp edge (1 mm) joins the part (TD-178).
  The acceptance run (`measureautooutline`: a 2000 x 1500 photo at a slant, the card turned
  12 degrees, the card's corners exact): sides 80.00, 19.99, 60.00, 40.01, 20.01, 59.99 mm,
  80.02 x 60.01 mm, the hole 9.99 mm, fitted within 0.04 mm, in about 0.22 s on the desktop;
  extruded 5 mm, 11609.6 mm³ against 11607.3. A real photo adds lens
  distortion (not corrected: the straightening is projective), a card cut a little off its
  nominal size (TD-176), the part's thickness (its top edge stands above the paper, so seen
  from an angle its outline shifts; from straight above it doesn't) and real shadows.
- **Limits** (TD-178): the straightened view reaches about 1.5 card sizes around the card (13
  cm or so beside it): a bigger part needs a sheet of A4 or Letter as the reference, the part
  on it. A part touching the card's margin, the photo's edge or the paper's edge is cut or not
  found. Fillets under about 0.6 mm come out as sharp corners; parts under about 0.5 mm wide
  are dropped as specks; a shiny part's glints larger than 3 mm² read as holes; a part the
  colour of its paper is not found (it says so).
- **A ruler instead of a card**: Sam asked about photographing beside a ruler. A ruler gives
  the scale but not the photo's angle: it measures true only in a photo taken straight down,
  and only along it. The card's four corners give both the scale and the correction for an
  angled photo, so the card stays the reference. A ruler (its marks read off the photo, the
  photo taken from straight above) is a possible addition.

### Camera (AR)

- ARKit's camera view with a crosshair; **Add point** places a point where the crosshair meets
  a surface (raycasts on estimated planes; LiDAR depth where there is one). **Distance**,
  **Outline** (points on one surface, closed) and **Box** (three base corners, then a point on
  the top). The view is native UIKit (`src/ui/ios/ArMeasureIos.mm`), so it is iOS only; the
  result comes back to the Measure workspace to keep, use, sketch or make into a box.
- LiDAR (the Pro iPhones) is good to about 1 cm; without it ARKit's planes are less exact. AR
  is for rooms and furniture, not for print tolerances: the readout says so.

## Measurements

- `measure/MeasurementLog`: the kept values (length mm, angle degrees, area mm²), named
  ("Length 3" until renamed), newest first, at most 60, stored in the app's settings
  (`measure/measurements`). Shown in the Measure workspace's tray; each can be used, renamed,
  removed.
- `valueText` writes what the value boxes' parsers read back ("42.3mm", "37.5°").

### Into the design

Sam, after build 62: "can we make it more clear how to implement measurements into builds?"
The three ways were all there but scattered; now the workspace says them where a measurement
is:
- **Keep** says "Kept as Length 2 · tap it to use it" (just "Kept as Length 2" when a value box
  asked: its Use is right there).
- **A kept measurement's card** has an **In the design** part: **Add to project** (the main
  button unless a value box asked), and once added the card stays and says "In the project as
  length_2. Type length_2 in any value box (length_2 - 2 works too); on the keypad it's a key".
  From Home (no design open) it says to open a design first. A kept measurement in the project
  is marked: a green check on its chip, its project name in the list.
- **"?" beside Kept** (and **How measurements reach the design** on a card) opens
  **Measurements in the design**: 1. straight into a value box (its Measure button, then Use),
  2. by name (Add to project, then type the name; the keypad's keys; the Model panel's
  Measurements), 3. as a shape (Make sketch, Make box, a placed photo). **Add all N kept to the
  project** adds every kept length and angle not yet there (one undo step each).
- Whether a kept measurement is in the project is read from the project: one of its
  measurements of the same kind and value (`MeasureScreen.projectNameFor`), so it holds across
  sessions and after renames in the Model panel.

### Measurements in the project

The log above lives on the device; a project keeps its own list, saved with it, whose names
any value box reads.

- **What**: `doc::ProjectMeasurement` {name, quantity (length mm / angle degrees), value,
  note ("Ruler", "Photo"...)} in `Document::measurements()`, in the order added. Names are
  value names (`core/Units` `isValueName`): a letter or `_`, then letters, digits, `_`, at most
  64, unique ignoring case, never a unit word ("mm", "in", "deg", "ft"...: "2 in" must stay two
  inches). `valueNameFor` makes one from a display name: "Length 3" -> `length_3`, "Shelf
  depth" -> `shelf_depth`, accents dropped, a unit word gets `_value`.
- **Typed by name**: `parseLength` / `parseAngle` take a `NameResolver`; every typed-value path
  passes `Document::valueNames()` (the value box for every operation, sketch live values and
  dimensions, the Model panel's step values, the keypad's text). `shelf_depth - 2`, `2 * wall`,
  `(width + 0.4) / 2`, `corner_angle / 2`. A length's name counts in mm and a bare number
  beside it stays in the display unit (`wall + 2` in inches = wall + 50.8 mm); an angle's name
  counts in degrees; the wrong kind is refused ("This is a length, and corner_angle is an
  angle."), as are an unknown name ("No measurement called 'x' in this project"), a number
  right before a name ("Put an operator before wall: 2 * wall, 2 + wall") and a unit after
  one. While a project has none, the resolver is empty and stray letters read as before
  ("Unexpected 'x'").
- **A copy, not a link**: a name is evaluated when the value is typed; the step stores the
  number. Changing a measurement later changes nothing already built (the Model panel's
  footnote says so). A live link (a step's value as an expression that follows the
  measurement) is future work.
- **Undo**: every add, rename, new value and delete is one `SetProjectMeasurementsCommand`
  (the whole list before and after).
- **File**: `document.json`'s optional `measurements` (docs/FILE_FORMAT.md); written only when
  there are some, so a project without any is byte-for-byte what 0.3 wrote. 0.3.x ignores
  the key: it opens such a project and drops the measurements if it saves it again.
- **UI**: the Model panel's **Measurements** section (`ProjectMeasurementsSection.qml`, the
  list view's footer): rows "shelf_depth = 243.0 mm", a tap or click opens one (name, value
  with the keypad on touch, Delete), **+** adds one by hand, a hint "Type a name in any value
  box, e.g. shelf_depth - 2". The keypad shows the measurements of its kind as keys above the
  digits (`insertKeypadName`; backspace takes a name whole). The Model panel shows when the
  project has steps or measurements. A project without any shows no section, only **+
  Measurement** in the panel's header, so its panel keeps the height it had (the iPhone Duo's
  layouts are tight).
- **For the Measure workspace** (AppController): `projectMeasurements` ({name, quantity, value,
  text, typed, note}), `addProjectMeasurement(name, quantity, value, note)` (returns the name
  used: a display name is made a value name, a taken one gets `_2`), `renameProjectMeasurement`,
  `setProjectMeasurementValue` (typed with units), `removeProjectMeasurement`, `identifierFor`.
  Acceptance: `projectmeasurements`.

## How the code is laid out

- `src/measure/` (`openshape_measure`, Qt-free, `tests/test_measure.cpp`): ScreenScale,
  Homography (PlaneRectifier, the straightened view and the warp), StrokeFit, Fold
  (SteadyReading), Level, Outline, Measurement, ObjectOutline (Find outline), OutlineProfile
  (its points, report and pieces) and OutlineSynthetic (test photos of parts beside a card).
- `src/ui/MeasureDevices.h`: the platform's hardware as functions, empty where there is none;
  `src/ui/ios/*.mm` fill them in (hinge, gravity, haptics, camera and photos, Vision, ARKit).
  `pickPhoto` takes `CameraOptions` (the camera screen's purpose, Photo or Scan, and the last
  photo for the overlay; the photo library ignores them).
- `src/ui/MeasureController.*`: the Measure workspace's state for QML (`app.measure`): the
  scale and its calibration, the sensors (started while the workspace is open), the photo
  session, the measurements, and the stand-ins the acceptance run drives.
- `src/ui/qml/MeasureScreen.qml` and its parts: the workspace.
- AppController: `createOutlineSketch` (on the ground, Front or Right), `createCircleSketch`,
  `createSizedBox` (one undo step each), the 1:1 view, the value boxes' ruler button;
  SketchSession: the Trace tool.
  - 1:1 lives in InteractionController (`setScreenScale`, `setActualSize`): orthographic,
    `orthoHeight` = viewport height / points per mm, same direction and center. Anything that
    zooms (wheel, pinch, Fit, a framing view, revealing the selection for the keypad, choosing
    Perspective) leaves it at the size then shown; a view that only turns (a standard view,
    finishing a sketch) keeps it; a new scale or window size keeps it at 1:1. Before the Measure
    workspace calls `setScreenScale`, AppController estimates the primary screen's scale.
  - Trace (key D): tolerance max(0.2 mm, 2.5 px), closing gap 14 px at the current zoom. The
    fitted pieces share end points; an arc tighter than three tolerances (a hand-rounded
    corner) and pieces shorter than two tolerances are dropped (their neighbours meet), and a
    closed stroke begun mid-side gets that side as one line.
  - The ruler: the value chip's (`valueChipMeasure`, whenever an operation's chip shows) and a
    sketch dimension editor's (`dimensionMeasure`) ask Main.qml's `openMeasure(kind, target)`;
    `useMeasuredValue(text)` puts the result into the box that asked, previewed, for ✓ or Enter.
- Reference photos: `AppController::addReferenceImage(file, mmPerPixel, plane, centerX, centerY)` (below).

## Reference photos in the design

A photo placed in the design at true size on a plane (the ground, Front, Right or a construction
plane), drawn under and behind the model (a body in front hides it, as real geometry would),
semi-transparent, so the owner can model or sketch over it: trace a part's outline with the
sketch tools, check a design against the real object. Saved in the project
(`references/<id>.jpg|png`, docs/FILE_FORMAT.md).

- **From the Measure workspace**: the straightened photo, whose scale in mm per pixel is exact:
  `addReferenceImage(imageFile, mmPerPixel, "XY")` puts it on the ground at its real size,
  centred on the origin (or with the view's origin point there, on the view's plane), frames
  the view on it (Top view) and is one undo step. A set of views goes in with **Place views**
  (Photo, above): each on its plane, their origin points at the origin.
- **File → Add Reference Photo…** (desktop; on an iPhone or iPad photos come from the Measure
  workspace, the file picker being unreliable on the iPhone Duo, TD-158): any picture, at an
  unknown scale (200 mm wide); its row says the scale is not set.
- **Model panel row** (a photo icon): Width, Opacity (%), Rotation, X and Y on its plane, the
  Plane (Ground, Front, Right, construction planes), Hide / Show, Delete, and **Set scale**: tap
  two points on the photo a known distance apart (a loupe under a finger; the view still orbits,
  pans and zooms; Back takes a point back, Esc or Cancel ends it), type the real distance: the
  photo is rescaled about its center (one undo step).
- Deleting the construction plane a photo lies on moves the photo to the ground (undo puts it
  back). Pictures larger than 4096 px a side are scaled down when added; a JPEG turned by its
  EXIF orientation is stored upright.

## Privacy

The camera, the photo library and the motion sensors are used on the device only. Photos are
kept in the app's temporary folder while measuring and never sent anywhere (the camera's last
photo stays there for the camera screen's overlay until the next photo replaces it or FoldForm
quits); FoldForm still collects no data (docs/PRIVACY.md). iOS asks once for the camera (`NSCameraUsageDescription`);
the photo library is read through the system picker, which needs no permission; motion sensors
need none.

## Not yet (TD-166 onward)

- A live link from a step's value to a project measurement (today a name is copied as a
  number when typed: "Measurements in the project").
- Apple's Object Capture (RealityKit, Swift only) or ARKit's LiDAR mesh as other routes to a 3D
  scan; FoldForm's own **Scan** (docs/SCAN.md) is the one built.
- Find outline: a tap to choose the part when there are several (the code takes a point,
  `findOutline(x, y)`; the screen offers none yet), dragging a found hole's points, a ruler as
  the reference for a photo from straight above, and lens distortion correction.
