# Manual test pass

What the owner is asked to try by hand (last updated 2026-09-27). Anything
odd: a screenshot and a sentence is plenty. Starting the app with
`OPENSHAPE_LOG=debug` lets Claude read the log afterwards
(`%LOCALAPPDATA%\OpenShape\OpenShape\logs\openshape.log`).

## FoldForm 0.4.0: Measure, on the iPhone Duo and the iPhone 16 Pro

Only a real device can check these (the PC runs every screen with stand-ins
for the sensors and the camera). After the round, send the log (Files app,
FoldForm, Logs, openshape.log): its `Measure:` lines say what the hinge, the
motion sensors, Vision and ARKit reported.

1. **Ruler and the card check.** Measure (the ruler at the top) → Ruler →
   Check with a card: lay a playing card on the outline (build 63 on; 62
   drew a debit/credit card) and stretch it to the card's edges, Done. Then
   measure the card's short side with the jaws: 63.5 mm should read
   63.4–63.6. Do it on the Duo's outer display and on
   the inner one (each keeps its own scale); say how far the estimate was off
   (the percentage the card check showed).
2. **Fold (the Duo).** Fold tab: open and close the phone slowly; the angle
   follows. Rest it on a box's corner (over the edge, screens on the faces),
   keep still until you feel the tap, lift and read: about 90°. Gap: Calibrate
   with a card, then pinch the card's other side and something you know.
3. **Level.** Lay the phone on a table (the bubble near the middle), on a
   book propped up (the slope), stand it on its edge.
4. **Photo.** Choose photo, and Take photo: a playing card (or a sheet of
   A4 or Letter paper) flat on a table beside a part. Did the four corners
   land on the card by themselves? Straighten, then Outline the part (tap the
   corners, then the first one again) and compare with a ruler. Make sketch:
   the outline opens as a sketch; tap inside it and extrude.
5. **Camera (AR).** Distance between two corners of a table; Box around a
   box (three base corners, then the top); Make box. On the Duo: does the AR
   view open at all, and does it fill the screen?
6. **Into a value box.** Push a box's top face, tap the ruler in the value
   box, measure something, Use: the value lands in the box; ✓.
7. **1:1 and Trace.** View → Actual size: lay a coin on the screen over a
   20 mm circle sketch. In a sketch, Trace (the pen-on-a-wave tool): draw
   around a coin or a key lying on the screen with the Pencil; it should
   become a circle or a few lines and arcs.
8. **Scan.** Measure → Scan: a mug or a small box on a sheet of A4 paper on a
   plain table, good light; 10 to 12 photos walking around it from about 45°
   above, all four paper corners in each. Build (stereo on): does it finish
   in a few seconds, and do its W × D × H match a ruler? Add to design.
   Then the same object from the photo library (Choose photos).
9. **Reference photo.** Photo → Straighten → Place in design: the photo lies
   on the ground at its real size; sketch a rectangle over a part in it.
   Model panel: change its opacity; Set scale with two points.
10. **Measurements in the project.** Keep a ruler length, open its card, Add
    to project; then push a face and type its name minus 2 (e.g.
    `length_1 - 2`) in the value box, or tap its key on the keypad.
11. **The ruler along the long side (next build; your request).** On the Duo
    unfolded, Ruler: the scale runs across the whole width (about 134 mm),
    the readout and the kept measurements below it, and "Fold" marks the
    crease on the scale (check it sits on the real crease). Drag a jaw past
    the fold with the phone half open: the distance turns amber and the tag
    says "Open flat"; open it flat and the warning goes. Turn the open
    phone tall: the scale runs down the long side, the readout beside it.
12. **When the fold holds (next build; your question).** Fold tab, phone in
    your hands: the number says LIVE. Rest it on a box's edge and keep still:
    the pill fills for about a second, then turns green ("Held · 90.0°"),
    the tag says HELD and you feel the tap. Lift it: the number stays. Fold
    it to another angle and keep still: the pill fills again and the new
    angle replaces the old. Measure again with the phone lying still: it
    waits a full second again. Same on the Level, and say whether holding
    the phone in your hand after a hold ever replaces it.
