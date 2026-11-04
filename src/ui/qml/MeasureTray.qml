// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import OpenShape

// The kept measurements (MeasureController.measurements, newest first):
// chips of name and value in a row along the bottom of a narrow window, a
// list in a card at the side of a wide one. A tap opens the measurement's
// card (Use, Rename, Delete: MeasureScreen).
Item {
    id: tray

    required property MeasureController m
    property bool vertical: false
    signal picked(string id)

    readonly property var items: m.measurements
    implicitHeight: vertical ? 200 : (Theme.touch ? 58 : 54)

    // ---- a row of chips
    ListView {
        id: row
        objectName: "measureTrayRow"
        visible: !tray.vertical
        anchors.fill: parent
        orientation: ListView.Horizontal
        spacing: 8
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: tray.vertical ? [] : tray.items
        // Room for the chips' shadows.
        topMargin: 2
        bottomMargin: 4
        header: Item {
            width: trayLabel.implicitWidth + 10
            height: row.height - 6
            Text {
                id: trayLabel
                anchors.verticalCenter: parent.verticalCenter
                text: "Kept"
                font.pixelSize: 12
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.6
                color: Theme.shortcutText
            }
        }
        delegate: AbstractButton {
            id: chip
            required property var modelData
            required property int index
            objectName: "measureChip_" + index
            height: row.height - 6
            width: Math.min(200, Math.max(84, Math.max(chipName.implicitWidth, chipValue.implicitWidth) + 28))
            focusPolicy: Qt.NoFocus
            onClicked: tray.picked(modelData.id)
            background: MeasureCard {
                radius: 12
                color: chip.pressed ? Theme.pressed : Theme.card
            }
            contentItem: Item {
                Column {
                    id: chipColumn
                    anchors.verticalCenter: parent.verticalCenter
                    x: 14
                    width: parent.width - 28
                    spacing: 1
                    Text {
                        id: chipName
                        width: parent.width
                        text: chip.modelData.name
                        textFormat: Text.PlainText
                        font.pixelSize: 11
                        color: Theme.mutedText
                        elide: Text.ElideRight
                    }
                    Text {
                        id: chipValue
                        width: parent.width
                        text: chip.modelData.text
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        font.features: ({ "tnum": 1 })
                        color: Theme.text
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    // ---- a list in a card
    MeasureCard {
        anchors.fill: parent
        visible: tray.vertical
        clip: true

        Text {
            id: listTitle
            x: 16
            y: 14
            text: "Kept measurements"
            font.pixelSize: 13
            font.weight: Font.DemiBold
            color: Theme.text
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.baseline: listTitle.baseline
            visible: tray.items.length > 0
            text: tray.items.length
            font.pixelSize: 13
            font.features: ({ "tnum": 1 })
            color: Theme.mutedText
        }
        Text {
            visible: tray.items.length === 0
            x: 16
            y: listTitle.y + listTitle.height + 10
            width: parent.width - 32
            text: "Keep a reading and it waits here, ready to type into any value box."
            font.pixelSize: 13
            color: Theme.mutedText
            wrapMode: Text.WordWrap
        }
        ListView {
            id: list
            objectName: "measureTrayList"
            anchors { fill: parent; topMargin: listTitle.y + listTitle.height + 8; bottomMargin: 6 }
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: tray.vertical ? tray.items : []
            delegate: AbstractButton {
                id: rowItem
                required property var modelData
                required property int index
                objectName: "measureChip_" + index
                width: list.width
                height: Theme.touch ? 50 : 44
                focusPolicy: Qt.NoFocus
                hoverEnabled: true
                onClicked: tray.picked(modelData.id)
                background: Rectangle {
                    color: rowItem.pressed ? Theme.menuPressed : rowItem.hovered && !Theme.touch ? Theme.menuHighlight : "transparent"
                    Rectangle {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 16; rightMargin: 16 }
                        height: 1
                        color: Theme.hairline
                        visible: rowItem.index < list.count - 1
                    }
                }
                contentItem: Item {
                    Text {
                        anchors { left: parent.left; leftMargin: 16; right: valueText.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
                        text: rowItem.modelData.name
                        textFormat: Text.PlainText
                        font.pixelSize: 14
                        color: Theme.text
                        elide: Text.ElideRight
                    }
                    Text {
                        id: valueText
                        anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                        text: rowItem.modelData.text
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        font.features: ({ "tnum": 1 })
                        color: Theme.text
                    }
                }
            }
        }
    }
}
