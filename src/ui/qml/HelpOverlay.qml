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

    Panel {
        anchors.centerIn: parent
        opacity: 1 // text on text: no translucency here
        width: Math.min(parent.width - 48, 760)
        height: Math.min(parent.height - 48, content.implicitHeight + 48)

        Flickable {
            anchors.fill: parent
            anchors.margins: 24
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
                    columns: 2
                    columnSpacing: 32
                    rowSpacing: 16

                    HelpSection {
                        title: "View"
                        rows: [
                            ["Orbit", "Drag empty space · one finger"],
                            ["Pan", "Shift+drag or middle-drag · two fingers"],
                            ["Zoom", "Wheel · pinch (toward the pointer)"],
                            ["Fit everything", "F"],
                            ["Standard views", "Iso / Top / Front / Right buttons"]
                        ]
                    }
                    HelpSection {
                        title: "Select"
                        rows: [
                            ["Face or edge", "Click · tap"],
                            ["Add to selection", "Shift+click · taps add on touch"],
                            ["Whole body", "Double-click · double-tap"],
                            ["Second body", "Shift+double-click"],
                            ["Clear", "Esc · tap empty space"]
                        ]
                    }
                    HelpSection {
                        title: "Model"
                        rows: [
                            ["New box / sketch", "B / K"],
                            ["Push or pull a flat face", "Select it, drag or type"],
                            ["Round or bevel edges", "Select edges → Fillet / Chamfer"],
                            ["Hollow out", "Face → Shell"],
                            ["Insert for a screw", "Hole rim → Heat-set insert"],
                            ["Measure", "Select two faces or edges"],
                            ["Apply / cancel", "Enter / Esc (or ✓ / ✕)"]
                        ]
                    }
                    HelpSection {
                        title: "Sketch"
                        rows: [
                            ["Tools", "S select · L line · R rectangle · C circle"],
                            ["Exact size while drawing", "Type, Tab to the next value, Enter"],
                            ["Stop drawing lines", "Right-click or Esc (again: leave the tool)"],
                            ["Change a dimension", "Click its label"],
                            ["Constrain", "Select 1–2 items → Parallel, Perpendicular, Equal, Tangent, Concentric, …"],
                            ["Construction curves", "Select curves → Construction (never become shapes)"],
                            ["Add to a sketch", "Select one of its shapes → Sketch, or sketch on its plane"],
                            ["Extrude / revolve", "Finish, then click inside a closed shape"]
                        ]
                    }
                    HelpSection {
                        title: "History"
                        rows: [
                            ["Change any step", "Model panel → click the step → edit"],
                            ["Failed step", "Shown in red with the reason; undo restores"],
                            ["Undo / redo", "Ctrl+Z / Ctrl+Y"]
                        ]
                    }
                    HelpSection {
                        title: "Files"
                        rows: [
                            ["New / open / save", "Ctrl+N / Ctrl+O / Ctrl+S"],
                            ["For printing", "File → Export STL or 3MF"],
                            ["For other CAD", "File → Export STEP"]
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
                    Layout.preferredWidth: 150
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
