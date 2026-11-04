// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// The Level (docs/MEASURE.md): gravity from the motion sensors. Lying, a
// bubble and the surface's slope; standing on an edge, the edge's angle
// from level. Keep as reference stores the position: then it reads the
// angle between that and the next resting position (two surfaces). Held
// when steady, like the fold.
Item {
    id: level

    property MeasureController m
    property Item screen

    readonly property bool available: m ? m.gravityAvailable : false
    readonly property bool lying: m ? m.levelLying : true
    readonly property bool reference: m ? m.hasLevelReference : false
    readonly property bool hasHold: m ? m.levelHasHold : false
    readonly property real shown: !m ? 0 : hasHold ? m.levelHeld : m.levelValue
    readonly property string label: reference ? "To reference" : lying ? "Slope" : "Edge"

    readonly property var reading: !available ? { valid: false, items: [], caption: "", source: "Level" }
        : { valid: true, caption: hasHold ? "held" : "live", source: "Level",
            items: [{ label: level.label, quantity: "angle", value: Math.abs(shown) }] }
    readonly property var actions: []
    function runAction(key) {}
    // The reading is shown huge here: a narrow window's readout leaves its number out.
    readonly property bool showsReading: available

    Connections {
        target: level.m
        function onHeld(what) {
            if (what === "level")
                holdPill.pulse()
        }
    }

    MeasureNotice {
        objectName: "levelUnavailable"
        anchors.fill: parent
        visible: !level.available
        iconName: "level"
        title: "The level needs motion sensors"
        text: "On an iPhone or iPad, lay it on a surface for the slope, or stand it on an edge for the edge's angle from level. Keep one position and the next gives the angle between the two surfaces."
    }

    Item {
        anchors.fill: parent
        visible: level.available

        // The number.
        Column {
            id: number
            anchors.horizontalCenter: parent.horizontalCenter
            y: 4
            spacing: 2
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                Text {
                    id: bigText
                    objectName: "levelValue"
                    text: Math.abs(level.shown).toFixed(1)
                    font.pixelSize: Math.max(44, Math.min(100, level.height * 0.16, level.width / 3.8))
                    font.weight: Font.DemiBold
                    font.features: ({ "tnum": 1 })
                    color: Theme.text
                }
                Text {
                    anchors.baseline: bigText.baseline
                    text: "°"
                    font.pixelSize: bigText.font.pixelSize * 0.6
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
            }
            Text {
                objectName: "levelCaption"
                anchors.horizontalCenter: parent.horizontalCenter
                text: level.reference ? "Angle to the kept position"
                    : level.lying ? "Slope of the surface" : "Edge from level" + (level.shown > 0.05 ? " · right end up" : level.shown < -0.05 ? " · left end up" : "")
                font.pixelSize: 16
                color: Theme.mutedText
            }
        }

        // Lying: the bubble.
        Item {
            id: vial
            objectName: "levelBubbleVial"
            visible: level.lying
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: number.bottom
            anchors.topMargin: 14
            readonly property real size: Math.max(120, Math.min(parent.width - 16, footer.y - y - 14, 300))
            width: size
            height: size
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: Theme.card
                border.color: Theme.panelBorder
                border.width: 1
            }
            Repeater {
                model: [0.66, 0.33]
                Rectangle {
                    required property real modelData
                    anchors.centerIn: parent
                    width: vial.size * modelData
                    height: width
                    radius: width / 2
                    color: "transparent"
                    border.color: Theme.panelBorder
                    border.width: 1
                }
            }
            Rectangle { anchors.centerIn: parent; width: vial.size - 24; height: 1; color: Theme.hairline }
            Rectangle { anchors.centerIn: parent; width: 1; height: vial.size - 24; color: Theme.hairline }
            Rectangle {
                id: bubble
                objectName: "levelBubble"
                readonly property real travel: vial.size / 2 - width / 2 - 6
                readonly property bool centered: level.m && Math.abs(level.m.levelSlope) < 0.5
                width: Math.max(34, vial.size * 0.16)
                height: width
                radius: width / 2
                x: vial.size / 2 - width / 2 + (level.m ? level.m.bubbleX : 0) * travel
                y: vial.size / 2 - height / 2 + (level.m ? level.m.bubbleY : 0) * travel
                color: centered ? Qt.rgba(0.10, 0.56, 0.34, 0.85) : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.85)
                border.color: "white"
                border.width: 2
                Behavior on x { SmoothedAnimation { velocity: 900 } }
                Behavior on y { SmoothedAnimation { velocity: 900 } }
            }
        }

        // Standing: the edge against a level line.
        Item {
            id: horizon
            objectName: "levelHorizon"
            visible: !level.lying
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: number.bottom
            anchors.topMargin: 14
            width: Math.min(parent.width - 16, 420)
            height: Math.max(60, Math.min(footer.y - y - 14, 220))
            Rectangle {
                anchors.centerIn: parent
                width: parent.width
                height: 1
                color: Theme.panelBorder
            }
            Rectangle {
                objectName: "levelEdgeBar"
                anchors.centerIn: parent
                width: parent.width * 0.86
                height: 8
                radius: 4
                rotation: level.m ? -level.m.levelEdge : 0
                color: level.m && Math.abs(level.m.levelEdge) < 0.3 ? "#1A8F57" : Theme.accent
                antialiasing: true
                Behavior on rotation { SmoothedAnimation { velocity: 240 } }
            }
            Rectangle {
                anchors.centerIn: parent
                width: 14
                height: 14
                radius: 7
                color: Theme.card
                border.color: Theme.text
                border.width: 2
            }
        }

        ColumnLayout {
            id: footer
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            spacing: 8
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 8
                MeasureHoldPill {
                    id: holdPill
                    objectName: "levelHold"
                    steady: level.m ? level.m.levelSteady : false
                    held: level.hasHold
                    heldText: level.hasHold ? Math.abs(level.m.levelHeld).toFixed(1) + "°" : ""
                }
                ActionButton {
                    objectName: "levelMeasureAgain"
                    visible: level.hasHold
                    text: "Measure again"
                    outlined: true
                    onClicked: level.m.clearHold()
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 8
                ActionButton {
                    objectName: "levelKeepReference"
                    text: level.reference ? "Keep this position instead" : "Keep as reference"
                    iconName: "keep"
                    outlined: true
                    onClicked: level.m.keepLevelReference()
                }
                ActionButton {
                    objectName: "levelClearReference"
                    visible: level.reference
                    text: "Clear"
                    onClicked: level.m.clearLevelReference()
                }
            }
        }
    }
}
