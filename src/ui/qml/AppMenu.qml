// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import OpenShape

// FoldForm's menus: a white card with a hairline edge and a soft shadow,
// rows with a line icon, the label and the shortcut (AppMenuItem), fading in.
// Only opacity animates (a menu's rows stay where they will be clicked), and
// only on the way in.
Menu {
    id: menu

    // The icon of the row that opens this menu as a sub-menu.
    property string iconName: ""
    property real minWidth: Theme.touch ? 260 : 240

    padding: 6
    overlap: 4
    delegate: AppMenuItem {}

    background: Item {
        implicitWidth: menu.minWidth
        RectangularShadow {
            anchors.fill: card
            radius: card.radius
            offset.y: 8
            blur: 28
            spread: -2
            color: Theme.shadow
        }
        Rectangle {
            id: card
            anchors.fill: parent
            radius: Theme.menuRadius
            color: Theme.card
            border.color: Theme.cardBorder
            border.width: 1
        }
    }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 110; easing.type: Easing.OutCubic } }
    // No fade out: a menu still closing hands the focus back when it is done,
    // after the dialog its row opened has taken it (Rename...'s name field).
}
