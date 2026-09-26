# Manual test pass

What the owner is asked to try by hand (last updated 2026-09-25). Anything
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
11. **Home and STEP.** Start OpenShape from the shortcut: Home shows your
    recent projects with pictures (save a project once to give it one).
    Tap a card to open it; ⋯ (or a right click) → Remove from list. File →
    Home comes back to it. Import STEP… with a part from another program
    (Onshape, Fusion, FreeCAD, a download): the bodies should keep their
    size and names; push/pull a face and round an edge of it, save, reopen.

## On the iPad

See [IPAD.md](IPAD.md), "What to test on the iPad".
