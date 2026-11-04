// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import OpenShape

// The kept measurements (MeasureController.measurements, newest first):
// chips of name and value in a row along the bottom of a narrow window, a
// list in a card at the side of a wide one. A tap opens the measurement's
// card (Use, Add to project, Rename, Delete: MeasureScreen); one kept in the
// open project is marked (a check on a chip, its name in the list), and the
// help button opens "Measurements in the design" (docs/MEASURE.md).
Item {
    id: tray

    required property MeasureController m
    property bool vertical: false
    // The name a kept measurement has in the open project ("" when none).
    property var projectName: function(entry) { return "" }
    signal picked(string id)
    signal helpRequested()

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
            width: trayLabel.implicitWidth + rowHelp.width + 8
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
            HelpButton {
                id: rowHelp
                objectName: "measureTrayHelp"
                anchors.left: trayLabel.right
                anchors.verticalCenter: parent.verticalCenter
                visible: !tray.vertical
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
                // In the open project: a name any value box takes.
                LineIcon {
                    objectName: "measureChipInProject_" + chip.index
                    visible: tray.projectName(chip.modelData) !== ""
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 6
                    name: "check"
                    size: 12
                    color: "#1A8F57"
                }
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
            anchors.right: listHelp.left
            anchors.rightMargin: 4
            anchors.baseline: listTitle.baseline
            visible: tray.items.length > 0
            text: tray.items.length
            font.pixelSize: 13
            font.features: ({ "tnum": 1 })
            color: Theme.mutedText
        }
        HelpButton {
            id: listHelp
            objectName: "measureTrayHelp"
            visible: tray.vertical
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: listTitle.verticalCenter
        }
        Text {
            visible: tray.items.length === 0
            x: 16
            y: listTitle.y + listTitle.height + 10
            width: parent.width - 32
            text: "Keep a reading and it waits here. Tap it to use it in the design."
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
                        anchors { left: parent.left; leftMargin: 16; right: projectText.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
                        text: rowItem.modelData.name
                        textFormat: Text.PlainText
                        font.pixelSize: 14
                        color: Theme.text
                        elide: Text.ElideRight
                    }
                    // Its name in the open project, to type in a value box.
                    Text {
                        id: projectText
                        objectName: "measureChipInProject_" + rowItem.index
                        anchors { right: valueText.left; rightMargin: 10; verticalCenter: parent.verticalCenter }
                        readonly property string name: tray.projectName(rowItem.modelData)
                        visible: name !== ""
                        width: visible ? Math.min(implicitWidth, list.width * 0.36) : 0
                        text: name
                        textFormat: Text.PlainText
                        font.pixelSize: 12
                        color: "#1A8F57"
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

    // "?": how the kept measurements reach the design.
    component HelpButton: AbstractButton {
        id: help
        implicitWidth: Theme.touch ? 40 : 32
        implicitHeight: Theme.touch ? 40 : 32
        focusPolicy: Qt.NoFocus
        onClicked: tray.helpRequested()
        background: Rectangle { radius: width / 2; color: help.pressed ? Theme.pressed : "transparent" }
        contentItem: Item { LineIcon { anchors.centerIn: parent; name: "help"; size: 17; color: Theme.accent } }
    }
}
