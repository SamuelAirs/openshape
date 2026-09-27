// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// Everything you can do, on one card: gestures, shortcuts and tips (in the
// touch layout: taps and gestures only). Opened with the "?" button or F1;
// closes on a click or tap, or Esc.
Rectangle {
    id: overlay
    // Saving as on an iPhone or iPad (Main.qml: app.savesToAppFolder).
    property bool appFolder: false

    color: "#66000000"
    visible: false
    focus: visible

    function toggle() { visible = !visible }

    Keys.onEscapePressed: visible = false
    MouseArea {
        anchors.fill: parent
        onClicked: overlay.visible = false
        onWheel: (wheel) => wheel.accepted = true
    }

    // The card stays clear of a phone's Dynamic Island, rounded corners and
    // home indicator (Theme.safe*; zero on the desktop).
    Item {
        id: safeRect
        anchors { fill: parent; topMargin: Theme.safeTop; rightMargin: Theme.safeRight
                  bottomMargin: Theme.safeBottom; leftMargin: Theme.safeLeft }
    }

    Panel {
        anchors.centerIn: safeRect
        opacity: 1 // text on text: no translucency here
        width: Math.min(safeRect.width - (Theme.compact ? 16 : 48), 760)
        height: Math.min(safeRect.height - (Theme.compact ? 16 : 48), content.implicitHeight + 2 * (Theme.compact ? 16 : 24))

        Flickable {
            anchors.fill: parent
            anchors.margins: Theme.compact ? 16 : 24
            contentHeight: content.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: content
                width: parent.width
                spacing: 16

                // The title wraps in a narrow window (a phone), so Close stays on screen.
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: "How OpenShape works"
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                        color: Theme.text
                        wrapMode: Text.WordWrap
                    }
                    ActionButton { objectName: "helpClose"; text: "Close"; onClicked: overlay.visible = false }
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.mutedText
                    font.pixelSize: 13
                    text: Theme.touch
                          ? "Select something and the tools for it appear next to it. Drag the arrow, or tap the value, "
                            + "type a new one and tap ✓. Every value accepts units and arithmetic: 25, 1in, 20+5, (10+2)*3."
                          : "Select something and the tools for it appear next to it. Drag the arrow, or just type a "
                            + "value and press Enter. Every value accepts units and arithmetic: 25, 1in, 20+5, (10+2)*3."
                }

                // The whole line is the link (so a tap anywhere on it opens the
                // guide); as wide as its text, wrapping on narrow windows.
                Text {
                    objectName: "helpGuideLink"
                    readonly property string url: "https://github.com/SamuelAirs/openshape/blob/main/docs/USER_GUIDE.md"
                    Layout.maximumWidth: content.width
                    wrapMode: Text.WordWrap
                    font.pixelSize: 13
                    textFormat: Text.StyledText
                    linkColor: Theme.accent
                    onLinkActivated: (link) => Qt.openUrlExternally(link)
                    text: "<a href=\"" + url + "\">Step by step, with pictures: the user guide "
                        + "(github.com/SamuelAirs/openshape, docs/USER_GUIDE.md)</a>"
                }

                // Each row: what, how with mouse and keyboard, and (third) how by
                // touch when that differs. The touch layout (Theme.touch) shows
                // the touch text: taps and gestures, never Shift, Esc, Enter,
                // shortcuts, hovering or the scroll wheel (tests check it).
                GridLayout {
                    Layout.fillWidth: true
                    columns: Theme.compact ? 1 : 2 // one column on a phone
                    columnSpacing: 32
                    rowSpacing: 16

                    HelpSection {
                        title: "View"
                        rows: [
                            ["Orbit", "Drag empty space · one finger", "Drag empty space with one finger"],
                            ["Pan", "Shift+drag or middle-drag · two fingers", "Drag with two fingers"],
                            ["Zoom", "Wheel · pinch (toward the pointer)", "Pinch with two fingers"],
                            ["Fit everything", "F", "Fit (in the View menu on a phone)"],
                            ["Standard views", "Iso / Top / Front / Right buttons", "Iso / Top / Front / Right (in the View menu on a phone)"],
                            ["Axes", "X red, Y green, Z blue (the marker shows which way they point)"],
                            ["Display unit", "The mm / in button; any value can be typed in any unit, e.g. 1in or 25mm"]
                        ]
                    }
                    HelpSection {
                        title: "Select"
                        rows: [
                            ["Face or edge", "Click · tap", "Tap"],
                            ["Add to selection", "Shift+click · taps add on touch", "Tap more: taps add"],
                            ["Whole body", "Double-click · double-tap", "Double-tap"],
                            ["Second body", "Shift+double-click", "Double-tap it too"],
                            ["From the Model panel", "Click a body (Shift adds) · hover a step to see it",
                             "Tap a body (taps add) · tap a step to see it and change it (the Model button on a phone)"],
                            ["Clear", "Esc · tap empty space", "Tap empty space"]
                        ]
                    }
                    HelpSection {
                        title: "Model"
                        rows: [
                            ["New box / sketch", "B / K", "Box / Sketch in the tools (along the bottom on a phone)"],
                            ["Push or pull a flat face", "Select it, drag or type"],
                            ["Resize to an exact size", "Click a face: it shows the size to the opposite face; type the new one (+5 / -5 adds or removes)",
                             "Tap a face: it shows the size to the opposite face; tap the value and type the new one (+5 / -5 adds or removes)"],
                            ["Round or bevel edges", "Select edges → Fillet / Chamfer"],
                            ["Hollow out", "Face → Shell"],
                            ["Resize a hole", "Click its wall, type the new diameter", "Tap its wall, type the new diameter"],
                            ["Remove a hole or fillet", "Select its face(s), press Delete", "Select its face(s) → Delete face"],,
                            ["Holes for screws", "Flat face → Hole: click or tap where each goes (snaps to the center and edge middles, lines up with the others); M2–M6 with Close fit / Normal fit (ISO 273) or Tap; Through all or a depth; Counterbore / Countersink",
                             "Flat face → Hole: tap where each goes (snaps to the center and edge middles, lines up with the others); M2–M6 with Close fit / Normal fit (ISO 273) or Tap; Through all or a depth; Counterbore / Countersink"],
                            ["Exact hole positions", "Hole tool: X / Y (Tab) from the face's corner, or From last hole; click a hole to pick it again, Remove hole drops it",
                             "Hole tool: X / Y (the next field) from the face's corner, or From last hole; tap a hole to pick it again, Remove hole drops it"],
                            ["Insert for a screw", "Hole rim → Heat-set insert"],
                            ["Seat for a screw head", "Hole rim → Counterbore or Countersink, pick M2–M6 or type the diameter; a counterbore's arrow into the hole sets its depth"],
                            ["Text on a face", "Flat face → Text: type the words (Noto Sans; digits typed go to the words too; Ctrl+Backspace erases a word), click where they go (snaps to the center and edge middles); drag the arrow out to raise them (emboss) or in to cut them (deboss); Depth, Size (the height of capital letters) or Angle, then type its value; 0° / 90° / 180° / 270°; change the words later in the Model panel",
                             "Flat face → Text: type the words (Noto Sans), tap where they go (snaps to the center and edge middles); drag the arrow out to raise them (emboss) or in to cut them (deboss), or Emboss / Deboss; tap Depth, Size (the height of capital letters) or Angle to type its value; 0° / 90° / 180° / 270°; change the words later in the Model panel"],
                            ["Holes that fit when printed","Screw sizes add the hole allowance (0.2 mm unless changed in Preferences) to clearance holes, counterbores and countersinks; Tap and heat-set insert sizes already assume printing; typed sizes are used exactly"],
                            ["Measure", "Select two faces or edges"],
                            ["Apply / cancel", "Enter / Esc (or ✓ / ✕)", "✓ / ✕ beside the value"],
                            ["When a step can't be done", "The red text says why and what to try, e.g. the largest radius that fits"]
                        ]
                    }
                    HelpSection {
                        title: "Bodies"
                        rows: [
                            ["Move / rotate", "Select the body → Move or Rotate; drag an arrow or ring, or type"],
                            ["Rotate about an edge", "Rotate, then click a straight edge or a hole (one ring around it), or a corner or circle (the rings move there); Center pivot goes back",
                             "Rotate, then tap a straight edge or a hole (one ring around it), or a corner or circle (the rings move there); Center pivot goes back"],
                            ["Duplicate", "Body → Duplicate (Ctrl+D), or Duplicate in its Model panel row; drag the copy away",
                             "Body → Duplicate, or Duplicate in its Model panel row; drag the copy away"],
                            ["Split into bodies", "A body in separate pieces (e.g. cut in two) → Split into bodies: each piece becomes a body of its own, with a copy of the history"],
                            ["Align", "Face or edge → Align, then click the face or edge to line it up with",
                             "Face or edge → Align, then tap the face or edge to line it up with (empty space gives up)"],
                            ["Mirror", "Body → Mirror, then click a flat face or choose a plane",
                             "Body → Mirror, then tap a flat face or choose a plane (empty space gives up)"],
                            ["Pattern", "Body → Pattern: Linear or Circular, type the spacing, ± copy"],
                            ["Copies as bodies", "Mirror or Pattern: copies that touch neither the original nor each other become bodies of their own (independent: changing one changes no other); Separate bodies switches it"],
                            ["Delete a body", "Double-click it, press Delete; a body another body is built from is hidden instead",
                             "Double-tap it → Delete; a body another body is built from is hidden instead"],
                            ["Union / subtract / intersect", "Select two bodies → Union, Subtract or Intersect (Swap picks which is cut)"]
                        ]
                    }
                    HelpSection {
                        title: "Sketch"
                        rows: [
                            ["Tools", "On the left · S select · L line · R rectangle · E center rectangle · P polygon · C circle · A arc · G tangent arc · O slot · T trim",
                             "On the left (along the bottom on a phone): Line, Rectangle, Center rectangle, Polygon, Circle, Arc, Tangent arc, Slot, Select, Trim"],
                            ["Center rectangle", "Click the center, then a corner (or type width, Tab, height): it stays centered",
                             "Tap the center, then a corner: it stays centered"],
                            ["Polygon", "Click the center, move to the middle of a side; type the size across flats, Tab for the number of sides; − / + (or the - / + keys) change the sides",
                             "Tap the center, then the middle of a side; − / + change the sides"],
                            ["Slot", "Click both centers, then move or type the width", "Tap both centers, then tap to set the width"],
                            ["Trim", "Click the piece of a curve to cut away (it shows red first)", "Tap the piece of a curve to cut away"],
                            ["Round a corner", "Select the corner point → Fillet, then click R to change it", "Select the corner point → Fillet, then tap R to change it"],
                            ["Offset", "Select curves → Offset, move to a side, click or type a distance", "Select curves → Offset, then tap on the side to offset to"],
                            ["Mirror", "Select curves → Mirror, then click the line to mirror across (they stay mirrored)",
                             "Select curves → Mirror, then tap the line to mirror across (they stay mirrored)"],
                            ["Pattern", "Select curves → Pattern: click where the next copy goes (or type the spacing); Circular: click the center, type the angle; − / + or Tab for the count; Apply",
                             "Select curves → Pattern: tap where the next copy goes; Circular: tap the center; − / + for the count; Apply"],
                            ["Arc", "Click start, click end, then bend it (or type a radius)", "Tap start, tap end, then tap a point the arc passes through"],
                            ["Tangent arc", "Click the free end of a line or arc, then where the arc ends (or type a radius); it keeps going from there until Esc",
                             "Tap the free end of a line or arc, then where the arc ends; it keeps going until you tap Tangent arc again"],
                            ["Exact size while drawing", "Type, Tab to the next value, Enter", "Draw it, then tap a dimension and type the value"],
                            ["Stop drawing lines", "Right-click or Esc (again: leave the tool)", "Tap the Line tool again"],
                            ["Change a dimension", "Click its label", "Tap its label"],
                            ["Angle", "Select two lines → Angle; click the ° label to type a new one", "Select two lines → Angle; tap the ° label to type a new one"],
                            ["Constrain", "Select 1–2 items → Parallel, Perpendicular, Equal, Tangent, Concentric, …"],
                            ["See / remove constraints", "Small glyphs beside the geometry (H, V, ∥, ⊥, =, T, …): with the Select tool click or tap one, then Delete",
                             "Small glyphs beside the geometry (H, V, ∥, ⊥, =, T, …): with the Select tool tap one → Delete constraint"],
                            ["Construction curves", "Select curves → Construction (never become shapes)"],
                            ["Add to a sketch", "Select one of its shapes → Sketch, or sketch on its plane"],
                            ["Extrude / revolve", "Finish, then click inside a closed shape", "Finish, then tap inside a closed shape"],
                            ["Both sides / up to a face", "While extruding: Symmetric, or Up to face and click a face", "While extruding: Symmetric, or Up to face and tap a face"],
                            ["Tapered walls (draft)", "While extruding: Draft, type the angle (positive narrows away from the sketch); change it later in the Model panel"]
                        ]
                    }
                    HelpSection {
                        title: "History"
                        rows: [
                            ["Change any step", "Model panel → click the step → edit", "Model panel (the Model button on a phone) → tap the step → edit"],
                            ["Failed step", "Shown in red with the reason; undo restores"],
                            ["Undo / redo", "Ctrl+Z / Ctrl+Y · two-finger / three-finger tap", "Two-finger / three-finger tap, or Undo / Redo at the top"],
                            ["Pen", "The Pen switch: the pen selects and draws; fingers then only move the view (a resting hand does nothing)",
                             "The Pen switch (in the View menu on a phone): the pen selects and draws; fingers then only move the view (a resting hand does nothing)"]
                        ]
                    }
                    HelpSection {
                        title: "Files"
                        rows: (overlay.appFolder ? [
                            ["Where files go", "Projects into OpenShape's folder (the Files app: On My iPhone / iPad → OpenShape), "
                             + "by name; exports into its Exports folder. Open picks any project there"]
                        ] : []).concat([
                            ["New / open / save", "Ctrl+N / Ctrl+O / Ctrl+S", "File → New / Open… / Save"],
                            ["Recent projects", "Home (at start, or File → Home): click a project to open it; ⋯ or a right click removes it from the list · File → Open Recent",
                             "Home (at start, or File → Home): tap a project to open it; ⋯ or touch and hold removes it from the list"],
                            ["For printing", "File → Export STL or 3MF"],
                            ["For other CAD", "File → Export STEP"],
                            ["From other CAD", "File → Import STEP… (Ctrl+I): each solid becomes a body you can push, pull, round and combine; inches and meters come in at the right size",
                             "File → Import STEP…: each solid becomes a body you can push, pull, round and combine; inches and meters come in at the right size"],
                            ["After a crash", "Unsaved work is kept in a recovery copy and offered at the next start (your file changes only when you save)"],
                            ["Preferences", "File → Preferences… (Ctrl+,): units, grid snapping, recovery copies, hole allowance for 3D printing",
                             "File → Preferences…: units, grid snapping, recovery copies, hole allowance for 3D printing"]
                        ])
                    }
                }
            }
        }
    }

    component HelpSection: ColumnLayout {
        property string title
        property var rows: []
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        spacing: 6
        SectionLabel { text: parent.title; Layout.leftMargin: 0 }
        Repeater {
            model: parent.rows
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 12
                // modelData is briefly undefined while the rows are rebuilt
                // (e.g. the file rows when the app-folder mode changes).
                Text {
                    text: modelData ? modelData[0] : ""
                    color: Theme.text
                    font.pixelSize: 13
                    Layout.preferredWidth: Theme.compact ? 110 : 150
                    wrapMode: Text.WordWrap
                }
                Text {
                    // The touch text (third) in the touch layout, when there is one.
                    text: !modelData ? "" : Theme.touch && modelData.length > 2 ? modelData[2] : modelData[1]
                    color: Theme.mutedText
                    font.pixelSize: 13
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
