// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// Everything you can do, on one card: gestures, shortcuts and tips. Opened
// with the "?" button or F1; closes on click or Esc.
Rectangle {
    id: overlay
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

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "How OpenShape works"
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton { text: "Close"; onClicked: overlay.visible = false }
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.mutedText
                    font.pixelSize: 13
                    text: "Select something and the tools for it appear next to it. Drag the arrow, or just type a "
                        + "value and press Enter. Every value accepts units and arithmetic: 25, 1in, 20+5, (10+2)*3."
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: Theme.compact ? 1 : 2 // one column on a phone
                    columnSpacing: 32
                    rowSpacing: 16

                    HelpSection {
                        title: "View"
                        rows: [
                            ["Orbit", "Drag empty space · one finger"],
                            ["Pan", "Shift+drag or middle-drag · two fingers"],
                            ["Zoom", "Wheel · pinch (toward the pointer)"],
                            ["Fit everything", "F"],
                            ["Standard views", "Iso / Top / Front / Right buttons"],
                            ["Axes", "X red, Y green, Z blue (the marker shows which way they point)"]
                        ]
                    }
                    HelpSection {
                        title: "Select"
                        rows: [
                            ["Face or edge", "Click · tap"],
                            ["Add to selection", "Shift+click · taps add on touch"],
                            ["Whole body", "Double-click · double-tap"],
                            ["Second body", "Shift+double-click"],
                            ["From the Model panel", "Click a body (Shift adds) · hover a step to see it"],
                            ["Clear", "Esc · tap empty space"]
                        ]
                    }
                    HelpSection {
                        title: "Model"
                        rows: [
                            ["New box / sketch", "B / K"],
                            ["Push or pull a flat face", "Select it, drag or type"],
                            ["Resize to an exact size", "Click a face: it shows the size to the opposite face; type the new one (+5 / -5 adds or removes)"],
                            ["Round or bevel edges", "Select edges → Fillet / Chamfer"],
                            ["Hollow out", "Face → Shell"],
                            ["Resize a hole", "Click its wall, type the new diameter"],
                            ["Remove a hole or fillet", "Select its face(s), press Delete"],
                            ["Insert for a screw", "Hole rim → Heat-set insert"],
                            ["Measure", "Select two faces or edges"],
                            ["Apply / cancel", "Enter / Esc (or ✓ / ✕)"]
                        ]
                    }
                    HelpSection {
                        title: "Bodies"
                        rows: [
                            ["Move / rotate", "Select the body → Move or Rotate; drag an arrow or ring, or type"],
                            ["Rotate about an edge", "Rotate, then click a straight edge or a hole (one ring around it), or a corner or circle (the rings move there); Center pivot goes back"],
                            ["Duplicate", "Body → Duplicate (Ctrl+D), or Duplicate in its Model panel row; drag the copy away"],
                            ["Split into bodies", "A body in separate pieces (e.g. cut in two) → Split into bodies: each piece becomes a body that still follows the history"],
                            ["Align", "Face or edge → Align, then click the face or edge to line it up with"],
                            ["Mirror", "Body → Mirror, then click a flat face or choose a plane"],
                            ["Pattern", "Body → Pattern: Linear or Circular, type the spacing, ± copy"],
                            ["Copies as bodies", "Mirror or Pattern → Separate bodies: each copy is its own body and follows the original when it changes"],
                            ["Delete a body", "Double-click it, press Delete; a body that pieces or copies are built from is hidden instead"],
                            ["Union / subtract / intersect", "Select two bodies → Union, Subtract or Intersect (Swap picks which is cut)"]
                        ]
                    }
                    HelpSection {
                        title: "Sketch"
                        rows: [
                            ["Tools", "On the left · S select · L line · R rectangle · E center rectangle · P polygon · C circle · A arc · G tangent arc · O slot · T trim"],
                            ["Center rectangle", "Click the center, then a corner (or type width, Tab, height): it stays centered"],
                            ["Polygon", "Click the center, move to the middle of a side; type the size across flats, Tab for the number of sides; − / + (or the - / + keys) change the sides"],
                            ["Slot", "Click both centers, then move or type the width"],
                            ["Trim", "Click the piece of a curve to cut away (it shows red first)"],
                            ["Round a corner", "Select the corner point → Fillet, then click R to change it"],
                            ["Offset", "Select curves → Offset, move to a side, click or type a distance"],
                            ["Mirror", "Select curves → Mirror, then click the line to mirror across (they stay mirrored)"],
                            ["Pattern", "Select curves → Pattern: click where the next copy goes (or type the spacing); Circular: click the center, type the angle; − / + or Tab for the count; Apply"],
                            ["Arc", "Click start, click end, then bend it (or type a radius)"],
                            ["Tangent arc", "Click the free end of a line or arc, then where the arc ends (or type a radius); it keeps going from there until Esc"],
                            ["Exact size while drawing", "Type, Tab to the next value, Enter"],
                            ["Stop drawing lines", "Right-click or Esc (again: leave the tool)"],
                            ["Change a dimension", "Click its label"],
                            ["Angle", "Select two lines → Angle; click the ° label to type a new one"],
                            ["Constrain", "Select 1–2 items → Parallel, Perpendicular, Equal, Tangent, Concentric, …"],
                            ["See / remove constraints", "Small glyphs beside the geometry (H, V, ∥, ⊥, =, T, …): with the Select tool click or tap one, then Delete"],
                            ["Construction curves", "Select curves → Construction (never become shapes)"],
                            ["Add to a sketch", "Select one of its shapes → Sketch, or sketch on its plane"],
                            ["Extrude / revolve", "Finish, then click inside a closed shape"],
                            ["Both sides / up to a face", "While extruding: Symmetric, or Up to face and click a face"]
                        ]
                    }
                    HelpSection {
                        title: "History"
                        rows: [
                            ["Change any step", "Model panel → click the step → edit"],
                            ["Failed step", "Shown in red with the reason; undo restores"],
                            ["Undo / redo", "Ctrl+Z / Ctrl+Y · two-finger / three-finger tap"],
                            ["Pen", "Selects and draws; fingers then only move the view"]
                        ]
                    }
                    HelpSection {
                        title: "Files"
                        rows: [
                            ["New / open / save", "Ctrl+N / Ctrl+O / Ctrl+S"],
                            ["Recent projects", "File → Open Recent"],
                            ["For printing", "File → Export STL or 3MF"],
                            ["For other CAD", "File → Export STEP"],
                            ["After a crash", "Unsaved work is kept in a recovery copy and offered at the next start (your file changes only when you save)"],
                            ["Preferences", "File → Preferences… (Ctrl+,): units, grid snapping, recovery copies"]
                        ]
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
                Text {
                    text: modelData[0]
                    color: Theme.text
                    font.pixelSize: 13
                    Layout.preferredWidth: Theme.compact ? 110 : 150
                    wrapMode: Text.WordWrap
                }
                Text {
                    text: modelData[1]
                    color: Theme.mutedText
                    font.pixelSize: 13
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
