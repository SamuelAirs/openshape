// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic

AbstractButton {
    id: control

    property bool accent: false
    property bool compact: false

    implicitHeight: compact ? (Theme.touch ? 40 : 30) : Theme.controlHeight
    implicitWidth: Math.max(implicitHeight, label.implicitWidth + (compact ? 20 : 24))
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    opacity: enabled ? 1.0 : 0.4

    contentItem: Text {
        id: label
        text: control.text
        font.pixelSize: control.compact ? 12 : 13
        font.weight: control.accent || control.checked ? Font.DemiBold : Font.Normal
        color: control.accent ? "white" : control.checked ? Theme.accent : Theme.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: 8
        color: control.accent ? (control.pressed ? Theme.accentPressed : Theme.accent)
             : control.pressed ? Theme.pressed
             : control.checked ? Theme.checked
             : control.hovered ? Theme.hover
             : control.compact ? Theme.panel : "transparent"
        border.color: control.compact && !control.checked ? Theme.panelBorder : "transparent"
        Behavior on color { ColorAnimation { duration: 90 } }
    }
}
