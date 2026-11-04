// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import OpenShape

// Above the fold's and the level's big number: whether it is the held value
// (it stays while the phone is lifted to be read) or the live one.
Rectangle {
    id: tag

    property bool held: false

    implicitWidth: row.implicitWidth + 18
    implicitHeight: 22
    radius: height / 2
    color: held ? "#E5F5EC" : Theme.track
    Behavior on color { ColorAnimation { duration: 160 } }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 4
        LineIcon {
            anchors.verticalCenter: parent.verticalCenter
            visible: tag.held
            name: "check"
            size: 12
            color: "#1A8F57"
        }
        Text {
            objectName: tag.objectName !== "" ? tag.objectName + "Text" : ""
            anchors.verticalCenter: parent.verticalCenter
            text: tag.held ? "HELD" : "LIVE"
            font.pixelSize: 11
            font.weight: Font.DemiBold
            font.letterSpacing: 0.8
            color: tag.held ? "#1A8F57" : Theme.mutedText
        }
    }
}