13. **Measurements into the design (next build; your question).** In a
    design, Measure → Ruler → Keep: the message says "tap it to use it". Tap
    the kept chip: its card has "In the design" with Add to project; tap it:
    the card says the name to type and the chip gets a green check. Close,
    push a face and type that name (or tap its key). Also try the "?" beside
    KEPT: is the three-step guide clear, and does Add all work?
14. **The playing card (build 63; your request).** Ruler → Check with a
    card: the outline is a playing card (63.5 × 88.9 mm); lay a Bicycle card
    on it, stretch the outline to its edges, Done. (Ignore the "Bank card"
    choice above the outline: it meant a debit or credit card, and it goes
    in the next build.) Then lay a real ruler on the screen along FoldForm's
    ruler, zeros together, the phone open flat: at 100 mm, how far apart are
    the marks? Under half a millimeter means the playing card is a good
    reference; tell me the gap either way. The Fold's Gap calibration and
    Photo's Card now mean the playing card too.
15. **The ruler at the screen's edge (the build after 63; your request).**
    Ruler on the Duo unfolded: the scale runs along the top edge, its ticks
    starting at the edge, the tabs at the bottom. Lay the phone beside
    something longer than it, read the ticks where its end is, and check
    the zero tick at the top-left corner isn't cut off by the screen's
    rounded corner. On the iPhone and the Duo turned tall, the scale runs
    down the left edge.
16. **Photo: guides, several dimensions, views (next build; your request).**
    Photo → a part and a playing card on a table → Straighten. Rulers run
    along the photo's top and left: zoom in with two fingers, they follow.
    Tap **Grid**: 10 mm squares (finer zoomed in). Distance: tap one end,
    then press the other and slide: the angle shows by your finger, and near
    0° or 90° the line snaps straight and turns green ("Straight · 0°"). A
    slanted edge keeps its angle; with **Snap** off a nearly straight one
    stays as you placed it. Measure three things: each stays on the photo as
    A, B, C and in the list under it; tap B, tap it again, rename it, then
    **Keep all** (kept as "Top A", "Top Width"...). Then **+ View**, photograph
    the part from the front (the card flat on the same table), straighten,
    outline its side, tap **Origin** and its bottom-left corner. Tap Top in
    the strip and back: the dimensions come back. **Place 2 views** →
    **Photos and outlines as sketches**: the top photo lies on the ground,
    the front one stands on the Front plane, the outlines on them at real
    size; Undo four times takes it back. Does the snap feel right (1.5°), is
    the list under the photo enough, and do the views line up in the design?
