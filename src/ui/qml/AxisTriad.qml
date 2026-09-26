// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import OpenShape

// Which way X, Y and Z point in the current view, in the colors of the axis
// lines through the origin. Axes pointing away from the viewer are dimmed.
Panel {
    id: triad

    required property AppController app
    readonly property real arm: Theme.compact ? 19 : 25
    readonly property var colors: ["#D64545", "#3D9A4F", "#3F6FDC"]

    // Smaller in a compact window (a phone).
    width: Theme.compact ? 64 : 80
    height: width

    Repeater {
        model: 3
        delegate: Item {
            id: axisItem
            required property int index
            readonly property var mark: triad.app.axisTriad[index]
            readonly property real length: triad.arm * Math.hypot(mark.dx, mark.dy)
            anchors.fill: parent
            z: 1 + mark.depth // nearer axes on top; never below the panel (z < 0)
            opacity: mark.depth < -0.2 ? 0.45 : 1.0

            Rectangle {
                x: triad.width / 2
                y: triad.height / 2 - height / 2
                width: axisItem.length
                height: 2
                radius: 1
                color: triad.colors[axisItem.index]
                transformOrigin: Item.Left
                rotation: Math.atan2(axisItem.mark.dy, axisItem.mark.dx) * 180 / Math.PI
            }
            Text {
                text: axisItem.mark.label
                color: triad.colors[axisItem.index]
                font.pixelSize: 11
                font.weight: Font.DemiBold
                // Past the tip; an axis pointing at the viewer labels the center.
                x: triad.width / 2 + axisItem.mark.dx * (triad.arm + 8) - width / 2
                y: triad.height / 2 + axisItem.mark.dy * (triad.arm + 8) - height / 2
            }
        }
    }
    Rectangle {
        anchors.centerIn: parent
        width: 4
        height: 4
        radius: 2
        color: Theme.mutedText
        z: 3
    }
}
