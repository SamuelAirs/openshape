// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Layouts
import OpenShape

// A friendly card in the middle of an instrument that has nothing to work
// with here (no hinge, no motion sensors) or waits for a first step (a
// photo): an icon, a title, a few words, and buttons (its children).
Item {
    id: notice

    property string iconName
    property string title
    property string text
    default property alias buttons: buttonColumn.data

    MeasureCard {
        objectName: notice.objectName !== "" ? notice.objectName + "Card" : ""
        anchors.centerIn: parent
        width: Math.min(parent.width - 8, 420)
        height: Math.min(parent.height - 8, column.implicitHeight + 48)
        clip: true
        ColumnLayout {
            id: column
            x: 24
            y: 24
            width: parent.width - 48
            spacing: 10
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                visible: notice.height > 300
                width: 64
                height: 64
                radius: 32
                color: Theme.menuHighlight
                LineIcon {
                    anchors.centerIn: parent
                    name: notice.iconName
                    size: 32
                    color: Theme.accent
                }
            }
            Text {
                Layout.fillWidth: true
                Layout.topMargin: 4
                horizontalAlignment: Text.AlignHCenter
                text: notice.title
                font.pixelSize: 18
                font.weight: Font.DemiBold
                color: Theme.text
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: notice.text
                font.pixelSize: 14
                lineHeight: 1.15
                color: Theme.mutedText
                wrapMode: Text.WordWrap
                visible: text !== ""
            }
            ColumnLayout {
                id: buttonColumn
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 8
                visible: children.length > 0
            }
        }
    }
}
