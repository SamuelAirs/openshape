// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// Check with a card (docs/MEASURE.md, "Scale"): a bank card's outline
// (ISO/IEC 7810 ID-1, 85.60 x 53.98 mm) drawn at the current scale; lay a
// real card on it and stretch the outline (its handle, or - / + for the
// last fraction) until the two match. Done keeps the scale for this screen
// (MeasureController.calibrate). The card lies along the window's longer
// side, its first corner fixed, so the handle follows the finger.
Rectangle {
    id: calibration

    property MeasureController m
    signal calibrated()

    readonly property real cardLongMm: 85.60
    readonly property real cardShortMm: 53.98
    readonly property real cornerMm: 3.18
    // The outline's long side in points (what Done keeps).
    property real spanPt: 300
    property string problem: ""

    function open() {
        spanPt = cardLongMm * m.pointsPerMm
        problem = ""
        visible = true
        forceActiveFocus()
    }
    function close() {
        visible = false
    }
    function nudge(points) {
        spanPt = Math.max(cardLongMm * 50 / 25.4, Math.min(cardLongMm * 300 / 25.4, spanPt + points))
    }

    visible: false
    color: Theme.background
    Keys.onEscapePressed: close()
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true
    }

    Item {
        id: safeArea
        readonly property real pad: Theme.compact ? 12 : 20
        anchors { fill: parent; topMargin: Theme.safeTop + (Theme.safeTop > 0 ? 6 : pad); rightMargin: Theme.safeRight + pad
                  bottomMargin: Theme.safeBottom + pad; leftMargin: Theme.safeLeft + pad }
    }

    // ---- top: Cancel, the title, Done
    RowLayout {
        id: top
        x: safeArea.x
        y: safeArea.y
        width: safeArea.width
        spacing: 8
        ActionButton {
            objectName: "cardCancel"
            text: "Cancel"
            outlined: true
            onClicked: calibration.close()
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: "Check with a card"
            font.pixelSize: 17
            font.weight: Font.DemiBold
            color: Theme.text
            elide: Text.ElideRight
        }
        ActionButton {
            objectName: "cardDone"
            text: "Done"
            iconName: "check"
            accent: true
            onClicked: {
                if (calibration.m.calibrate(calibration.spanPt, calibration.cardLongMm)) {
                    calibration.close()
                    calibration.calibrated()
                } else {
                    calibration.problem = "That doesn't look like a card's size on this screen. Stretch the outline to the card and try again."
                }
            }
        }
    }
    Text {
        id: instructions
        objectName: "cardInstructions"
        x: safeArea.x
        y: top.y + top.height + 10
        width: safeArea.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        font.pixelSize: 14
        color: calibration.problem !== "" ? Theme.error : Theme.mutedText
        text: calibration.problem !== "" ? calibration.problem
            : area.vertical ? "Lay a bank card on the outline, its short edge on the outline's top edge, then drag the handle to the card's other end."
                            : "Lay a bank card on the outline, its short edge on the outline's left edge, then drag the handle to the card's other end."
    }

    // ---- the card outline
    Item {
        id: area
        x: safeArea.x
        y: instructions.y + instructions.height + 16
        width: safeArea.width
        height: safeArea.y + safeArea.height - y - fine.height - 16
        // Along the longer side, so it fits a phone held either way.
        readonly property bool vertical: height > width
        readonly property real longPt: calibration.spanPt
        readonly property real shortPt: calibration.spanPt * calibration.cardShortMm / calibration.cardLongMm
        readonly property real w: vertical ? shortPt : longPt
        readonly property real h: vertical ? longPt : shortPt
        // The fixed corner: centered across, near the start along.
        readonly property real ox: vertical ? Math.max(0, (width - w) / 2) : Math.max(8, Math.min(40, width - w - 52))
        readonly property real oy: vertical ? Math.max(8, Math.min(24, height - h - 52)) : Math.max(0, (height - h) / 2)

        Rectangle {
            id: outline
            objectName: "cardOutline"
            x: area.ox
            y: area.oy
            width: area.w
            height: area.h
            radius: calibration.spanPt * calibration.cornerMm / calibration.cardLongMm
            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.06)
            border.color: Theme.accent
            border.width: 2
            // A card's chip and stripe, faintly, to say which way is which.
            Rectangle {
                x: area.vertical ? parent.width * 0.62 : parent.width * 0.12
                y: area.vertical ? parent.height * 0.12 : parent.height * 0.30
                width: area.vertical ? parent.width * 0.22 : parent.width * 0.14
                height: area.vertical ? parent.height * 0.14 : parent.height * 0.24
                radius: 4
                color: "transparent"
                border.color: Theme.accent
                border.width: 1
                opacity: 0.5
            }
            Text {
                anchors.centerIn: parent
                rotation: area.vertical ? 90 : 0
                text: "85.60 × 53.98 mm"
                font.pixelSize: 13
                font.features: ({ "tnum": 1 })
                color: Theme.accent
                opacity: 0.8
            }
        }
        // The handle at the far end.
        Item {
            id: stretch
            objectName: "cardStretch"
            width: 48
            height: 48
            x: area.vertical ? outline.x + outline.width / 2 - width / 2 : outline.x + outline.width - width / 2
            y: area.vertical ? outline.y + outline.height - height / 2 : outline.y + outline.height / 2 - height / 2
            Rectangle {
                anchors.centerIn: parent
                width: 34
                height: 34
                radius: 17
                color: Theme.accent
                LineIcon {
                    anchors.centerIn: parent
                    name: "move"
                    size: 18
                    color: "white"
                }
            }
            MouseArea {
                anchors.fill: parent
                preventStealing: true
                cursorShape: area.vertical ? Qt.SizeVerCursor : Qt.SizeHorCursor
                property real start
                property real startSpan
                onPressed: (mouse) => {
                    const p = mapToItem(area, mouse.x, mouse.y)
                    start = area.vertical ? p.y : p.x
                    startSpan = calibration.spanPt
                }
                onPositionChanged: (mouse) => {
                    const p = mapToItem(area, mouse.x, mouse.y)
                    calibration.spanPt = startSpan
                    calibration.nudge((area.vertical ? p.y : p.x) - start)
                }
            }
        }
    }

    // ---- fine steps, the scale, back to the estimate
    RowLayout {
        id: fine
        x: safeArea.x + (safeArea.width - width) / 2
        y: safeArea.y + safeArea.height - height
        spacing: 12
        RoundButton { objectName: "cardMinus"; iconName: "minus"; onClicked: calibration.nudge(-0.5) }
        ColumnLayout {
            spacing: 0
            Text {
                objectName: "cardScaleText"
                Layout.alignment: Qt.AlignHCenter
                text: (calibration.spanPt / calibration.cardLongMm).toFixed(3) + " pt per mm"
                font.pixelSize: 15
                font.weight: Font.DemiBold
                font.features: ({ "tnum": 1 })
                color: Theme.text
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                readonly property real change: calibration.m ? (calibration.spanPt / calibration.cardLongMm / calibration.m.pointsPerMm - 1) * 100 : 0
                text: Math.abs(change) < 0.05 ? (calibration.m && calibration.m.scaleCalibrated ? "as checked before" : "as estimated")
                    : (change > 0 ? "+" : "−") + Math.abs(change).toFixed(1) + " % from now"
                font.pixelSize: 12
                font.features: ({ "tnum": 1 })
                color: Theme.mutedText
            }
        }
        RoundButton { objectName: "cardPlus"; iconName: "plus"; onClicked: calibration.nudge(0.5) }
        ActionButton {
            objectName: "cardReset"
            visible: calibration.m && calibration.m.scaleCalibrated
            text: "Use the estimate"
            outlined: true
            onClicked: {
                calibration.m.resetCalibration()
                calibration.spanPt = calibration.cardLongMm * calibration.m.pointsPerMm
            }
        }
    }

    component RoundButton: AbstractButton {
        id: round
        property string iconName
        implicitWidth: 48
        implicitHeight: 48
        focusPolicy: Qt.NoFocus
        autoRepeat: true
        background: MeasureCard {
            radius: 24
            color: round.pressed ? Theme.pressed : Theme.card
        }
        contentItem: Item {
            LineIcon { anchors.centerIn: parent; name: round.iconName; size: 20; color: Theme.text }
        }
    }
}