17. **FoldForm's own camera (next build; your idea).** Measure → Photo →
    Take photo: FoldForm's camera opens, not the system's (no "Retake / Use
    Photo" step). Hold the phone over a playing card on a table: the yellow
    cross moves as you tilt; flat above the card it meets the white cross,
    both turn green ("0.6° off") and you feel a tap. Try 1× and 2× (and 5×
    on the 16 Pro), turn the phone sideways (the buttons move to the side),
    and on the Duo both displays. Take the photo: the corners step opens as
    before. New photo → Take photo again: tap **Last photo**; the first
    photo shows faintly, exactly over the view (does the card line up with
    its ghost when the phone is where it was?). Then Scan → Take photos:
    the gauge shows the angle, green between 35° and 55°. Cancel works
    everywhere. Send the log: its `Measure: camera:` lines give the tilt,
    the zoom and the focal length of each photo.
18. **Find outline (next build; your idea).** Lay a flat part with a hole
    (a bracket, a washer, a gasket) on plain paper of a contrasting colour
    (white paper for a dark part, black or blue for a light one), the
    playing card beside it on the same paper, a finger's width apart. Take
    the photo from straight above (then once from an angle), Straighten,
    Outline, **Find outline**. Does the outline sit on the part's edge, with
    its hole? Compare the width x height and the hole's diameter in the
    readout with your calipers, and tell me how far off they are. Drag one
    point and see it follow; then Make sketch, push the shape up a few
    millimetres and check the hole goes through. Try it once in harsh light
    (one lamp low to the side) and tell me whether the shadow got in.
19. **Calibrating the Level (next build; your request).** Level, phone lying
    on a table (any table: it needn't be level). Check: keep still until it
    holds, turn the phone end for end in the same spot (rotate it half a
    turn flat on the table, top where the bottom was), tap Turned, keep
    still: it says the two readings and how far off the level is. Then
    Calibrate the same way: it says the offset it found ("It read 0.4° off
    lying down") and the surface's slope. Check again: now "off by" should
    be 0.1° or less. Tell me the offset, and whether the two Checks agree
    with the Calibrate. Same standing on the long edge (Check, then
    Calibrate). Reset brings back the raw reading. A pencil line around
    the phone helps put it back in the same spot.
20. **Scan on Legal paper and on your pad (next build; your report).**
    Measure → Scan, pick Legal paper: a small box on a sheet torn off your
    yellow legal pad (the pad's glued top hides part of the sheet, and
    Legal means the whole 8.5 × 14 in), on a darker table. 10 to 12 photos
    around it. Are the paper's corners right in every photo (open a few)?
    Build: do W × D × H match a ruler? If you can, the same box on plain
    white paper, to compare how often the corners were found.

Checks 21 to 26 of this list (several faces, tap again to unselect, edges,
turning the view, a hole made larger, the Pencil and your fingers) went out
as 0.3.2 and are in every build after 64: they are the section "FoldForm
0.3.2" below, 1 to 6.

## On Windows (the desktop shortcut)

1. **Sizes instead of distances.** Add a box, click its top face: the value
   box says "Height 20 mm" and a blue line shows what is measured. Drag the
   arrow (the number follows), type `35` + Enter, then `+5` and `-5`. Side
   faces say Width / Depth.
2. **Rounded edges come along.** Round the four top edges of a box (click an
   edge, Shift-click the others, type `3`, Enter). Click the top face, type
   a new height: the rounded edges should keep their size and move with the
   face — also when making it shorter, and with a chamfer instead.
3. **Sketch tools** (press K for a sketch):
   - Slot (O): click the two end centers, then move or type the width.
   - Trim (T): draw a rectangle and a line across it; point at the part of
     the line sticking out (it turns red) and click.
   - Round a corner: Select tool, click a rectangle corner, "Fillet"; click
     the R label to change the radius.
   - Offset: Shift-click the four sides of a rectangle, "Offset", move
     outside or inside, type `2`, Enter.
   - The tools now sit on the left (Draw / Edit). Center rectangle (E):
     click the center, then a corner. Polygon (P): click the center, move
     out, press + or - (or the on-screen buttons) to change the sides, type
     the size across flats. Tangent arc (G): draw a line, then click its
     free end and swing the arc.
   - Constraint glyphs (H, V, =, …) beside the geometry: with the Select
     tool click one, then "Delete constraint". Check that clicking right
     next to a line still selects the line, not the glyph.
   - Mirror: draw half a shape against a line, select the half, "Mirror",
     click the line. Pattern: select a hole, "Pattern", click where the next
     one goes, + for more, Apply; try "Circular" too.
   - Editing by dragging (Select tool): drag a rectangle's side, a circle's
     rim and its center, the inside of a shape clicked first (without the
     click the drag orbits), and one of several
     Shift-selected items; drop a line's end on a corner, on a side and on
     a side's middle (it stays joined: drag the corner and the line
     follows). Click a side, then its dimmed length, type `30`, Enter.
     Double-click a side: the whole shape; Delete. Click inside a shape →
     "Extrude". Blue items can still move, dark ones cannot; dragging a
     dark one says "Fully sized …". Each drag is one Undo.
4. **Extrude options.** Click a sketch profile, then "Symmetric" and type
   `10` (5 mm each side); or "Up to face" and click the top of another body.
5. **Axes and the view cube.** Blue Z axis through the origin and the view
   cube above the view buttons; orbit and watch the cube turn (edges: X
   red, Y green, Z blue). Click its Top face, then a corner (an isometric
   view down that corner), then drag the cube: the view orbits. Press `K`,
   arm Line and drag the cube again: it orbits and draws nothing.
6. **About.** File → About FoldForm.
7. **Touch layout (optional).** Add ` --touch` after `OpenShape.exe` in the
   shortcut's Properties → Target: bigger buttons and a Pen switch.
8. **A real part.** Design something you need, export STL or 3MF, print it
   and measure: do sizes and hole diameters come out as typed?
9. **Work is never lost.** Add a box, wait five seconds, then kill
   OpenShape: Task Manager → Details → OpenShape.exe → End task. Start it
   again: it offers to restore the box (Restore / Discard / Decide later).
   Restore, then Ctrl+S. Closing the window with unsaved changes and
   choosing Don't Save must not offer anything at the next start.
10. **The app remembers you.** Move and resize the window (or maximize it),
    close and start again: same place. File → Open Recent lists your last
    projects. File → Preferences… (Ctrl+,): units for new documents, grid
    snapping in sketches, how often recovery copies are kept.
11. **Bodies.**
    - Duplicate: select a body and press Ctrl+D (or "Duplicate"): a copy
      named "… copy" appears, selected with the Move arrows; drag it aside.
      Change a step of the copy in the Model panel: the original stays as
      it was.
    - Split into bodies: cut a plate in two (a sketch across it, extruded
      as a cut through the plate): a message says it is now in pieces and
      the row turns amber. Click "Split into bodies": each piece is its own
      body. Push/pull one piece, then the other: each changes alone. Delete
      the body you split: the other piece stays.
    - Separate bodies (the report from the iPhone): move a box away from
      the middle, Mirror, "Across YZ": "Separate bodies" lights up by itself
      and the line at the bottom says the image will be a separate body.
      Apply, then push/pull the original's top face: the image stays as it
      was (and the other way round). Pattern the box: 5 mm apart the copies
      are separate bodies; type the box's width as the spacing and they
      join. Mirror a box across its own face: one body. Open a project from
      version 0.1.0 with a mirrored or patterned copy, mirror that copy:
      changing the 0.1.0 original moves its old copy but not the new one.
    - Rotate about an edge: select a body, Rotate, click one of its straight
      edges: a single ring appears around that edge; type `90`, Enter. Try
      again clicking near a corner (the rings move there), a hole's wall
      (turns about the hole) and "Center pivot" (back to the middle).
12. **Holes.**
    - Counterbore / Countersink: in a plate with a through hole, click the
      hole's rim: next to "Heat-set insert" are "Counterbore" and
      "Countersink". Pick a screw size (M2 … M6), drag the arrows or type
      a size, Enter. A head that does not fit (narrower than the hole,
      through the plate) is refused with a reason.
    - The Hole tool: click the top face of a plate, then "Hole" (or Modify
      → Hole). Click a few spots: holes snap to the middle of the face and
      of its edges and line up with each other. Pick M3 and "Close fit",
      "Normal fit" or "Tap"; switch "Through all" off to type a depth; try
      "Counterbore" / "Countersink"; Tab through the fields to type a
      hole's X / Y ("From last hole" measures from the previous one);
      "Remove hole" drops the current one; Enter drills them all as one
      step. Print a plate and try real screws.
    - Draft: click a sketch profile, then "Draft" in the value box, type
      `5`: the walls taper (a square becomes a pyramid stump). A negative
      angle widens. A draft that would close the shape before the full
      height is refused. An existing extrusion's row in the Model panel
      offers "Draft" too.
13. **Home and STEP.** Start OpenShape from the shortcut: Home shows your
    recent projects with pictures (save a project once to give it one).
    Tap a card to open it; ⋯ (or a right click) → Remove from list. File →
    Home comes back to it. On a card's ⋯: Duplicate (a "… copy" card with
    the same picture appears first), Rename… (try the name of another
    project: refused; then a new name: the card and, for the open project,
    the title bar change), Delete… (the card names the project; afterwards
    it is in the Recycle Bin and can be restored from there).
    **iPhone / iPad:** the same, by touch and hold on a card. After Delete…,
    open the Files app → Browse → Recently Deleted: the project should be
    there and "Recover" should put it back into On My iPhone → FoldForm
    (Home shows it again). If instead FoldForm asks "Delete … for good?",
    tell us: iOS did not take it into Recently Deleted. Import STEP… with a part from another program
    (Onshape, Fusion, FreeCAD, a download): the bodies should keep their
    size and names; push/pull a face and round an edge of it, save, reopen.
14. **Print helpers.**
    - Hole allowance: File → Preferences… → "Hole allowance for 3D
      printing" (0.2 mm to start with). With the Hole tool, M3 close fit
      now shows 3.40 mm (3.2 + 0.2). Print a plate with M3 close-fit and
      normal-fit holes and try real screws; if they are tight or loose,
      change the allowance (0.1, 0.3, or type e.g. `0.25`) and print again.
      Tap and heat-set insert sizes do not change.
    - Text: click a flat face, "Text", type a word (the letters appear in
      the middle), click elsewhere on the face to move it, drag the arrow
      up (raised) or down (cut in), click Size and type `5`, 90°, Deboss;
      type a digit after clicking the face (it goes to the words); Enter.
      Text again on another face: click where the words go, then type (the
      remembered words are replaced, not added to); Ctrl+Backspace erases a
      word, never the face; on a German or French keyboard AltGr+Q or
      AltGr+0 types @. In
      the Model panel, click the Text step and change the words. Print a
      label: are 5 mm capitals 0.6-1 mm deep readable?
15. **Axes and planes** (your question: "can you align to an axis? And can
    you create axis?").
    - Align onto the origin: in a plate with a hole, click the hole's rim,
      Align, then click the blue Z line (or "Z axis"): the hole ends up
      around Z. Try a flat face onto "XZ plane" (Flip puts the part on the
      other side) and a rim onto "Origin".
    - Construct → Axis, click the hole's rim: a dashed orange axis. Select
      another body, Pattern, Circular, click the dashed line: the copies go
      around the hole. Rotate about it the same way. Try "Two points" (two
      corners) and "Parallel to Z" (one corner).
    - Construct → Plane, click a top face, type `10`, Enter: a see-through
      orange square above it, selected. Sketch on it, draw a rectangle,
      finish and extrude. Make the part taller (its row in the Model panel):
      the plane, the sketch and the new block move up. Change the plane's
      distance in its row. Try "At angle" on an edge and "Midway" between
      two parallel faces; Mirror a body across a plane by clicking it.
    - Hide, Show and Delete in the plane's row; undo; save and reopen.
16. **The documentation.** Follow the Quick start in README.md step by step
    (the mounting plate with two holes), and skim docs/USER_GUIDE.md: does
    everything work as written? Is anything you use missing? F1 → the
    link under the first paragraph opens the guide on GitHub.
17. **The new look (your report: "confusing and not realistic at different
    angles", "the grid cuts off abruptly").** Open a real project and orbit
    all the way round, low over the ground, from above and from below:
    tops should stay the lightest, undersides the darkest, and two sides
    seen at once never the same shade. The view is now in perspective;
    zoom with the wheel onto a small detail (it heads for what is under
    the pointer) and orbit about it. The grid should fade out softly, also
    far away towards the horizon, with a faint shadow where a body stands
    on the ground. The Perspective / Orthographic button switches back;
    close and start again: your choice is kept. Is anything harder to
    read than before (edges, sketches, selected faces)?
18. **Licenses (for the App Store release).** File → About FoldForm →
    **Licenses**: the list starts with "Your rights to the LGPL libraries",
    then OpenShape, the libraries (Open CASCADE, Qt, PlaneGCS, FreeType,
    libzip, nlohmann/json, Eigen, Noto Sans) and "Inside Qt". Open a few:
    each shows the license text and where its source is; Back and Esc
    return to the list. Then read docs/LICENSING.md, "The iOS app and the
    App Store" (what selling on the App Store needs; the settled points,
    judgement calls and risk) and [EULA.md](EULA.md): the proposed
    license agreement for App Store Connect, with your name, address and
    contact to fill in. Two decisions for you: whether to use that custom
    EULA (recommended) and which contact to publish for the source offer
    (now the GitHub issue tracker).
19. **Typing without jumps (your report: "as I type in 100, the model snaps
    to 1mm, then 10mm, and finally 100").** Click a box's top face and type
    `100` quickly: the model stays at 20 mm until you stop typing (about
    0.7 s) or press Enter. Type `5`, wait: it follows. Press Enter right
    after typing `30`: exactly 30. The same while drawing a rectangle
    (type the width, Tab, the height). On a touch screen the numeric keypad
    types instead (see IPAD.md).
20. **Units and arithmetic, and a dimension's field.** Type `2 cm`, `1' 2"`
    (14 inches), `6×7` and `10 deg` into a box's height: 20 mm, 355.6 mm,
    42 mm, and a message that this is a length (nothing changes). In a
    sketch with inches shown (the **mm / in** button), tap a dimension's
    label and apply it without typing: the size stays exactly what it was
    (a 12 mm side stays 12 mm, not 0.472 in). Type a new value and tap
    somewhere empty in the view: the value is applied and nothing is drawn
    there, even with the Rectangle tool; with the field open, a drag that
    starts on a side moves nothing. On the iPad keypad, tap a value, then
    **×** **2** **✓**: it doubles.
21. **A reference photo to model over (0.4).** File → **Add Reference
    Photo…**, pick a photo of a part lying flat next to a ruler: it lies on
    the ground, half see-through, 200 mm wide, and the view looks down on
    it. In the Model panel open **Photo 1**, **Set scale**: click both ends
    of 100 mm on the ruler, type `100`, **Set scale**. Draw a box over part
    of it: the box hides the photo where it stands. Start a sketch (K) and
    trace the part's outline over the photo. Try Opacity, Rotation, X / Y
    and the Plane buttons, Hide, Delete and undo; save, reopen: the photo
    is still there. On the phone, photos come from the Measure workspace.

## The Windows installer (optional)

`scripts/windows/test-installer.ps1` (silent) and
`scripts/windows/test-installer-dialogs.ps1` (clicks through the dialogs)
check the installer automatically; this is the real thing, once, by hand.
Use `OpenShape-<version>-windows-x64-setup.exe` from a GitHub Release (or
`dist/` after BUILDING.md, "Release").

1. **Install.** An unsigned release: Windows SmartScreen warns
   ("More info" → "Run anyway"). A signed one (its release notes say so,
   docs/CODE_SIGNING.md): right-click the setup → Properties → Digital
   Signatures shows **SignPath Foundation** and "This digital signature is
   OK"; the same for the installed `OpenShape.exe`; if SmartScreen still
   asks, it names SignPath Foundation as the publisher, not "Unknown
   publisher". No administrator prompt should appear. Click
   through: the license page shows the MPL-2.0, the folder is
   `%LOCALAPPDATA%\Programs\OpenShape`. On the last page "Create a desktop
   shortcut" is off. **Leave it off** if you want to keep your current
   desktop shortcut to `dist\OpenShape`: both are called `OpenShape`, so
   ticking it replaces yours (and uninstalling removes it).
2. **Use it.** Start OpenShape from the Start menu; save a project, then
   double-click the `.openshape` file in Explorer: it opens in OpenShape and
   shows the OpenShape icon.
3. **Install again while OpenShape is open.** The installer says OpenShape
   is running: close it, click Retry; the installation finishes.
4. **Uninstall** from Settings → Apps → Installed apps → OpenShape. The
   Start-menu entry and the file association are gone; your projects stay.

## FoldForm 0.3.2: selecting, the view, a hole made larger (the Duo, with the Pencil)

Only a device can say whether these feel right: the PC checks them with
synthetic finger and Pencil events at the Duo's sizes. The owner's list
with boxes to tick is the vault's `DuoApps/FoldForm/FoldForm tests.md`.

1. **Several faces, with the Pencil as you use it.** Open the Electronics
   box or the Honeycomb organizer. Tap a small face, then three more, some
   of them near their edges on purpose: each tap should add the face under
   the tip; nothing should turn into an edge, and nothing should clear. Tap
   one of them again: only it goes. Tap two faces quickly one after the
   other: both are added (not the whole body). Tap just beside the part:
   nothing changes, and a line says how to go on. Tap empty space well away
   from it: all clear.
2. **Tap again to unselect.** Tap one face: its arrow stands on it. Tap the
   same spot again: the face is unselected. The same with an edge (the
   Fillet arrow starts on it). Then tap a face and drag from the same spot:
   the arrow still pulls the face.
3. **Edges.** With nothing selected, tap an edge, then two more (for a
   fillet): each tap near an edge adds it. Tap the middle of a large face:
   the edges stay, and a line says so. A small face's middle is the face
   when nothing is selected, and its edges are in its outer quarters: does
   that feel right on the outer display?
4. **Turning the view.** Drag one finger straight up or down and keep
   going: the view passes over the top (or under the bottom) and comes
   round upside down, with no stop. Upside down, drag sideways: the model
   follows your finger. Tap Iso (or the view cube): the right way up.
5. **A hole made larger.** A box, a sketch on its top, a square in it,
   pushed through as a cut. Hold a finger (or the Pencil) on one wall of
   the hole for half a second: all four walls are selected and one arrow
   shows. Drag the arrow back into the part (or type `-2`): the hole grows
   on every side, corners square; the tick applies it. Tap the top and one
   side of a box and drag: both grow. With all four walls selected, pull
   the arrow out into the hole as far as it goes: before the walls meet,
   the value turns red and says how far they can go; the app must not
   close.
6. **The Pencil and your fingers in a sketch.** Start a sketch on a face
   and draw a line with the Pencil; drag one finger across the view: it
   turns (it only slid sideways before), the sketch staying open; **Face
   on** looks straight at it again. Two fingers still pan and pinch. Does a
   resting hand ever turn the view by accident?

## On the iPhone Duo

See [IPAD.md](IPAD.md), "The iPhone Duo": fold and unfold mid-sketch and
mid-edit (a value typed on the keypad), on Home and with the Model panel
open; turn the inner display; Split View on both sides of the side bar;
half fold while typing (TD-140). Then send Logs/openshape.log: its
`window ...` lines give the real sizes and safe areas (TD-150).

## The purchase "Unlimited projects" (TestFlight, iPhone or iPad)

Needs the in-app purchase created in App Store Connect and the Paid Apps
Agreement active ([IPAD.md](IPAD.md), "The in-app purchase"). TestFlight
uses Apple's sandbox: nothing is charged.

1. **Free with one project:** with one project in FoldForm's folder (delete
   the others in the Files app, or use a new device), Home shows "Free
   version of FoldForm: one project · Unlock unlimited projects". Tap New
   project: the sheet "Unlimited projects" opens with "Unlock for <price>"
   in your currency, Restore purchase and Not now. Not now: nothing changes.
2. **Buy:** New project → Unlock for <price> → Apple's sheet (it says
   "[Environment: Sandbox]") → confirm. The sheet closes, "Unlimited
   projects unlocked" shows and the new project opens. Home's line is gone;
   File → About says "Unlimited projects: unlocked".
3. **Restore:** delete FoldForm and install it again from TestFlight (save
   a project somewhere else first), save one project, then File → About →
   Restore purchase: it unlocks without paying again. On the other device
   (iPhone / iPad with the same Apple Account) Restore unlocks too.
4. **Duplicate, Import STEP, Save As a new name, a project sent from Mail:**
   each opens the sheet while free (repeat step 1 on a fresh install).
5. **No connection:** airplane mode, open the sheet from Home's line: Unlock
   is off and a line says the App Store can't be reached.
6. Send Logs/openshape.log afterwards: the `store:` lines say what the App
   Store answered.

## On the iPad and iPhone

See [IPAD.md](IPAD.md), "What to test on the iPad" and "What to test on the
iPhone". New in the build after 2026-09-27 (iPad 12-18, iPhone 13-18):

1. **Share an STL to the slicer:** File → Export STL: the share sheet opens
   (on the iPad pointing at File); choose the slicer app, then AirDrop a
   3MF to another device; close the sheet once without choosing (no
   message; the file is in FoldForm → Exports).
2. **Share Project…:** on a new project it asks for a name first, then the
   sheet; send the project to yourself by Mail.
3. **Open a STEP file from the Files app:** Share → FoldForm: a new project
   with the file's bodies (unsaved changes are asked about first).
4. **Open a STEP file and a project from Mail:** touch and hold the
   attachment → FoldForm; the project opens as a copy in FoldForm's
   folder; no "Inbox" folder is left in FoldForm's folder afterwards.
5. **Open a project from the Files app:** tap one in On My iPhone / iPad →
   FoldForm (opens directly), and one in iCloud Drive (Share → FoldForm:
   opens as a copy).
6. If FoldForm is missing from the share sheet for STEP files, say which
   other CAD or slicer apps are installed (TD-68).
7. **Privacy policy link and Home folders:** File → About FoldForm → tap
   "Privacy policy: FoldForm collects no data about you": Safari opens the
   policy on GitHub (after this branch is on `main`). On Home, the cards of
   projects saved in FoldForm's folder say *FoldForm (Files app)*, not a
   long `/var/mobile/…` path, and the *FoldForm* title and **New project**
   sit below the clock and the Dynamic Island (they used to sit under them).
8. **Loft:** sketch a rectangle on the ground; Plane → From XY, type 30;
   Sketch on the plane, a circle over the rectangle; Finish. Tap the
   rectangle, then the circle (also where the rectangle's arrow runs over
   it), then Loft: a transition piece appears. Try Straight, Apply, then
   in the Model panel change the plane's distance: the loft follows. Also
   loft from a box's top face to a plane above it (it joins the box). Is
   the Loft button easy to find, and does the order of taps feel right?
9. **The view cube (iPhone and iPad).** The cube under the **View**
   button (iPhone) or above the view buttons (iPad) turns with the model.
   Tap its **Top** face, a **Front** face, a corner: the view turns there.
   Start a sketch, arm **Line**, then drag the cube with one finger: the
   view orbits and no line is drawn; lift the finger and draw a line as
   usual. Does the cube feel big enough to hit its corners on the iPhone,
   in portrait and in landscape? Is the drag's speed right?
10. **Sketching on a face (iPhone).** Select a box's top face, Sketch:
    draw a rectangle from one corner of the face to the opposite one; the
    points lock onto the corners, edge middles and the center (the hint
    names them), and a loupe beside your finger shows where the point goes.
    Drag empty space with one finger: the view pans. Orbit with two
    fingers, then **Face on**: the view faces the sketch again. Are the
    lock-on points easy to hit, and is the loupe in the way?
11. **Before the App Store:** look through the listing texts and the twelve
   screenshots in [APP_STORE.md](APP_STORE.md), section 3, and compare them
   with the app on your devices (the screenshots are made on Windows, so
   the font differs slightly).
