// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import OpenShape

// How a reading of the fold or the level stands: moving ("Hold still"),
// steady, or held (the value kept once the phone is lifted to be read),
// with a short pulse when a new hold comes.
Rectangle {
    id: pill

    property bool steady: false
    property bool held: false
    property string heldText: ""

    readonly property color tone: held ? "#1A8F57" : steady ? Theme.accent : Theme.mutedText

    implicitWidth: row.implicitWidth + 28
    implicitHeight: 34
    radius: height / 2
    color: held ? "#E5F5EC" : steady ? Theme.menuHighlight : Theme.track
    Behavior on color { ColorAnimation { duration: 160 } }

    function pulse() { pulseAnimation.restart() }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 7
        LineIcon {
            anchors.verticalCenter: parent.verticalCenter
            name: pill.held ? "check" : "hold"
            size: 16
            color: pill.tone
        }
        Text {
            objectName: pill.objectName !== "" ? pill.objectName + "Text" : ""
            anchors.verticalCenter: parent.verticalCenter
            text: pill.held ? (pill.heldText !== "" ? "Held · " + pill.heldText : "Held")
                 : pill.steady ? "Steady" : "Hold still to measure"
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
