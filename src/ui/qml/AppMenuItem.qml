// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import OpenShape

// A row of an AppMenu: a line icon, the label (as it is, never HTML: recent
// files' names), an optional muted detail (a recent file's folder), and the
// shortcut at the right (not on touch) or a chevron for a sub-menu.
MenuItem {
    id: item

    property string iconName: subMenu && subMenu.iconName !== undefined ? subMenu.iconName : ""
    property string shortcutText: ""
    property string detail: ""
    // Delete... and its kind: in red.
    property bool destructive: false

    leftPadding: 10
    rightPadding: 10
    topPadding: 0
    bottomPadding: 0
    implicitHeight: Theme.touch ? 44 : 34
    implicitWidth: Math.max(implicitBackgroundWidth, Math.min(leftPadding + implicitContentWidth + rightPadding, 440))
    hoverEnabled: true
    font.pixelSize: Theme.touch ? 15 : 13

    indicator: null
    arrow: null

    contentItem: Item {
        implicitHeight: label.implicitHeight
        Row {
            id: row
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            spacing: 10
            LineIcon {
                id: glyph
                anchors.verticalCenter: parent.verticalCenter
                name: item.iconName
                color: !item.enabled ? Theme.disabledIcon : item.destructive ? Theme.error
                     : item.highlighted ? Theme.accent : Theme.iconColor
            }
            Text {
                id: label
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, row.width - glyph.width - trail.width - detailText.width - 3 * row.spacing)
                text: item.text
                textFormat: Text.PlainText
                font: item.font
                color: !item.enabled ? Theme.disabledText : item.destructive ? Theme.error : Theme.text
                elide: Text.ElideRight
            }
            Text {
                id: detailText
                anchors.verticalCenter: parent.verticalCenter
                visible: item.detail !== ""
                width: visible ? Math.min(implicitWidth, Math.max(0, row.width - glyph.width - label.implicitWidth - trail.width - 3 * row.spacing)) : 0
                text: item.detail
                textFormat: Text.PlainText
                font.pixelSize: item.font.pixelSize - 1
                color: Theme.mutedText
                elide: Text.ElideMiddle
            }
        }
        // The shortcut, or the sub-menu's chevron, at the right edge.
        Item {
            id: trail
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            implicitWidth: item.subMenu ? chevron.width : shortcut.visible ? shortcut.implicitWidth + 16 : 0
            width: implicitWidth
            height: parent.height
            Text {
                id: shortcut
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                visible: !item.subMenu && item.shortcutText !== "" && !Theme.touch
                text: item.shortcutText
                font.pixelSize: 12
                color: Theme.shortcutText
            }
            LineIcon {
                id: chevron
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                visible: !!item.subMenu
                name: "chevron"
                size: 14
                color: Theme.shortcutText
            }
        }
        // (Row's width for implicit sizing: icon, label, detail, gap and trail.)
        implicitWidth: glyph.width + label.implicitWidth + (item.detail !== "" ? detailText.implicitWidth + row.spacing : 0)
                       + 2 * row.spacing + trail.implicitWidth + 12
    }

    background: Rectangle {
        implicitWidth: Theme.touch ? 248 : 228
        radius: 8
        color: !item.enabled ? "transparent"
             : item.down ? (item.destructive ? Theme.destructivePressed : Theme.menuPressed)
             : item.highlighted ? (item.destructive ? Theme.destructiveHighlight : Theme.menuHighlight) : "transparent"
    }
}
