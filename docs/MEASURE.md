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
- **Scale**: iOS doesn't say how big its screens are, so FoldForm knows the iPhones' and iPads'
  pixel densities (`measure/ScreenScale`: 460 ppi 3x iPhones, 458 for the Duo's displays (an
  estimate from the diagonals), 264 iPads, 326 iPad mini) and offers **Check with a card**: a
  bank card's outline (ISO/IEC 7810 ID-1, 85.60 x 53.98 mm) drawn at the current scale; lay a
  real card on it and stretch the outline until they match. The calibration is kept per screen
  (`screenKey`: "951x669@3"; the Duo's two displays get one each). Until then the readout says
  the scale is estimated.

### Fold (iPhone Duo)

- UIKit's `UIHinge.angle` (radians, 0 closed, pi flat) read through a `UIHingeInteraction` on
  the app's view. Shown: the angle between the halves and the bend (180 minus it), with a side
  view of the phone at that angle.
- **Inside a corner** (the backs on the faces) or **over an edge** (the screens on the faces:
  the reading can't be seen while measuring), so readings are smoothed and **held when steady**
  (`measure/SteadyReading`: still within 0.4 degrees for 0.8 s), with a tap felt in the hand.
- **Gap** (caliper): something pinched between the two free edges is 2 L sin(angle / 2) wide,
  L being a half's length from the hinge to its edge (estimated from the screen, then
  calibrated by pinching a card's side). The readout shows how far one degree moves it.
- Apple says the angle's rate and precision are system policy: the log records what the Duo
  gives (TD-166).

### Level

- Lying down: a bubble and the surface's slope. Standing on an edge: the edge's angle from
  level. **Keep** stores the current position; then the angle between it and the next resting
  position is the angle between the two surfaces. Held when steady, like the fold.

### Photo

1. **Take** or **choose** a photo (the desktop: open an image file) with a reference lying flat
   on the surface: a bank card, A4 or Letter paper, or any rectangle whose sides are typed.
2. **Corners**: Vision finds the reference where it can (`VNDetectRectanglesRequest`); four
   handles (with a magnifier while dragged) fix it.
3. **Straighten**: `measure/PlaneRectifier` computes the photo-to-surface map and
   `warpToView` draws the photo as seen from straight above, in millimeters.
4. **Measure** on it: **Distance** (two taps), **Outline** (taps around a shape, closed on the
   first point: each side, the area), **Circle** (three taps on its rim: the diameter). **Make
   sketch** puts the outline or the circle on the ground plane at real size.
- Only points on the reference's surface measure true; a reference's error scales everything
  (a card gives about 0.5 % from the corners' placement, a sheet of paper better).

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
  (SteadyReading), Level, Outline, Measurement.
- `src/ui/MeasureDevices.h`: the platform's hardware as functions, empty where there is none;
  `src/ui/ios/*.mm` fill them in (hinge, gravity, haptics, camera and photos, Vision, ARKit).
- `src/ui/MeasureController.*`: the Measure workspace's state for QML (`app.measure`): the
  scale and its calibration, the sensors (started while the workspace is open), the photo
  session, the measurements, and the stand-ins the acceptance run drives.
- `src/ui/qml/MeasureScreen.qml` and its parts: the workspace.
- AppController: `createOutlineSketch`, `createCircleSketch`, `createSizedBox` (one undo step
  each), the 1:1 view, the value boxes' ruler button; SketchSession: the Trace tool.
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
- Reference photos: `AppController::addReferenceImage(file, mmPerPixel, plane)` (below).

## Reference photos in the design

A photo placed in the design at true size on a plane (the ground, Front, Right or a construction
plane), drawn under and behind the model (a body in front hides it, as real geometry would),
semi-transparent, so the owner can model or sketch over it: trace a part's outline with the
sketch tools, check a design against the real object. Saved in the project
(`references/<id>.jpg|png`, docs/FILE_FORMAT.md).

- **From the Measure workspace**: the straightened photo, whose scale in mm per pixel is exact:
  `addReferenceImage(imageFile, mmPerPixel, "XY")` puts it on the ground at its real size,
  centred on the origin, frames the view on it (Top view) and is one undo step.
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
kept in the app's temporary folder while measuring and never sent anywhere; FoldForm still
collects no data (docs/PRIVACY.md). iOS asks once for the camera (`NSCameraUsageDescription`);
the photo library is read through the system picker, which needs no permission; motion sensors
need none.

## Not yet (TD-166 onward)

- A live link from a step's value to a project measurement (today a name is copied as a
  number when typed: "Measurements in the project").
- Apple's Object Capture (RealityKit, Swift only) or ARKit's LiDAR mesh as other routes to a 3D
  scan; FoldForm's own **Scan** (docs/SCAN.md) is the one built.
