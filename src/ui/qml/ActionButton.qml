// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic

AbstractButton {
    id: control

    property bool accent: false
    property bool compact: false
    // A framed button on a plain background (Home), rather than in a panel.
    property bool outlined: false
    property int fontSize: compact ? 12 : 13

    implicitHeight: compact ? (Theme.touch ? 40 : 30) : Theme.controlHeight
    implicitWidth: Math.max(implicitHeight, label.implicitWidth + (compact ? 20 : 24))
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    opacity: enabled ? 1.0 : 0.4

    contentItem: Text {
        id: label
        text: control.text
        textFormat: Text.PlainText // labels may hold names from files ("Back to <project>")
        font.pixelSize: control.fontSize
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
             : control.compact || control.outlined ? Theme.panel : "transparent"
        border.color: (control.compact || control.outlined) && !control.checked ? Theme.panelBorder : "transparent"
        Behavior on color { ColorAnimation { duration: 90 } }
    }
}
