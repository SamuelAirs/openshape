// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import OpenShape

// A segmented control: a track with one white segment for the current
// choice (the Measure workspace's instruments, an instrument's modes).
// Each segment is a button named `namePrefix + key`, as tall as a touch
// target; icons show where every label still fits beside one.
Rectangle {
    id: seg

    // [{key, label, icon}]
    property var model: []
    property string current: ""
    property string namePrefix: "segment_"
    property bool showIcons: true
    signal picked(string key)

    readonly property int pad: 3
    readonly property real segmentWidth: model.length > 0 ? (width - 2 * pad) / model.length : 0
    readonly property bool iconsFit: showIcons && segmentWidth >= 92
    readonly property int currentIndex: {
        for (let i = 0; i < model.length; ++i)
            if (model[i].key === current)
                return i
        return -1
    }

    implicitHeight: Theme.touch ? 44 : 36
    implicitWidth: model.length * 96 + 2 * pad
    radius: Math.min(12, height / 2)
    color: Theme.track

    Rectangle {
        id: thumb
        visible: seg.currentIndex >= 0
        x: seg.pad + Math.max(0, seg.currentIndex) * seg.segmentWidth
        y: seg.pad
        width: seg.segmentWidth
        height: seg.height - 2 * seg.pad
        radius: seg.radius - seg.pad
        color: Theme.card
        Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        RectangularShadow {
            z: -1
            anchors.fill: parent
            radius: parent.radius
            offset.y: 1
            blur: 6
            spread: -1
            color: Theme.panelShadow
        }
    }

    Row {
        x: seg.pad
        Repeater {
            model: seg.model
            delegate: AbstractButton {
                id: button
                required property var modelData
                required property int index
                readonly property bool selected: seg.currentIndex === index
                objectName: seg.namePrefix + modelData.key
                width: seg.segmentWidth
                height: seg.height
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                text: modelData.label
                onClicked: seg.picked(modelData.key)
                background: Rectangle {
                    anchors.fill: parent
                    anchors.margins: seg.pad
                    radius: Math.max(0, seg.radius - seg.pad)
                    color: !button.selected && button.hovered && !Theme.touch ? Qt.rgba(1, 1, 1, 0.45) : "transparent"
                }
                contentItem: Item {
                    Row {
                        anchors.centerIn: parent
                        spacing: 6
                        LineIcon {
                            visible: seg.iconsFit && (button.modelData.icon || "") !== ""
                            anchors.verticalCenter: parent.verticalCenter
                            name: button.modelData.icon || ""
                            size: 17
                            color: button.selected ? Theme.accent : Theme.iconColor
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: button.modelData.label
                            font.pixelSize: 13
                            font.weight: button.selected ? Font.DemiBold : Font.Normal
                            color: button.selected ? Theme.text : Theme.mutedText
                        }
                    }
                }
            }
        }
    }
}
