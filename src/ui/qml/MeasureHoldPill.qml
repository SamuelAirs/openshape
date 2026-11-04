// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import OpenShape

// How a reading of the fold or the level stands, so the owner knows when it
// is held: moving ("Hold still to measure"), keeping still (the pill fills
// over the steady time, "Hold still…"), held (green, "Held · 92.5°", with a
// tap felt and a short pulse). Moved away from a hold, the held value stays
// (and the pill green) until the phone keeps still somewhere new: then it
// fills again toward the next hold.
Rectangle {
    id: pill

    property bool steady: false
    property bool held: false
    property string heldText: ""
    // How far the next hold has come (0..1), and whether a still stretch
    // makes one (MeasureController.foldProgress / foldArmed).
    property real progress: 0
    property bool armed: true

    readonly property bool filling: armed && progress > 0.05
    // A held value, and no new hold on its way: the green state.
    readonly property bool showsHold: held && !filling
    readonly property string text: showsHold ? (heldText !== "" ? "Held · " + heldText : "Held")
        : filling ? "Hold still…" : "Hold still to measure"
    readonly property color tone: showsHold ? "#1A8F57" : filling ? Theme.accent : Theme.mutedText

    implicitWidth: row.implicitWidth + 28
    implicitHeight: 34
    radius: height / 2
    color: showsHold ? "#E5F5EC" : Theme.track
    Behavior on color { ColorAnimation { duration: 160 } }

    function pulse() { pulseAnimation.restart() }

    // The fill: how long the reading has kept still, toward the hold.
    Item {
        anchors.fill: parent
        visible: !pill.showsHold
        clip: true
        Rectangle {
            objectName: pill.objectName !== "" ? pill.objectName + "Fill" : ""
            width: pill.width * (pill.filling ? Math.max(0, Math.min(1, pill.progress)) : 0)
            height: pill.height
            radius: pill.radius
            color: Theme.menuHighlight
            Behavior on width { NumberAnimation { duration: 90 } }
        }
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 7
        LineIcon {
            anchors.verticalCenter: parent.verticalCenter
            name: pill.showsHold ? "check" : "hold"
            size: 16
            color: pill.tone
        }
        Text {
            objectName: pill.objectName !== "" ? pill.objectName + "Text" : ""
            anchors.verticalCenter: parent.verticalCenter
            text: pill.text
            font.pixelSize: 13
            font.weight: Font.DemiBold
            font.features: ({ "tnum": 1 })
            color: pill.tone
        }
    }

    SequentialAnimation {
        id: pulseAnimation
        NumberAnimation { target: pill; property: "scale"; to: 1.12; duration: 110; easing.type: Easing.OutQuad }
        NumberAnimation { target: pill; property: "scale"; to: 1.0; duration: 260; easing.type: Easing.OutBack }
    }
}
