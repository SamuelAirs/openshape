// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// File → Preferences… (Ctrl+,): a few settings that apply at once and are
// remembered. Closes on Esc, Close or a click outside.
Rectangle {
    id: overlay

    required property AppController app

    color: "#66000000"
    visible: false
    focus: visible

    function open() { visible = true }

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
        opacity: 1
        width: Math.min(safeRect.width - (Theme.compact ? 16 : 32), 560)
        height: Math.min(safeRect.height - (Theme.compact ? 16 : 32), content.implicitHeight + 2 * (Theme.compact ? 16 : 24))

        Flickable {
            anchors.fill: parent
            anchors.margins: Theme.compact ? 16 : 24
            contentHeight: content.implicitHeight
            clip: true
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: content
                width: parent.width
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "Preferences"
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton { objectName: "preferencesClose"; text: "Close"; onClicked: overlay.visible = false }
                }

                SectionLabel { text: "Units for new documents"; Layout.leftMargin: 0; Layout.topMargin: 6 }
                Choice {
                    name: "prefUnit"
                    options: [{ value: "mm", label: "Millimeters" }, { value: "in", label: "Inches" }]
                    current: overlay.app.defaultUnit
                    onChosen: (value) => overlay.app.defaultUnit = value
                }
                Note { text: "Any value can still be typed in any unit, e.g. 1in or 25mm." }

                SectionLabel { text: "Sketches"; Layout.leftMargin: 0; Layout.topMargin: 6 }
                Choice {
                    name: "prefGridSnap"
                    options: [{ value: true, label: "Snap to grid" }, { value: false, label: "Free" }]
                    current: overlay.app.sketchGridSnap
                    onChosen: (value) => overlay.app.sketchGridSnap = value
                }
                Note { text: "Points you draw away from other geometry round to the grid, which follows the zoom. Points, midpoints and horizontal/vertical still snap either way." }

                SectionLabel { text: "Recovery copies"; Layout.leftMargin: 0; Layout.topMargin: 6 }
                Choice {
                    name: "prefRecovery"
                    options: [{ value: 0, label: "Off" }, { value: 30, label: "30 s" },
                              { value: 60, label: "1 min" }, { value: 300, label: "5 min" }]
                    current: overlay.app.recoveryInterval
                    onChosen: (value) => overlay.app.recoveryInterval = value
                }
                Note {
                    text: overlay.app.recoveryInterval === 0
                          ? "Off: work that is not saved is lost if OpenShape closes unexpectedly."
                          : "While there are unsaved changes, OpenShape keeps a copy in its own folder, a few seconds after "
                            + "you stop editing and at least this often, and offers it after a crash. Your file changes only when you save."
                }
            }
        }
    }

    // A row of mutually exclusive buttons ("segments"), big enough for touch.
    component Choice: RowLayout {
        id: choice
        property string name
        property var options: []
        property var current
        signal chosen(var value)
        spacing: 4
        Repeater {
            model: choice.options
            delegate: ActionButton {
                id: segment
                required property var modelData
                objectName: choice.name + "_" + modelData.value
                text: modelData.label
                checked: choice.current === modelData.value
                background: Rectangle {
                    radius: 8
                    color: segment.checked ? Theme.checked : segment.pressed ? Theme.pressed
                         : segment.hovered ? Theme.hover : Theme.panel
                    border.color: segment.checked ? Theme.accent : Theme.panelBorder
                }
                onClicked: choice.chosen(segment.modelData.value)
            }
        }
    }

    component Note: Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.mutedText
        font.pixelSize: 12
    }
}
