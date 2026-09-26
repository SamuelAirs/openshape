# Manual test pass

What the owner is asked to try by hand (last updated 2026-09-26). Anything
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
      body. Now delete the bigger one: it is hidden instead, and the
      message says which body is built from it.
    - Separate bodies: select a body, Mirror or Pattern, switch on
      "Separate bodies", apply: each copy is its own body (click them one
      by one). Push/pull the original: the copies follow.
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
      up (raised) or down (cut in), try Size `5`, 90°, Deboss; Enter. In
      the Model panel, click the Text step and change the words. Print a
      label: are 5 mm capitals 0.6-1 mm deep readable?
15. **The documentation.** Follow the Quick start in README.md step by step
    (the mounting plate with two holes), and skim docs/USER_GUIDE.md: does
    everything work as written? Is anything you use missing? F1 → the
    link under the first paragraph opens the guide on GitHub.

## The Windows installer (optional)

`scripts/windows/test-installer.ps1` (silent) and
`scripts/windows/test-installer-dialogs.ps1` (clicks through the dialogs)
check the installer automatically; this is the real thing, once, by hand.
Use `OpenShape-<version>-windows-x64-setup.exe` from a GitHub Release (or
`dist/` after BUILDING.md, "Release").

1. **Install.** Windows SmartScreen warns (the installer is not signed yet):
   "More info" → "Run anyway". No administrator prompt should appear. Click
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
iPhone".
