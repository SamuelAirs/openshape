// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import OpenShape

// A row of controls that scrolls sideways when it is wider than
// `maximumWidth` (the compact layout of a phone), with a fade at the edge
// that has more. As wide as its content otherwise, and laid out by a plain
// Row, so a regular window sees exactly what it did before (put a RowLayout
// inside for Layout properties).
Item {
    id: scrollRow

    property real maximumWidth: Infinity
    property alias spacing: row.spacing
    // The color the fades blend into (the surface behind the row).
    property color fadeColor: Theme.panel
    default property alias content: row.data
    readonly property bool scrolls: row.implicitWidth > width + 1

    implicitWidth: Math.min(row.implicitWidth, maximumWidth)
    implicitHeight: row.implicitHeight

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: row.implicitWidth
        contentHeight: height
        clip: scrollRow.scrolls
        interactive: scrollRow.scrolls
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: row
        }
    }

    // More to the left / right: a fade at that edge says "scroll".
    Rectangle {
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: 24
        visible: scrollRow.scrolls && flick.contentX > 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: scrollRow.fadeColor }
            GradientStop { position: 1.0; color: Qt.rgba(scrollRow.fadeColor.r, scrollRow.fadeColor.g, scrollRow.fadeColor.b, 0) }
        }
    }
    Rectangle {
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
        width: 24
        visible: scrollRow.scrolls && flick.contentX + flick.width < flick.contentWidth - 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(scrollRow.fadeColor.r, scrollRow.fadeColor.g, scrollRow.fadeColor.b, 0) }
            GradientStop { position: 1.0; color: scrollRow.fadeColor }
        }
    }
}
