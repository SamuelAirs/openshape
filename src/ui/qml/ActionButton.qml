// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import OpenShape

AbstractButton {
    id: control

    property bool accent: false
    property bool compact: false
    // A framed button on a plain background (Home), rather than in a panel.
    property bool outlined: false
    property int fontSize: compact ? 12 : 13
    // A line icon before the label (LineIcon's names); with `alignLeft`
    // the two sit at the left (a column of tools), else in the middle.
    property string iconName: ""
    property bool alignLeft: false
    // Only the icon (the label stays the button's name for accessibility).
    property bool iconOnly: false
    // An icon after the label (a menu's chevron).
    property string trailingIconName: ""

    readonly property color labelColor: control.accent ? "white" : control.checked ? Theme.accent : Theme.text

    implicitHeight: compact ? (Theme.touch ? 40 : 30) : Theme.controlHeight
    implicitWidth: Math.max(implicitHeight, content.implicitWidth + (iconOnly ? 0 : compact ? 20 : 24))
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    opacity: enabled ? 1.0 : 0.4

    contentItem: Item {
        id: content
        implicitWidth: row.implicitWidth
        implicitHeight: label.implicitHeight
        Row {
            id: row
            spacing: 8
            x: control.alignLeft ? 0 : Math.round((parent.width - width) / 2)
            anchors.verticalCenter: parent.verticalCenter
            LineIcon {
                id: glyph
                anchors.verticalCenter: parent.verticalCenter
                visible: control.iconName !== ""
                name: control.iconName
                color: control.accent ? "white" : control.checked ? Theme.accent : Theme.iconColor
            }
            Text {
                id: label
                anchors.verticalCenter: parent.verticalCenter
                visible: !control.iconOnly && control.text !== ""
                text: control.text
                textFormat: Text.PlainText // labels may hold names from files ("Back to <project>")
                font.pixelSize: control.fontSize
                font.weight: control.accent || control.checked ? Font.DemiBold : Font.Normal
                color: control.labelColor
                verticalAlignment: Text.AlignVCenter
            }
            LineIcon {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.trailingIconName !== ""
                name: control.trailingIconName
                size: 14
                color: Theme.shortcutText
            }
        }
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
