// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Effects
import OpenShape

// A white card on a soft shadow (the Measure workspace's surfaces, like
// Home's cards and the menus).
Rectangle {
    id: card

    property bool lifted: false

    radius: Theme.cardRadius
    color: Theme.card
    border.color: Theme.cardBorder
    border.width: 1

    RectangularShadow {
        z: -1
        anchors.fill: parent
        radius: card.radius
        offset.y: card.lifted ? 8 : 3
        blur: card.lifted ? 28 : 16
        spread: -3
        color: card.lifted ? Theme.shadow : Theme.panelShadow
    }
}
