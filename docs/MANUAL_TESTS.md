# Manual test pass

What the owner is asked to try by hand (last updated 2026-09-27). Anything
odd: a screenshot and a sentence is plenty. Starting the app with
`OPENSHAPE_LOG=debug` lets Claude read the log afterwards
(`%LOCALAPPDATA%\OpenShape\OpenShape\logs\openshape.log`).

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
5. **Axes.** Blue Z axis through the origin and the X/Y/Z marker above the
   view buttons; orbit and watch the marker turn.
6. **About.** File → About OpenShape.
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
    Home comes back to it. Import STEP… with a part from another program
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
18. **Licenses (for the App Store release).** File → About OpenShape →
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

## On the iPad and iPhone

See [IPAD.md](IPAD.md), "What to test on the iPad" and "What to test on the
iPhone". New in the build after 2026-09-27 (iPad 12-18, iPhone 13-17):

1. **Share an STL to the slicer:** File → Export STL: the share sheet opens
   (on the iPad pointing at File); choose the slicer app, then AirDrop a
   3MF to another device; close the sheet once without choosing (no
   message; the file is in OpenShape → Exports).
2. **Share Project…:** on a new project it asks for a name first, then the
   sheet; send the project to yourself by Mail.
3. **Open a STEP file from the Files app:** Share → OpenShape: a new project
   with the file's bodies (unsaved changes are asked about first).
4. **Open a STEP file and a project from Mail:** touch and hold the
   attachment → OpenShape; the project opens as a copy in OpenShape's
   folder; no "Inbox" folder is left in OpenShape's folder afterwards.
5. **Open a project from the Files app:** tap one in On My iPhone / iPad →
   OpenShape (opens directly), and one in iCloud Drive (Share → OpenShape:
   opens as a copy).
6. If OpenShape is missing from the share sheet for STEP files, say which
   other CAD or slicer apps are installed (TD-68).
7. **Privacy policy link and Home folders:** File → About OpenShape → tap
   "Privacy policy: OpenShape collects no data about you": Safari opens the
   policy on GitHub (after this branch is on `main`). On Home, the cards of
   projects saved in OpenShape's folder say *OpenShape (Files app)*, not a
   long `/var/mobile/…` path, and the *OpenShape* title and **New project**
   sit below the clock and the Dynamic Island (they used to sit under them).
8. **Loft:** sketch a rectangle on the ground; Plane → From XY, type 30;
   Sketch on the plane, a circle over the rectangle; Finish. Tap the
   rectangle, then the circle (also where the rectangle's arrow runs over
   it), then Loft: a transition piece appears. Try Straight, Apply, then
   in the Model panel change the plane's distance: the loft follows. Also
   loft from a box's top face to a plane above it (it joins the box). Is
   the Loft button easy to find, and does the order of taps feel right?
9. **Before the App Store:** look through the listing texts and the twelve
   screenshots in [APP_STORE.md](APP_STORE.md), section 3, and compare them
   with the app on your devices (the screenshots are made on Windows, so
   the font differs slightly).
