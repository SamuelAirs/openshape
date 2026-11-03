// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import OpenShape

// A small arrow at the edge of a scrolling tool palette that has more past
// it (the owner on his Duo, 2026-11-03: "I forget sometimes I can scroll"),
// over a fade of the panel's color. Only a sign: a tool half under it
// still takes the tap. Put one at each end, inside the panel, over its
// Flickable.
Item {
    id: hint

    required property Flickable flickable
    // Toward the end (bottom, or right for a sideways strip) or the start.
    property bool forward: true
    property bool horizontal: false
    property color fadeColor: Theme.panel

    readonly property real position: horizontal ? flickable.contentX : flickable.contentY
    readonly property real overflow: horizontal ? flickable.contentWidth - flickable.width : flickable.contentHeight - flickable.height
    readonly property bool more: overflow > 1 && (forward ? position < overflow - 1 : position > 1)

    visible: more
    width: horizontal ? 28 : flickable.width
    height: horizontal ? flickable.height : 28
    // The panel's corner radius, for a hint along its edge.
    property real radius: 0
    z: 2

    Rectangle {
        anchors.fill: parent
        radius: hint.radius
        gradient: Gradient {
            orientation: hint.horizontal ? Gradient.Horizontal : Gradient.Vertical
            GradientStop { position: 0.0; color: Qt.rgba(hint.fadeColor.r, hint.fadeColor.g, hint.fadeColor.b, 0) }
            GradientStop { position: 0.55; color: hint.fadeColor }
            GradientStop { position: 1.0; color: hint.fadeColor }
        }
        // Solid at the panel's edge: the start's runs the other way.
        rotation: hint.forward ? 0 : 180
    }

    LineIcon {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: hint.horizontal ? 0 : (hint.forward ? 4 : -4)
        anchors.horizontalCenterOffset: hint.horizontal ? (hint.forward ? 4 : -4) : 0
        size: 16
        name: hint.horizontal ? (hint.forward ? "chevron" : "chevronLeft") : (hint.forward ? "chevronDown" : "chevronUp")
        color: Theme.iconColor
    }
}
