// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes
import OpenShape

// The Fold (docs/MEASURE.md): the iPhone Duo's hinge as a protractor and a
// caliper. Angle: the angle between the halves, huge, the bend under it and
// the phone seen from the side at that angle. Gap: the distance between the
// free edges, 2 L sin(angle / 2), with how far one degree moves it, and a
// card to calibrate L. The reading is held once steady (with a tap felt in
// the hand), so it can be read after the phone is lifted off the part.
Item {
    id: fold

    property MeasureController m
    property Item screen

    // "angle" or "gap".
    property string mode: "angle"
    property bool calibrating: false
    property string calibrationSide: "short"

    readonly property bool available: m ? m.hingeAvailable : false
    readonly property bool hasHold: m ? m.foldHasHold : false
    readonly property real liveAngle: m ? m.foldAngle : 0
    readonly property real shownAngle: hasHold ? m.foldHeld : liveAngle
    readonly property real shownGap: !m ? 0 : hasHold ? m.caliperHeldGapMm : m.caliperGapMm

    readonly property var reading: !available ? { valid: false, items: [], caption: "", source: "Fold" }
        : mode === "gap"
            ? { valid: true, caption: hasHold ? "held" : "live", source: "Caliper",
                items: [{ label: "Gap", quantity: "length", value: shownGap }] }
            : { valid: true, caption: hasHold ? "held" : "live", source: "Fold",
                items: [{ label: "Angle", quantity: "angle", value: shownAngle },
                        { label: "Bend", quantity: "angle", value: 180 - shownAngle }] }
    readonly property var actions: []
    function runAction(key) {}
    // The reading is shown huge here: a narrow window's readout leaves its number out.
    readonly property bool showsReading: available

    Connections {
        target: fold.m
        function onHeld(what) {
            if (what === "fold")
                holdPill.pulse()
        }
    }

    // ---- no hinge here
    MeasureNotice {
        objectName: "foldUnavailable"
        anchors.fill: parent
        visible: !fold.available
        iconName: "fold"
        title: "Measure angles with the fold"
        text: "On iPhone Duo, rest the two halves on two faces, inside a corner or over an edge, and the hinge reads the angle between them. Pinch a part between the free edges and it reads the gap."
        ActionButton {
            objectName: "foldToLevel"
            Layout.fillWidth: true
            text: "Use the Level instead"
            iconName: "level"
            outlined: true
            onClicked: fold.screen.select("level")
        }
    }

    // ---- the reading
    Item {
        anchors.fill: parent
        visible: fold.available

        MeasureSegmented {
            id: modes
            anchors.horizontalCenter: parent.horizontalCenter
            y: 0
            width: Math.min(parent.width, 260)
            namePrefix: "foldMode_"
            model: [{ key: "angle", label: "Angle", icon: "fold" }, { key: "gap", label: "Gap", icon: "distance" }]
            current: fold.mode
            onPicked: (key) => { fold.mode = key; fold.calibrating = false }
        }

        Item {
            id: stage
            anchors { top: modes.bottom; topMargin: 8; left: parent.left; right: parent.right; bottom: footer.top; bottomMargin: 8 }
            readonly property real drawingHeight: Math.max(0, Math.min(height - number.height - 12, Math.min(Math.min(width, 380) * 0.42, 150) + 36))

            // The number, as large as the room allows.
            Column {
                id: number
                anchors.horizontalCenter: parent.horizontalCenter
                y: Math.max(0, (parent.height - height - 12 - stage.drawingHeight) * 0.3)
                spacing: 2
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 4
                    Text {
                        id: bigText
                        objectName: "foldValue"
                        text: !fold.m ? "" : fold.mode === "gap" ? fold.m.formatNumber("length", fold.shownGap)
                                                                : fold.shownAngle.toFixed(1)
                        font.pixelSize: Math.max(44, Math.min(108, stage.height * 0.24, stage.width / 3.6))
                        font.weight: Font.DemiBold
                        font.features: ({ "tnum": 1 })
                        color: Theme.text
                    }
                    Text {
                        anchors.baseline: bigText.baseline
                        text: fold.mode === "gap" ? (fold.m ? " " + fold.m.unitSymbol("length") : "") : "°"
                        font.pixelSize: fold.mode === "gap" ? bigText.font.pixelSize * 0.32 : bigText.font.pixelSize * 0.6
                        font.weight: fold.mode === "gap" ? Font.Normal : Font.DemiBold
                        color: fold.mode === "gap" ? Theme.mutedText : Theme.text
                    }
                }
                Text {
                    objectName: "foldSubtitle"
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: !fold.m ? "" : fold.mode === "gap"
                        ? "± " + fold.m.formatValue("length", fold.m.caliperMmPerDegree) + " per degree"
                        : "Bend " + (180 - fold.shownAngle).toFixed(1) + "°"
                    font.pixelSize: 17
                    font.features: ({ "tnum": 1 })
                    color: Theme.mutedText
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: fold.hasHold && Math.abs(fold.liveAngle - fold.shownAngle) > 0.5
                    text: "Now " + (fold.mode === "gap" && fold.m ? fold.m.formatValue("length", fold.m.caliperGapMm)
                                                                   : fold.liveAngle.toFixed(1) + "°")
                    font.pixelSize: 13
                    font.features: ({ "tnum": 1 })
                    color: Theme.shortcutText
                }
            }

            // The phone seen from the side, opened to the angle.
            Item {
                id: drawing
                objectName: "foldDrawing"
                visible: !fold.calibrating
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: number.bottom
                anchors.topMargin: 12
                width: Math.min(parent.width, 380)
                height: stage.drawingHeight
                readonly property real arm: Math.min(width * 0.42, 150)
                readonly property real phi: (180 - Math.max(0, Math.min(180, fold.shownAngle))) * Math.PI / 180
                readonly property point hinge: Qt.point(width / 2, height - 14)
                readonly property point leftEnd: Qt.point(hinge.x - arm, hinge.y)
                readonly property point rightEnd: Qt.point(hinge.x + arm * Math.cos(phi), hinge.y - arm * Math.sin(phi))
                clip: true

                // The angle between the halves.
                Shape {
                    anchors.fill: parent
                    preferredRendererType: Shape.CurveRenderer
                    ShapePath {
                        strokeColor: Theme.accent
                        strokeWidth: 2
                        fillColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.10)
                        startX: drawing.hinge.x
                        startY: drawing.hinge.y
                        PathLine {
                            x: drawing.hinge.x + drawing.arm * 0.36 * Math.cos(drawing.phi)
                            y: drawing.hinge.y - drawing.arm * 0.36 * Math.sin(drawing.phi)
                        }
                        PathAngleArc {
                            centerX: drawing.hinge.x
                            centerY: drawing.hinge.y
                            radiusX: drawing.arm * 0.36
                            radiusY: drawing.arm * 0.36
                            startAngle: -drawing.phi * 180 / Math.PI
                            sweepAngle: -(180 - drawing.phi * 180 / Math.PI)
                            moveToStart: false
                        }
                        PathLine { x: drawing.hinge.x; y: drawing.hinge.y }
                    }
                    // Gap: between the free edges.
                    ShapePath {
                        strokeColor: fold.mode === "gap" ? Theme.accent : "transparent"
                        strokeWidth: 2
                        strokeStyle: ShapePath.DashLine
                        dashPattern: [3, 3]
                        fillColor: "transparent"
                        startX: drawing.leftEnd.x
                        startY: drawing.leftEnd.y - 8
                        PathLine { x: drawing.rightEnd.x; y: drawing.rightEnd.y - 8 }
                    }
                }
                // The halves: slim bodies, the screens' sides toward the angle.
                Half { rotation: 180 }
                Half { rotation: -drawing.phi * 180 / Math.PI }
                Rectangle {
                    x: drawing.hinge.x - 7
                    y: drawing.hinge.y - 7
                    width: 14
                    height: 14
                    radius: 7
                    color: Theme.card
                    border.color: Theme.text
                    border.width: 2
                }
            }

            // Calibrating the caliper with a card.
            MeasureCard {
                id: caliperCard
                objectName: "caliperPanel"
                visible: fold.calibrating
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: number.bottom
                anchors.topMargin: 12
                width: Math.min(parent.width, 400)
                height: caliperColumn.implicitHeight + 32
                ColumnLayout {
                    id: caliperColumn
                    x: 16
                    y: 16
                    width: parent.width - 32
                    spacing: 10
                    Text {
                        Layout.fillWidth: true
                        text: "Pinch a bank card between the free edges, its side square to the hinge, and hold still."
                        font.pixelSize: 14
                        color: Theme.text
                        wrapMode: Text.WordWrap
                    }
                    MeasureSegmented {
                        Layout.fillWidth: true
                        namePrefix: "caliperSide_"
                        showIcons: false
                        model: [{ key: "short", label: "Short side 53.98" }, { key: "long", label: "Long side 85.60" }]
                        current: fold.calibrationSide
                        onPicked: (key) => fold.calibrationSide = key
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ActionButton {
                            objectName: "caliperCancel"
                            Layout.fillWidth: true
                            text: "Cancel"
                            outlined: true
                            onClicked: fold.calibrating = false
                        }
                        ActionButton {
                            objectName: "caliperCalibrateDone"
                            Layout.fillWidth: true
                            text: "Calibrate"
                            accent: true
                            onClicked: {
                                if (fold.m.calibrateCaliper(fold.calibrationSide === "long" ? 85.60 : 53.98)) {
                                    fold.calibrating = false
                                    fold.screen.toast("Caliper calibrated")
                                } else {
                                    fold.screen.toast("Open the phone a little wider around the card", true)
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---- hold, and the caliper's arm
        ColumnLayout {
            id: footer
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            spacing: 8
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 8
                MeasureHoldPill {
                    id: holdPill
                    objectName: "foldHold"
                    steady: fold.m ? fold.m.foldSteady : false
                    held: fold.hasHold
                    heldText: fold.hasHold ? (fold.mode === "gap" ? fold.m.formatValue("length", fold.m.caliperHeldGapMm)
                                                                  : fold.m.foldHeld.toFixed(1) + "°") : ""
                }
                ActionButton {
                    objectName: "foldMeasureAgain"
                    visible: fold.hasHold
                    text: "Measure again"
                    outlined: true
                    onClicked: fold.m.clearHold()
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                visible: fold.mode === "gap" && !fold.calibrating
                spacing: 8
                Text {
                    objectName: "caliperArmText"
                    text: !fold.m ? "" : "Arm " + fold.m.formatValue("length", fold.m.caliperArmMm)
                                        + (fold.m.caliperCalibrated ? " · calibrated" : " · estimated")
                    font.pixelSize: 13
                    font.features: ({ "tnum": 1 })
                    color: Theme.mutedText
                }
                ActionButton {
                    objectName: "caliperCalibrate"
                    text: "Calibrate with a card"
                    iconName: "card"
                    outlined: true
                    onClicked: fold.calibrating = true
                }
                ActionButton {
                    objectName: "caliperReset"
                    visible: fold.m && fold.m.caliperCalibrated && fold.width > 560
                    text: "Reset"
                    onClicked: fold.m.resetCaliper()
                }
            }
        }
    }

    component Half: Rectangle {
        x: drawing.hinge.x
        y: drawing.hinge.y - height / 2
        width: drawing.arm
        height: 11
        radius: 5.5
        color: "#2A2F36"
        transformOrigin: Item.Left
        antialiasing: true
        Rectangle {
            // The screen side.
            x: 8
            width: parent.width - 16
            height: 3
            radius: 1.5
            y: parent.rotation === 180 ? parent.height - 3 : 0
            color: Theme.accent
            opacity: 0.9
        }
    }
}
